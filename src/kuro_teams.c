// Team storage: two dedicated flash sectors written alternately, so a power cut loses one save at most.
#include "global.h"
#include "constants/characters.h"
#include "kuro_link.h"
#include "kuro_session.h"
#include "kuro_teams.h"
#include "pokemon.h"
#include "save.h"
#include "load_save.h"
#include "string_util.h"

#define KURO_SAVE_MAGIC 0x4B55524F
#define KURO_SAVE_VERSION 2
#define KURO_SAVE_VERSION_TEAMS_ONLY 1
#define KURO_TEAM_CHUNK_SIZE 200

STATIC_ASSERT(sizeof(struct KuroTeamMon) == KURO_MON_WIRE_SIZE, KuroTeamMonIsWireSize);

struct KuroSaveData
{
    u32 magic;
    u32 version;
    u32 generation;
    u32 checksum;
    struct KuroTeam teams[KURO_TEAM_SLOTS];
    u8 recentWords[KURO_RECENT_WORDS][KURO_RECENT_WORD_SIZE];
};

STATIC_ASSERT(sizeof(struct KuroSaveData) <= SECTOR_DATA_SIZE - 8, KuroSaveFitsSector);

static const u8 sSectors[2] = {SECTOR_ID_TRAINER_HILL, SECTOR_ID_RECORDED_BATTLE};
static const u8 sText_TeamDash[] = _("Team ");
static const u8 sTagAny[] = _("Any format");
static const u8 sTagOu[] = _("Gen 9 OU");
static const u8 sTagDoubles[] = _("Gen 9 Doubles OU");
static const u8 *const sFormatTags[KURO_FORMAT_TAG_COUNT] = {sTagAny, sTagOu, sTagDoubles};

EWRAM_DATA struct KuroTeam gKuroTeams[KURO_TEAM_SLOTS] = {0};
EWRAM_DATA u8 gKuroRecentWords[KURO_RECENT_WORDS][KURO_RECENT_WORD_SIZE] = {0};
static EWRAM_DATA u32 sGeneration = 0;
static EWRAM_DATA u8 sNextSector = 0;
static EWRAM_DATA u8 sSectorBuffer[SECTOR_DATA_SIZE] = {0};
static EWRAM_DATA u8 sPushBuffer[1 + KURO_TEAM_SIZE * KURO_MON_WIRE_SIZE] = {0};
static EWRAM_DATA u16 sPushLength = 0;

static u32 ChecksumBytes(u32 sum, const u8 *bytes, u32 size)
{
    u32 i;

    for (i = 0; i < size; i++)
        sum = (sum * 31) + bytes[i];
    return sum;
}

static u32 Checksum(const struct KuroSaveData *data, u32 version)
{
    u32 sum = ChecksumBytes(data->generation ^ version, (const u8 *)data->teams, sizeof(data->teams));

    if (version != KURO_SAVE_VERSION_TEAMS_ONLY)
        sum = ChecksumBytes(sum, (const u8 *)data->recentWords, sizeof(data->recentWords));
    return sum;
}

void KuroTeams_DefaultName(u8 slot, u8 *name)
{
    u8 *end = StringCopy(name, sText_TeamDash);

    ConvertIntToDecimalStringN(end, slot + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
}

static void InitEmptyTeams(void)
{
    u32 i;

    memset(gKuroTeams, 0, sizeof(gKuroTeams));
    memset(gKuroRecentWords, EOS, sizeof(gKuroRecentWords));
    for (i = 0; i < KURO_TEAM_SLOTS; i++)
        KuroTeams_DefaultName(i, gKuroTeams[i].name);
}

void KuroTeams_Load(void)
{
    const struct KuroSaveData *data = (const struct KuroSaveData *)sSectorBuffer;
    u32 i, bestGeneration = 0;
    bool32 found = FALSE;

    InitEmptyTeams();
    sGeneration = 0;
    sNextSector = 0;
    if (!gFlashMemoryPresent)
        return;
    for (i = 0; i < ARRAY_COUNT(sSectors); i++)
    {
        if (TryReadSpecialSaveSector(sSectors[i], sSectorBuffer) != SAVE_STATUS_OK)
            continue;
        if (data->magic != KURO_SAVE_MAGIC
         || (data->version != KURO_SAVE_VERSION && data->version != KURO_SAVE_VERSION_TEAMS_ONLY)
         || data->checksum != Checksum(data, data->version))
            continue;
        if (!found || data->generation >= bestGeneration)
        {
            found = TRUE;
            bestGeneration = data->generation;
            memcpy(gKuroTeams, data->teams, sizeof(gKuroTeams));
            if (data->version == KURO_SAVE_VERSION)
                memcpy(gKuroRecentWords, data->recentWords, sizeof(gKuroRecentWords));
            else
                memset(gKuroRecentWords, EOS, sizeof(gKuroRecentWords));
            sNextSector = (i + 1) % ARRAY_COUNT(sSectors);
        }
    }
    sGeneration = bestGeneration;
}

bool32 KuroTeams_Save(void)
{
    struct KuroSaveData *data = (struct KuroSaveData *)sSectorBuffer;
    u32 status;

    if (!gFlashMemoryPresent)
        return FALSE;
    memset(sSectorBuffer, 0, sizeof(sSectorBuffer));
    data->magic = KURO_SAVE_MAGIC;
    data->version = KURO_SAVE_VERSION;
    data->generation = sGeneration + 1;
    memcpy(data->teams, gKuroTeams, sizeof(gKuroTeams));
    memcpy(data->recentWords, gKuroRecentWords, sizeof(gKuroRecentWords));
    data->checksum = Checksum(data, KURO_SAVE_VERSION);
    status = TryWriteSpecialSaveSector(sSectors[sNextSector], sSectorBuffer);
    if (status != SAVE_STATUS_OK)
        return FALSE;
    sGeneration++;
    sNextSector = (sNextSector + 1) % ARRAY_COUNT(sSectors);
    return TRUE;
}

const u8 *KuroTeams_FormatTagName(u8 tag)
{
    return sFormatTags[tag < KURO_FORMAT_TAG_COUNT ? tag : 0];
}

void KuroTeams_InitMon(struct KuroTeamMon *mon, u16 species)
{
    u32 i;

    memset(mon, 0, sizeof(*mon));
    mon->species = species;
    mon->ability = GetSpeciesAbility(species, 0);
    mon->level = MAX_LEVEL;
    mon->nature = NATURE_SERIOUS;
    for (i = 0; i < NUM_STATS; i++)
        mon->ivs[i] = MAX_PER_STAT_IVS;
    mon->teraType = GetSpeciesType(species, 0);
    mon->nickname[0] = EOS;
}

bool32 KuroTeams_Upload(u8 slot)
{
    const struct KuroTeam *team = &gKuroTeams[slot];
    u8 body[3 + KURO_TEAM_CHUNK_SIZE];
    u8 stream[1 + KURO_TEAM_SIZE * KURO_MON_WIRE_SIZE];
    u32 total = 1 + team->count * KURO_MON_WIRE_SIZE;
    u32 offset = 0, chunk = 0;

    stream[0] = team->count;
    memcpy(&stream[1], team->mons, team->count * KURO_MON_WIRE_SIZE);
    do
    {
        u32 size = min(total - offset, KURO_TEAM_CHUNK_SIZE);

        body[0] = slot;
        body[1] = chunk++;
        body[2] = (offset + size == total);
        memcpy(&body[3], &stream[offset], size);
        if (!KuroLink_SendFrame(KURO_OP_UPLOAD_TEAM, body, 3 + size))
            return FALSE;
        offset += size;
    } while (offset < total);
    return TRUE;
}

// TEAM_PUSH: slot, chunk index, final flag, then team bytes.
void KuroTeams_ApplyPush(const u8 *payload, u16 length)
{
    u8 slot;
    u16 size;

    if (length < 3 || payload[0] >= KURO_TEAM_SLOTS)
        return;
    slot = payload[0];
    if (payload[1] == 0)
        sPushLength = 0;
    size = length - 3;
    if (sPushLength + size > sizeof(sPushBuffer))
        return;
    memcpy(&sPushBuffer[sPushLength], &payload[3], size);
    sPushLength += size;
    if (payload[2] && sPushBuffer[0] >= 1 && sPushBuffer[0] <= KURO_TEAM_SIZE
     && sPushLength == 1 + sPushBuffer[0] * KURO_MON_WIRE_SIZE)
    {
        struct KuroTeam *team = &gKuroTeams[slot];

        memset(team->mons, 0, sizeof(team->mons));
        memcpy(team->mons, &sPushBuffer[1], sPushBuffer[0] * KURO_MON_WIRE_SIZE);
        team->count = sPushBuffer[0];
        team->badge = KURO_BADGE_UNCHECKED;
        gKuroSession.teamPushed = TRUE;
    }
}
