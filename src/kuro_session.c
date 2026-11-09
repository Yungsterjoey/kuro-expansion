#include "global.h"
#include "constants/characters.h"
#include "kuro_build_info.h"
#include "kuro_link.h"
#include "kuro_playback.h"
#include "kuro_session.h"
#include "kuro_teams.h"
#include "string_util.h"

#define KURO_PROTOCOL_MINOR 1
#define KURO_FEATURE_TERA (1 << 1)
#define KURO_FEATURE_TEAM_IMPORT (1 << 5)
#define KURO_FEATURE_ACCOUNTS (1 << 6)
#define CAPABILITY_FIXED_SIZE 18

EWRAM_DATA struct KuroSession gKuroSession = {0};

static const u8 sRomBuild[] = _("KURO ROM 0.2");
static const u8 sCommit[8] = KURO_EXPANSION_COMMIT_BYTES;

const u8 *KuroSession_RomBuildString(void)
{
    return sRomBuild;
}

void KuroSession_Reset(void)
{
    memset(&gKuroSession, 0, sizeof(gKuroSession));
}

static u16 BuildCapabilityBlock(u8 *out)
{
    u32 crc = KURO_IDMAP_CRC;
    u32 features = KURO_FEATURE_TERA | KURO_FEATURE_TEAM_IMPORT | KURO_FEATURE_ACCOUNTS;
    u16 i, length = 0;

    out[length++] = KURO_PROTOCOL_MAJOR;
    out[length++] = KURO_PROTOCOL_MINOR;
    for (i = 0; i < 4; i++)
        out[length++] = (crc >> (8 * i)) & 0xFF;
    for (i = 0; i < ARRAY_COUNT(sCommit); i++)
        out[length++] = sCommit[i];
    for (i = 0; i < 4; i++)
        out[length++] = (features >> (8 * i)) & 0xFF;
    for (i = 0; sRomBuild[i] != EOS; i++)
        out[length++] = sRomBuild[i];
    out[length++] = EOS;
    return length;
}

static void CopyText(u8 *dest, u32 destSize, const u8 *source, u16 available, u16 *consumed)
{
    u16 i;

    for (i = 0; i < available && source[i] != EOS; i++)
    {
        if (i < destSize - 1)
            dest[i] = source[i];
    }
    dest[min(i, destSize - 1)] = EOS;
    *consumed = (i < available) ? i + 1 : i;
}

static void HandleHello(const u8 *payload, u16 length)
{
    u8 block[KURO_MAX_PAYLOAD];
    u16 blockLength = BuildCapabilityBlock(block);
    u16 consumed;
    bool32 matches = FALSE;

    if (length >= CAPABILITY_FIXED_SIZE)
    {
        matches = (payload[0] == KURO_PROTOCOL_MAJOR)
               && (memcmp(&payload[2], &block[2], 4) == 0)
               && (memcmp(&payload[6], sCommit, sizeof(sCommit)) == 0);
        CopyText(gKuroSession.relayBuild, sizeof(gKuroSession.relayBuild),
                 &payload[CAPABILITY_FIXED_SIZE], length - CAPABILITY_FIXED_SIZE, &consumed);
    }

    gKuroSession.handshakeDone = TRUE;
    gKuroSession.mismatch = !matches;
    KuroLink_SendFrame(matches ? KURO_OP_HELLO_ACK : KURO_OP_ERR_VERSION, block, blockLength);
}

static void HandleFormatList(const u8 *payload, u16 length)
{
    u16 offset = 2, consumed;

    if (length < 2)
        return;
    if (payload[0] == 0)
        gKuroSession.formatCount = 0;

    while (offset + 2 < length && gKuroSession.formatCount < KURO_MAX_FORMATS)
    {
        struct KuroFormat *format = &gKuroSession.formats[gKuroSession.formatCount++];

        format->index = payload[offset];
        format->flags = payload[offset + 1];
        CopyText(format->name, sizeof(format->name), &payload[offset + 2], length - offset - 2, &consumed);
        offset += 2 + consumed;
    }
}

static void HandleAccountList(const u8 *payload, u16 length)
{
    u16 offset = 2, consumed;
    u32 count = 0;

    if (length < 2)
        return;
    gKuroSession.accountStoreAvailable = (payload[0] & KURO_ACCOUNT_FLAG_STORE) != 0;
    while (count < payload[1] && count < KURO_MAX_ACCOUNTS && offset < length)
    {
        CopyText(gKuroSession.accountNames[count++], KURO_USERNAME_SIZE, &payload[offset], length - offset, &consumed);
        offset += consumed;
    }
    gKuroSession.accountCount = count;
    gKuroSession.accountListReady = TRUE;
}

static void HandleRatings(const u8 *payload, u16 length)
{
    u32 i, count;

    if (length < 1)
        return;
    count = min(payload[0], KURO_MAX_FORMATS);
    count = min(count, (u32)(length - 1) / 3);
    for (i = 0; i < count; i++)
    {
        gKuroSession.ratings[i].formatIndex = payload[1 + i * 3];
        gKuroSession.ratings[i].elo = payload[2 + i * 3] | (payload[3 + i * 3] << 8);
    }
    gKuroSession.ratingCount = count;
}

static void HandleAvatarList(const u8 *payload, u16 length)
{
    u32 i, count;

    if (length < 3)
        return;
    gKuroSession.currentAvatar = payload[0] | (payload[1] << 8);
    count = min(payload[2], KURO_MAX_AVATARS);
    count = min(count, (u32)(length - 3) / 2);
    for (i = 0; i < count; i++)
        gKuroSession.avatars[i] = payload[3 + i * 2] | (payload[4 + i * 2] << 8);
    gKuroSession.avatarCount = count;
    gKuroSession.avatarListReady = TRUE;
}

bool32 KuroSession_IsSessionOpcode(u32 opcode)
{
    return opcode < KURO_FIRST_BATTLE_OPCODE || opcode >= KURO_FIRST_SOCIAL_OPCODE;
}

void KuroSession_HandleFrame(u8 opcode, const u8 *payload, u16 length)
{
    u16 consumed;

    switch (opcode)
    {
    case KURO_OP_HELLO:
        HandleHello(payload, length);
        break;
    case KURO_OP_STATUS:
        if (length >= 1)
        {
            gKuroSession.status = payload[0];
            CopyText(gKuroSession.username, sizeof(gKuroSession.username), &payload[1], length - 1, &consumed);
            CopyText(gKuroSession.detail, sizeof(gKuroSession.detail), &payload[1 + consumed], length - 1 - consumed, &consumed);
        }
        break;
    case KURO_OP_FORMAT_LIST:
        HandleFormatList(payload, length);
        break;
    case KURO_OP_SEARCH_STATE:
        if (length >= 1)
            gKuroSession.searchState = payload[0];
        break;
    case KURO_OP_POPUP:
        CopyText(gKuroSession.popup, sizeof(gKuroSession.popup), payload, length, &consumed);
        gKuroSession.popupPending = TRUE;
        break;
    case KURO_OP_TEAM_PUSH:
        KuroTeams_ApplyPush(payload, length);
        break;
    case KURO_OP_TEAM_EXPORT_DONE:
        if (length >= 1)
        {
            gKuroSession.exportOk = payload[0];
            CopyText(gKuroSession.exportText, sizeof(gKuroSession.exportText), &payload[1], length - 1, &consumed);
            gKuroSession.exportDone = TRUE;
        }
        break;
    case KURO_OP_ACCOUNT_LIST:
        HandleAccountList(payload, length);
        break;
    case KURO_OP_LOGIN_RESULT:
        if (length >= 1)
        {
            gKuroSession.loginCode = payload[0];
            CopyText(gKuroSession.loginText, sizeof(gKuroSession.loginText), &payload[1], length - 1, &consumed);
            gKuroSession.loginResultPending = TRUE;
        }
        break;
    case KURO_OP_RATINGS:
        HandleRatings(payload, length);
        break;
    case KURO_OP_AVATAR_LIST:
        HandleAvatarList(payload, length);
        break;
    default:
        break;
    }
}
