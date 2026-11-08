// Puppet battle playback. Replaces the turn engine with an event player that drives the
// expansion's battle controllers, so every outcome comes from the event stream.
// This file must never call damage, AI, experience, item or catch code.
#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_controllers.h"
#include "battle_interface.h"
#include "battle_message.h"
#include "battle_setup.h"
#include "bg.h"
#include "constants/battle_anim.h"
#include "constants/songs.h"
#include "constants/trainers.h"
#include "kuro_link.h"
#include "kuro_session.h"
#include "party_menu.h"
#include "m4a.h"
#include "palette.h"
#include "window.h"
#include "constants/rgb.h"
#include "constants/party_menu.h"
#include "kuro_playback.h"
#include "main.h"
#include "move.h"
#include "pokemon.h"
#include "random.h"
#include "sound.h"
#include "string_util.h"
#include "text.h"
#include "data/kuro_m0_frames.h"
#include "data/kuro_m0_frames_switches.h"

enum KuroOpcode
{
    KURO_OP_BATTLE_START = 0x40,
    KURO_OP_REQUEST = 0x42,
    KURO_OP_TURN = 0x43,
    KURO_OP_SWITCH_IN = 0x50,
    KURO_OP_MOVE = 0x51,
    KURO_OP_HP_CHANGE = 0x52,
    KURO_OP_STATUS_SET = 0x53,
    KURO_OP_STAT_STAGE = 0x54,
    KURO_OP_FAINT = 0x55,
    KURO_OP_EFFECTIVENESS = 0x56,
    KURO_OP_FIELD = 0x57,
    KURO_OP_SIDE_COND = 0x58,
    KURO_OP_ABILITY_SHOW = 0x5A,
    KURO_OP_MESSAGE = 0x5F,
    KURO_OP_TIMER_INFO = 0x60,
    KURO_OP_BATTLE_END = 0x61,
    KURO_OP_RESYNC_BEGIN = 0x63,
    KURO_OP_RESYNC_END = 0x64,
    KURO_OP_CHOOSE_MOVE = 0x20,
    KURO_OP_CHOOSE_SWITCH = 0x21,
    KURO_OP_UNDO_CHOICE = 0x23,
    KURO_OP_FORFEIT = 0x24,
    KURO_OP_TIMER = 0x25,
    KURO_OP_ANIM_DONE = 0x30,
    KURO_OP_ERR_DESYNC = 0xE0,
};

enum KuroActionKind
{
    KURO_ACT_TEXT,
    KURO_ACT_MOVE_ANIM,
    KURO_ACT_HIT,
    KURO_ACT_HP_BAR,
    KURO_ACT_SET_HP,
    KURO_ACT_STATUS,
    KURO_ACT_STATUS_ANIM,
    KURO_ACT_STAT_ANIM,
    KURO_ACT_FAINT_CRY,
    KURO_ACT_FAINT,
    KURO_ACT_RETURN,
    KURO_ACT_SWITCH_IN,
    KURO_ACT_BATTLE_ANIM,
    KURO_ACT_ABILITY_POPUP,
    KURO_ACT_SE,
    KURO_ACT_FINISH,
};

enum KuroState
{
    KURO_STATE_INTRO,
    KURO_STATE_WAIT_INTRO,
    KURO_STATE_RUN,
    KURO_STATE_HALTED,
    KURO_STATE_LEAVING,
    KURO_STATE_FADING,
};

#define KURO_MAX_ACTIONS 8
#define KURO_TEXT_PAUSE_FRAMES 40
#define KURO_ABILITY_POPUP_FRAMES 60
#define KURO_PERSONALITY_SEARCH_LIMIT 512
#define KURO_RNG_SEED 0x4B55524F
#define KURO_SETTLE_FRAMES 12
#define KURO_RESULT_FRAMES 150
#define SWITCH_IN_NICKNAME_OFFSET 14
#define KURO_REQUEST_FORCE_SWITCH (1 << 0)
#define KURO_REQUEST_REVIVING (1 << 5)
#define KURO_REQUEST_TRAPPED (1 << 1)
#define KURO_REQUEST_CAN_TERA (1 << 2)
#define KURO_REQUEST_WAIT (1 << 3)
#define KURO_TIMER_WARNING_SECONDS 30
#define KURO_FRAMES_PER_SECOND 60
#define KURO_CHOOSE_FLAG_TERA 1
#define REQUEST_FLAGS 2
#define REQUEST_MOVES 4
#define REQUEST_MOVE_SIZE 6
#define REQUEST_PARTY_COUNT 28
#define REQUEST_PARTY 29
#define REQUEST_MEMBER_SIZE 25

#define KURO_MOVE_MISS (1 << 0)
#define KURO_MOVE_STILL (1 << 1)
#define KURO_MOVE_NOTARGET (1 << 2)

#define KURO_POSITION_SIDE 0x80
#define KURO_HP_CAUSE_DIRECT 0

struct KuroAction
{
    u8 kind;
    u8 battler;
    u8 target;
    s16 value;
    const u8 *text;
};

struct KuroPlayback
{
    u8 state;
    u8 actionCount;
    u8 actionIndex;
    u8 subStep;
    u16 timer;
    u16 cursor;
    u16 ackSeq;
    u16 framesSize;
    const u8 *frames;
    bool8 isWire;
    u16 lastSeq;
    u8 pendingTxOpcode;
    u8 pendingTxLength;
    u8 pendingTxBody[4];
    u8 payload[KURO_MAX_PAYLOAD + 1];
    bool8 isClient;
    bool8 teraSelected;
    bool8 hudShown;
    u16 timerSeconds;
    bool8 timerOn;
    u8 timerFrames;
    u8 hudText[24];
    bool8 resyncing;
    bool8 onFieldBeforeResync[MAX_BATTLERS_COUNT];
    bool8 hasStash;
    u8 stashOpcode;
    u16 stashLength;
    u8 stash[KURO_MAX_PAYLOAD + 1];
    u8 phase;
    u8 labelTimer;
    bool8 forfeitPromptPending;
    u8 settle;
    bool8 haveRequest;
    bool8 requestAnswered;
    bool8 sawTurn;
    bool8 reopen;
    u8 request[KURO_MAX_PAYLOAD + 1];
    u8 names[MAX_BATTLERS_COUNT][POKEMON_NAME_LENGTH + 1];
    struct KuroAction noAction;
    bool8 needsAck;
    bool8 battlerActive[MAX_BATTLERS_COUNT];
    u8 partyCount[NUM_BATTLE_SIDES];
    u8 opponentName[20];
    struct KuroAction actions[KURO_MAX_ACTIONS];
    u8 text[KURO_MAX_ACTIONS][80];
};

EWRAM_DATA volatile u16 gKuroAckSeq = 0;
EWRAM_DATA volatile u8 gKuroStatus = KURO_STATUS_RUNNING;
static EWRAM_DATA struct KuroPlayback sKuro = {0};

struct KuroFixture
{
    const u8 *frames;
    u16 size;
};

static const struct KuroFixture sKuroFixtures[] =
{
    {sKuroM0Frames, sizeof(sKuroM0Frames)},
    {sKuroM0FramesSwitches, sizeof(sKuroM0FramesSwitches)},
};

const volatile u8 gKuroBootMode = KURO_BOOT_CLIENT;
EWRAM_DATA volatile u32 gKuroLoopbackCorrupt = 0;

static const u8 sText_Foe[] = _("Foe ");
static const u8 sText_GoSpace[] = _("Go! ");
static const u8 sText_SentOut[] = _(" sent out\n");
static const u8 sText_Exclaim[] = _("!");
static const u8 sText_WantsToBattle[] = _("\nwants to battle!");
static const u8 sText_Used[] = _(" used\n");
static const u8 sText_Fainted[] = _("\nfainted!");
static const u8 sText_Crit[] = _("A critical hit!");
static const u8 sText_SuperEffective[] = _("It's super effective!");
static const u8 sText_Resisted[] = _("It's not very effective…");
static const u8 sText_NoEffectOn[] = _("It doesn't affect\n");
static const u8 sText_Avoided[] = _("\navoided the attack!");
static const u8 sText_Failed[] = _("But it failed!");
static const u8 sText_YouWon[] = _("You won the battle!");
static const u8 sText_YouLost[] = _("You lost the battle!");
static const u8 sText_Tie[] = _("The battle ended in a tie!");
static const u8 sText_PlaybackError[] = _("Playback error.\nThe event data is invalid.");

static const u32 sKuroStatusFlags[] =
{
    [1] = STATUS1_PARALYSIS,
    [2] = STATUS1_POISON,
    [3] = STATUS1_TOXIC_POISON,
    [4] = STATUS1_BURN,
    [5] = STATUS1_SLEEP_TURN(1),
    [6] = STATUS1_FREEZE,
};

static const u8 sKuroWeatherAnims[] =
{
    [1] = B_ANIM_RAIN_CONTINUES,
    [2] = B_ANIM_SUN_CONTINUES,
    [3] = B_ANIM_SANDSTORM_CONTINUES,
    [4] = B_ANIM_HAIL_CONTINUES,
    [5] = B_ANIM_SNOW_CONTINUES,
};

static inline u16 ReadU16(const u8 *data)
{
    return data[0] | (data[1] << 8);
}

static enum BattlerId BattlerFromPosition(u8 position)
{
    return GetBattlerAtPosition((position & KURO_POSITION_SIDE) ? B_POSITION_OPPONENT_LEFT : B_POSITION_PLAYER_LEFT);
}

static void BuildBattlerName(u8 *dst, enum BattlerId battler)
{
    dst[0] = EOS;
    if (!IsOnPlayerSide(battler))
        StringCopy(dst, sText_Foe);
    StringAppend(dst, sKuro.names[battler]);
}

static void ApplySwitchIn(enum BattlerId battler, u8 slot)
{
    gBattlerPartyIndexes[battler] = slot;
    PokemonToBattleMon(GetBattlerMon(battler), &gBattleMons[battler]);
    sKuro.battlerActive[battler] = TRUE;
}

static void SetBattlerHp(enum BattlerId battler, u16 hp);

static struct KuroAction *AddAction(u8 kind, enum BattlerId battler, s16 value)
{
    struct KuroAction *action;

    // A resync rebuilds state with no presentation, so state changes apply at once.
    if (sKuro.resyncing)
    {
        if (kind == KURO_ACT_SET_HP)
            SetBattlerHp(battler, (u16)value);
        else if (kind == KURO_ACT_SWITCH_IN)
            ApplySwitchIn(battler, value);
        else if (kind == KURO_ACT_FAINT)
            sKuro.battlerActive[battler] = FALSE;
        return &sKuro.noAction;
    }

    if (sKuro.actionCount >= KURO_MAX_ACTIONS)
        return &sKuro.actions[KURO_MAX_ACTIONS - 1];
    action = &sKuro.actions[sKuro.actionCount++];
    action->kind = kind;
    action->battler = battler;
    action->value = value;
    action->target = battler;
    action->text = NULL;
    return action;
}

static void AddText(const u8 *text)
{
    AddAction(KURO_ACT_TEXT, 0, 0)->text = text;
}

static u8 *NextTextBuffer(void)
{
    u32 index = sKuro.actionCount < KURO_MAX_ACTIONS ? sKuro.actionCount : KURO_MAX_ACTIONS - 1;
    sKuro.text[index][0] = EOS;
    return sKuro.text[index];
}

static u32 PersonalityForGender(enum Species species, u8 gender)
{
    u32 personality;
    u8 wanted = (gender == 1) ? MON_MALE : MON_FEMALE;

    if (gender == 0)
        return 0;
    for (personality = 0; personality < KURO_PERSONALITY_SEARCH_LIMIT; personality++)
    {
        if (GetGenderFromSpeciesAndPersonality(species, personality) == wanted)
            return personality;
    }
    return 0;
}

// Finds the party slot already holding this species on this side, or fills the next free one.
static u8 FindOrCreatePartySlot(enum BattlerId battler, enum Species species, u8 level, u8 gender, bool8 shiny)
{
    enum BattleTrainer trainer = GetBattlerTrainer(battler);
    enum BattleSide side = GetBattlerSide(battler);
    u8 slot;

    for (slot = 0; slot < sKuro.partyCount[side]; slot++)
    {
        if (GetMonData(&gParties[trainer][slot], MON_DATA_SPECIES) == species)
            return slot;
    }

    if (sKuro.partyCount[side] >= PARTY_SIZE)
        return PARTY_SIZE - 1;

    slot = sKuro.partyCount[side]++;
    CreateMon(&gParties[trainer][slot], species, level, PersonalityForGender(species, gender), OTID_STRUCT_PRESET(0));
    SetMonData(&gParties[trainer][slot], MON_DATA_IS_SHINY, &shiny);
    gPartiesCount[trainer] = sKuro.partyCount[side];
    return slot;
}

static void SetBattlerHp(enum BattlerId battler, u16 hp)
{
    SetMonData(GetBattlerMon(battler), MON_DATA_HP, &hp);
    gBattleMons[battler].hp = hp;
}

static void SetBattlerStatus(enum BattlerId battler, u32 status)
{
    SetMonData(GetBattlerMon(battler), MON_DATA_STATUS, &status);
    gBattleMons[battler].status1 = status;
}

static void HaltWithError(void)
{
    StringCopy(gDisplayedStringBattle, sText_PlaybackError);
    BattlePutTextOnWindow(gDisplayedStringBattle, B_WIN_MSG);
    gKuroStatus = KURO_STATUS_ERROR;
    sKuro.state = KURO_STATE_HALTED;
}

static void QueueSwitchIn(const u8 *payload, u16 length)
{
    enum BattlerId battler = BattlerFromPosition(payload[2]);
    enum Species species = ReadU16(&payload[3]);
    u8 level = payload[5];
    u8 slot = FindOrCreatePartySlot(battler, species, level, payload[6], payload[7]);
    enum BattleTrainer trainer = GetBattlerTrainer(battler);
    struct Pokemon *mon = &gParties[trainer][slot];
    u16 hp = ReadU16(&payload[9]);
    u16 maxHp = ReadU16(&payload[11]);
    u8 statusId = payload[13];
    u32 status = (statusId < ARRAY_COUNT(sKuroStatusFlags)) ? sKuroStatusFlags[statusId] : 0;
    u8 *text;

    if (length > SWITCH_IN_NICKNAME_OFFSET + 1)
    {
        StringCopyN(sKuro.names[battler], &payload[SWITCH_IN_NICKNAME_OFFSET], POKEMON_NAME_LENGTH);
        sKuro.names[battler][POKEMON_NAME_LENGTH] = EOS;
        SetMonData(mon, MON_DATA_NICKNAME, sKuro.names[battler]);
    }
    else
    {
        StringCopy(sKuro.names[battler], GetSpeciesName(species));
    }

    if (sKuro.battlerActive[battler])
        AddAction(KURO_ACT_RETURN, battler, 0);

    SetMonData(mon, MON_DATA_LEVEL, &level);
    SetMonData(mon, MON_DATA_MAX_HP, &maxHp);
    SetMonData(mon, MON_DATA_HP, &hp);
    SetMonData(mon, MON_DATA_STATUS, &status);

    text = NextTextBuffer();
    if (IsOnPlayerSide(battler))
    {
        StringCopy(text, sText_GoSpace);
        StringAppend(text, sKuro.names[battler]);
        StringAppend(text, sText_Exclaim);
    }
    else
    {
        StringCopy(text, sKuro.opponentName);
        StringAppend(text, sText_SentOut);
        StringAppend(text, sKuro.names[battler]);
        StringAppend(text, sText_Exclaim);
    }
    AddText(text);
    AddAction(KURO_ACT_SWITCH_IN, battler, slot);
}

static void QueueMove(const u8 *payload)
{
    enum BattlerId battler = BattlerFromPosition(payload[2]);
    enum Move move = ReadU16(&payload[3]);
    u8 flags = payload[6];
    u8 *text = NextTextBuffer();

    BuildBattlerName(text, battler);
    StringAppend(text, sText_Used);
    StringAppend(text, GetMoveName(move));
    StringAppend(text, sText_Exclaim);
    AddText(text);

    if (!(flags & (KURO_MOVE_MISS | KURO_MOVE_STILL)) && move != MOVE_NONE)
        AddAction(KURO_ACT_MOVE_ANIM, battler, move)->target = BattlerFromPosition(payload[5]);
}

static void QueueHpChange(const u8 *payload)
{
    enum BattlerId battler = BattlerFromPosition(payload[2]);
    u16 hp = ReadU16(&payload[3]);
    s32 delta = (s32)gBattleMons[battler].hp - (s32)hp;

    if (payload[7] == KURO_HP_CAUSE_DIRECT && delta > 0)
        AddAction(KURO_ACT_HIT, battler, 0);
    if (delta != 0)
        AddAction(KURO_ACT_HP_BAR, battler, delta);
    AddAction(KURO_ACT_SET_HP, battler, hp);
}

static void QueueStatus(const u8 *payload)
{
    enum BattlerId battler = BattlerFromPosition(payload[2]);
    u8 statusId = payload[3];
    u32 status = (statusId < ARRAY_COUNT(sKuroStatusFlags)) ? sKuroStatusFlags[statusId] : 0;
    bool32 isSet = payload[4];

    SetBattlerStatus(battler, isSet ? status : 0);
    AddAction(KURO_ACT_STATUS, battler, isSet);
    if (isSet)
        AddAction(KURO_ACT_STATUS_ANIM, battler, 0);
}

static void QueueStatStage(const u8 *payload)
{
    enum BattlerId battler = BattlerFromPosition(payload[2]);
    u8 stat = payload[3];
    s8 delta = (s8)payload[4];
    s32 stage = gBattleMons[battler].statStages[stat] + delta;
    u32 base;

    gBattleMons[battler].statStages[stat] = min(MAX_STAT_STAGE, max(MIN_STAT_STAGE, stage));
    if (delta > 0)
        base = (delta >= 2) ? STAT_ANIM_PLUS2 : STAT_ANIM_PLUS1;
    else
        base = (delta <= -2) ? STAT_ANIM_MINUS2 : STAT_ANIM_MINUS1;
    AddAction(KURO_ACT_STAT_ANIM, battler, base + stat);
}

static void QueueFaint(const u8 *payload)
{
    enum BattlerId battler = BattlerFromPosition(payload[2]);
    u8 *text = NextTextBuffer();

    BuildBattlerName(text, battler);
    StringAppend(text, sText_Fainted);
    AddAction(KURO_ACT_FAINT_CRY, battler, 0);
    AddAction(KURO_ACT_FAINT, battler, 0);
    AddText(text);
}

static void QueueEffectiveness(const u8 *payload)
{
    enum BattlerId battler = BattlerFromPosition(payload[2]);
    u8 *text = NextTextBuffer();

    switch (payload[3])
    {
    case 1: AddText(sText_Crit); break;
    case 2: AddText(sText_SuperEffective); break;
    case 3: AddText(sText_Resisted); break;
    case 4:
        StringCopy(text, sText_NoEffectOn);
        BuildBattlerName(text + StringLength(text), battler);
        StringAppend(text, sText_Exclaim);
        AddText(text);
        break;
    case 5:
        BuildBattlerName(text, battler);
        StringAppend(text, sText_Avoided);
        AddText(text);
        break;
    default:
        AddText(sText_Failed);
        break;
    }
}

static void QueueField(const u8 *payload)
{
    u8 condition = payload[2];
    enum BattlerId battler = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);

    // Weather animations read the owner's sprite, so an emptied slot cannot own one.
    if (!sKuro.battlerActive[battler])
        battler = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);

    if (condition != 0 && condition < ARRAY_COUNT(sKuroWeatherAnims) && payload[3] != 1 && sKuro.battlerActive[battler])
        AddAction(KURO_ACT_BATTLE_ANIM, battler, sKuroWeatherAnims[condition]);
}

static void BeginResync(void)
{
    u32 i;

    sKuro.resyncing = TRUE;
    sKuro.lastSeq = 0;
    sKuro.haveRequest = FALSE;
    sKuro.requestAnswered = FALSE;
    sKuro.sawTurn = FALSE;
    sKuro.partyCount[B_SIDE_PLAYER] = 0;
    sKuro.partyCount[B_SIDE_OPPONENT] = 0;
    for (i = 0; i < MAX_BATTLERS_COUNT; i++)
    {
        sKuro.onFieldBeforeResync[i] = sKuro.battlerActive[i];
        sKuro.battlerActive[i] = FALSE;
    }
    ZeroPlayerPartyMons();
    ZeroEnemyPartyMons();
}

// Replaces what was on screen with the rebuilt state.
static void EndResync(void)
{
    u32 i, slot;

    sKuro.resyncing = FALSE;
    sKuro.actionCount = 0;
    sKuro.actionIndex = 0;
    sKuro.subStep = 0;
    sKuro.reopen = TRUE;
    for (i = 0; i < gBattlersCount; i++)
    {
        if (sKuro.onFieldBeforeResync[i])
            AddAction(KURO_ACT_RETURN, i, TRUE);
        if (sKuro.battlerActive[i])
        {
            slot = gBattlerPartyIndexes[i];
            AddAction(KURO_ACT_SWITCH_IN, i, slot);
        }
    }

    // A request we already answered comes back flagged wait, and B can still undo it.
    if (sKuro.isClient && sKuro.haveRequest && (sKuro.request[REQUEST_FLAGS] & KURO_REQUEST_WAIT))
    {
        sKuro.phase = KURO_UI_WAITING;
        gKuroUiScreen = KURO_UI_WAITING;
    }
}

static void QueueBattleEnd(const u8 *payload)
{
    switch (payload[2])
    {
    case 1: AddText(sText_YouWon); break;
    case 2: AddText(sText_YouLost); break;
    default: AddText(sText_Tie); break;
    }
    AddAction(KURO_ACT_FINISH, 0, 0);
}

static bool32 IsFrameValid(u16 cursor, u16 *outLength)
{
    u16 length, i;
    u8 checksum = 0;

    if (cursor + 3u > sKuro.framesSize)
        return FALSE;
    length = ReadU16(&sKuro.frames[cursor + 1]);
    if (cursor + 3u + length + 1u > sKuro.framesSize)
        return FALSE;
    for (i = 0; i < 3u + length; i++)
        checksum ^= sKuro.frames[cursor + i];
    if (checksum != sKuro.frames[cursor + 3 + length])
        return FALSE;
    *outLength = length;
    return TRUE;
}

static void QueueTxBody(u8 opcode, const u8 *body, u8 length)
{
    u32 i;

    sKuro.pendingTxOpcode = opcode;
    sKuro.pendingTxLength = length;
    for (i = 0; i < length; i++)
        sKuro.pendingTxBody[i] = body[i];
}

static void QueueTx(u8 opcode, u16 seq)
{
    u8 body[2];

    body[0] = seq & 0xFF;
    body[1] = seq >> 8;
    QueueTxBody(opcode, body, sizeof(body));
}

// Returns TRUE when nothing is left waiting for room in the TX ring.
static bool32 FlushPendingTx(void)
{
    if (sKuro.pendingTxOpcode == 0)
        return TRUE;
    if (!KuroLink_SendFrame(sKuro.pendingTxOpcode, sKuro.pendingTxBody, sKuro.pendingTxLength))
        return FALSE;
    sKuro.pendingTxOpcode = 0;
    return TRUE;
}

static bool32 ReadFixtureFrame(u8 *opcode, u16 *length)
{
    u16 i;

    if (sKuro.cursor >= sKuro.framesSize || !IsFrameValid(sKuro.cursor, length))
    {
        HaltWithError();
        return FALSE;
    }
    *opcode = sKuro.frames[sKuro.cursor];
    for (i = 0; i < *length; i++)
        sKuro.payload[i] = sKuro.frames[sKuro.cursor + 3 + i];
    sKuro.cursor += 3 + *length + 1;
    return TRUE;
}

// Returns TRUE when a frame is ready. Corrupt frames ask the relay to resend.
static bool32 ReadWireFrame(u8 *opcode, u16 *length)
{
    if (sKuro.hasStash)
    {
        sKuro.hasStash = FALSE;
        *opcode = sKuro.stashOpcode;
        *length = sKuro.stashLength;
        memcpy(sKuro.payload, sKuro.stash, sKuro.stashLength);
        return TRUE;
    }

    switch (KuroLink_ReadFrame(opcode, sKuro.payload, length))
    {
    case KURO_LINK_READ_OK:
        return TRUE;
    case KURO_LINK_READ_CORRUPT:
        QueueTx(KURO_OP_ERR_DESYNC, sKuro.lastSeq);
        return FALSE;
    default:
        return FALSE;
    }
}

static void ApplyTimerInfo(const u8 *payload)
{
    sKuro.timerSeconds = ReadU16(&payload[2]);
    sKuro.timerOn = payload[4];
    sKuro.timerFrames = 0;
}

// Returns TRUE when a new event was queued.
static bool32 FetchNextEvent(void)
{
    u16 length, seq;
    u8 opcode;
    const u8 *payload = sKuro.payload;

    if (!(sKuro.isWire ? ReadWireFrame(&opcode, &length) : ReadFixtureFrame(&opcode, &length)))
        return FALSE;

    if (opcode < KURO_FIRST_BATTLE_OPCODE)
    {
        KuroSession_HandleFrame(opcode, payload, length);
        return FALSE;
    }

    seq = ReadU16(payload);
    if (opcode == KURO_OP_RESYNC_BEGIN)
    {
        BeginResync();
        return FALSE;
    }
    if (opcode == KURO_OP_RESYNC_END)
    {
        EndResync();
        return TRUE;
    }

    // After a reattach the relay resends from the last ack, so some frames were already played.
    if (sKuro.isWire && (s16)(seq - sKuro.lastSeq) <= 0)
        return FALSE;
    sKuro.lastSeq = seq;

    sKuro.ackSeq = ReadU16(payload);
    sKuro.actionCount = 0;
    sKuro.actionIndex = 0;
    sKuro.subStep = 0;
    sKuro.needsAck = (opcode >= KURO_OP_SWITCH_IN && opcode <= KURO_OP_MESSAGE && !sKuro.resyncing);

    switch (opcode)
    {
    case KURO_OP_BATTLE_START:
        if (sKuro.isClient)
        {
            u8 *text = NextTextBuffer();

            StringCopyN(text, &payload[5], sizeof(sKuro.opponentName) - 1);
            text[sizeof(sKuro.opponentName) - 1] = EOS;
            StringAppend(text, sText_WantsToBattle);
            AddText(text);
        }
        StringCopyN(sKuro.opponentName, &payload[5], sizeof(sKuro.opponentName) - 1);
        sKuro.opponentName[sizeof(sKuro.opponentName) - 1] = EOS;
        break;
    case KURO_OP_TURN:
        sKuro.sawTurn = TRUE;
        break;
    case KURO_OP_REQUEST:
        memcpy(sKuro.request, payload, length);
        sKuro.haveRequest = TRUE;
        sKuro.requestAnswered = FALSE;
        break;
    case KURO_OP_TIMER_INFO:
        ApplyTimerInfo(payload);
        break;
    case KURO_OP_SWITCH_IN: QueueSwitchIn(payload, length); break;
    case KURO_OP_MOVE: QueueMove(payload); break;
    case KURO_OP_HP_CHANGE: QueueHpChange(payload); break;
    case KURO_OP_STATUS_SET: QueueStatus(payload); break;
    case KURO_OP_STAT_STAGE: QueueStatStage(payload); break;
    case KURO_OP_FAINT: QueueFaint(payload); break;
    case KURO_OP_EFFECTIVENESS: QueueEffectiveness(payload); break;
    case KURO_OP_FIELD: QueueField(payload); break;
    case KURO_OP_SIDE_COND: AddAction(KURO_ACT_SE, 0, SE_SHINY); break;
    case KURO_OP_ABILITY_SHOW:
        AddAction(KURO_ACT_ABILITY_POPUP, BattlerFromPosition(payload[2]), ReadU16(&payload[3]));
        break;
    case KURO_OP_MESSAGE: AddText(&payload[2]); break;
    case KURO_OP_BATTLE_END: QueueBattleEnd(payload); break;
    default:
        if (sKuro.isWire)
            QueueTx(KURO_OP_ERR_DESYNC, sKuro.lastSeq);
        else
            HaltWithError();
        return FALSE;
    }
    return TRUE;
}

// Each step returns TRUE once its action has fully finished.
static bool32 RunText(const struct KuroAction *action)
{
    switch (sKuro.subStep)
    {
    case 0:
        gBattle_BG0_X = 0;
        gBattle_BG0_Y = 0;
        StringCopy(gDisplayedStringBattle, action->text);
        BattlePutTextOnWindow(gDisplayedStringBattle, B_WIN_MSG);
        sKuro.subStep++;
        return FALSE;
    case 1:
        if (IsTextPrinterActiveOnWindow(B_WIN_MSG))
            return FALSE;
        sKuro.timer = KURO_TEXT_PAUSE_FRAMES;
        sKuro.subStep++;
        return FALSE;
    default:
        return --sKuro.timer == 0;
    }
}

static bool32 RunAbilityPopup(const struct KuroAction *action)
{
    switch (sKuro.subStep)
    {
    case 0:
        CreateAbilityPopUp(action->battler, (u16)action->value, FALSE);
        sKuro.timer = KURO_ABILITY_POPUP_FRAMES;
        sKuro.subStep++;
        return FALSE;
    case 1:
        if (--sKuro.timer != 0)
            return FALSE;
        DestroyAbilityPopUp(action->battler);
        sKuro.timer = KURO_ABILITY_POPUP_FRAMES / 2;
        sKuro.subStep++;
        return FALSE;
    default:
        return --sKuro.timer == 0;
    }
}

// Issues one controller command, then waits for the controllers to go idle.
static bool32 RunControllerAction(const struct KuroAction *action)
{
    enum BattlerId battler = action->battler;

    if (sKuro.subStep != 0)
        return gBattleControllerExecFlags == 0;

    switch (action->kind)
    {
    case KURO_ACT_MOVE_ANIM:
        gBattlerAttacker = battler;
        gBattlerTarget = action->target;
        BtlController_EmitMoveAnimation(battler, B_COMM_TO_CONTROLLER, (u16)action->value, 0, 0, 0, MAX_FRIENDSHIP, 1);
        break;
    case KURO_ACT_HIT:
        BtlController_EmitHitAnimation(battler, B_COMM_TO_CONTROLLER);
        break;
    case KURO_ACT_HP_BAR:
        BtlController_EmitHealthBarUpdate(battler, B_COMM_TO_CONTROLLER, action->value);
        break;
    case KURO_ACT_STATUS:
        BtlController_EmitStatusIconUpdate(battler, B_COMM_TO_CONTROLLER, gBattleMons[battler].status1);
        break;
    case KURO_ACT_STATUS_ANIM:
        BtlController_EmitStatusAnimation(battler, B_COMM_TO_CONTROLLER, FALSE, gBattleMons[battler].status1);
        break;
    case KURO_ACT_STAT_ANIM:
        BtlController_EmitBattleAnimation(battler, B_COMM_TO_CONTROLLER, B_ANIM_STATS_CHANGE, action->value);
        break;
    case KURO_ACT_FAINT_CRY:
        BtlController_EmitFaintingCry(battler, B_COMM_TO_CONTROLLER);
        break;
    case KURO_ACT_FAINT:
        BtlController_EmitFaintAnimation(battler, B_COMM_TO_CONTROLLER);
        sKuro.battlerActive[battler] = FALSE;
        break;
    case KURO_ACT_RETURN:
        BtlController_EmitReturnMonToBall(battler, B_COMM_TO_CONTROLLER, action->value != 0);
        sKuro.battlerActive[battler] = FALSE;
        break;
    case KURO_ACT_SWITCH_IN:
        ApplySwitchIn(battler, action->value);
        BtlController_EmitSwitchInAnim(battler, B_COMM_TO_CONTROLLER, action->value, FALSE, FALSE);
        break;
    case KURO_ACT_BATTLE_ANIM:
        BtlController_EmitBattleAnimation(battler, B_COMM_TO_CONTROLLER, action->value, 0);
        break;
    default:
        return TRUE;
    }
    MarkBattlerForControllerExec(battler);
    sKuro.subStep++;
    return FALSE;
}

static bool32 RunAction(const struct KuroAction *action)
{
    switch (action->kind)
    {
    case KURO_ACT_TEXT:
        return RunText(action);
    case KURO_ACT_ABILITY_POPUP:
        return RunAbilityPopup(action);
    case KURO_ACT_SET_HP:
        SetBattlerHp(action->battler, (u16)action->value);
        return TRUE;
    case KURO_ACT_SE:
        PlaySE(action->value);
        return TRUE;
    case KURO_ACT_FINISH:
        gKuroStatus = KURO_STATUS_ENDED;
        sKuro.state = sKuro.isClient ? KURO_STATE_LEAVING : KURO_STATE_HALTED;
        sKuro.timer = KURO_RESULT_FRAMES;
        return TRUE;
    default:
        return RunControllerAction(action);
    }
}

static const u8 sText_BattleMenu[] = _("Battle{CLEAR_TO 56}---\nPokémon{CLEAR_TO 56}Forfeit");
static const u8 sText_WaitingForOpponent[] = _("Waiting for opponent…");
static const u8 sText_Forfeit[] = _("Forfeit this battle?");
static const u8 sText_CantSwitch[] = _("You can't switch out now!");
static const u8 sText_NoPP[] = _("That move can't be used!");
static const u8 sText_Seconds[] = _("s ");
static const u8 sText_Tera[] = _("Tera");

static void ShowBattleText(const u8 *text)
{
    gBattle_BG0_X = 0;
    gBattle_BG0_Y = 0;
    StringCopy(gDisplayedStringBattle, text);
    BattlePutTextOnWindow(gDisplayedStringBattle, B_WIN_MSG);
}

static enum BattlerId PlayerBattler(void)
{
    return GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);
}

static void SetPhase(u8 phase)
{
    sKuro.phase = phase;
    gKuroUiScreen = phase;
}

// The request lists our party in Showdown's order, so party indexes in choices line up with it.
static void ApplyRequestParty(void)
{
    enum BattlerId battler = PlayerBattler();
    u32 count = min(sKuro.request[REQUEST_PARTY_COUNT], PARTY_SIZE);
    u32 i;

    ZeroPlayerPartyMons();
    for (i = 0; i < count; i++)
    {
        const u8 *member = &sKuro.request[REQUEST_PARTY + i * REQUEST_MEMBER_SIZE];
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        enum Species species = ReadU16(&member[0]);
        u16 hp = ReadU16(&member[2]);
        u16 maxHp = ReadU16(&member[4]);
        u16 item = ReadU16(&member[7]);
        u8 level = member[10];
        bool8 shiny = member[12];
        u32 status = (member[6] < ARRAY_COUNT(sKuroStatusFlags)) ? sKuroStatusFlags[member[6]] : 0;
        u8 nickname[POKEMON_NAME_LENGTH + 1];

        CreateMon(mon, species, level, PersonalityForGender(species, member[11]), OTID_STRUCT_PRESET(0));
        SetMonData(mon, MON_DATA_IS_SHINY, &shiny);
        SetMonData(mon, MON_DATA_LEVEL, &level);
        SetMonData(mon, MON_DATA_MAX_HP, &maxHp);
        SetMonData(mon, MON_DATA_HP, &hp);
        SetMonData(mon, MON_DATA_STATUS, &status);
        SetMonData(mon, MON_DATA_HELD_ITEM, &item);
        memcpy(nickname, &member[13], POKEMON_NAME_LENGTH);
        nickname[POKEMON_NAME_LENGTH] = EOS;
        SetMonData(mon, MON_DATA_NICKNAME, nickname);

        if (member[9])
            gBattlerPartyIndexes[battler] = i;
    }
    sKuro.partyCount[B_SIDE_PLAYER] = count;
    gPartiesCount[B_TRAINER_PLAYER] = count;
    BufferBattlePartyCurrentOrderBySide(battler, 0);
}

static void OpenActionMenu(void)
{
    enum BattlerId battler = PlayerBattler();

    gBattleResources->bufferB[battler][1] = 0xFF;
    BtlController_EmitChooseAction(battler, B_COMM_TO_CONTROLLER, B_ACTION_USE_MOVE, 0);
    MarkBattlerForControllerExec(battler);
    sKuro.labelTimer = 2;
    SetPhase(KURO_UI_ACTION);
}

static void OpenMoveMenu(void)
{
    enum BattlerId battler = PlayerBattler();
    struct ChooseMoveStruct info;
    u32 i;

    memset(&info, 0, sizeof(info));
    info.species = gBattleMons[battler].species;
    info.monTypes[0] = GetSpeciesType(info.species, 0);
    info.monTypes[1] = GetSpeciesType(info.species, 1);
    info.monTypes[2] = TYPE_MYSTERY;
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        const u8 *move = &sKuro.request[REQUEST_MOVES + i * REQUEST_MOVE_SIZE];

        info.moves[i] = ReadU16(move);
        info.currentPP[i] = move[2];
        info.maxPP[i] = move[3];
        gBattleMons[battler].moves[i] = info.moves[i];
        gBattleMons[battler].pp[i] = info.currentPP[i];
    }
    BtlController_EmitChooseMove(battler, B_COMM_TO_CONTROLLER, FALSE, FALSE, &info);
    MarkBattlerForControllerExec(battler);
    SetPhase(KURO_UI_MOVE);
}

static void OpenPartyMenu(bool32 forced)
{
    enum BattlerId battler = PlayerBattler();
    u8 action = forced ? PARTY_ACTION_SEND_OUT : PARTY_ACTION_CHOOSE_MON;

    // Revival Blessing arrives as a forced switch whose only legal picks are fainted.
    if (sKuro.request[REQUEST_FLAGS] & KURO_REQUEST_REVIVING)
        action = PARTY_ACTION_CHOOSE_FAINTED_MON;
    gSelectedMonPartyId = PARTY_SIZE;
    gBattleResources->bufferB[battler][1] = PARTY_SIZE;
    BtlController_EmitChoosePokemon(battler, B_COMM_TO_CONTROLLER, action,
                                    PARTY_SIZE, ABILITY_NONE, 0, gBattleStruct->battlerPartyOrders[battler]);
    MarkBattlerForControllerExec(battler);
    SetPhase(KURO_UI_PARTY);
}

static void OpenForfeitConfirm(void)
{
    enum BattlerId battler = PlayerBattler();

    ShowBattleText(sText_Forfeit);
    gBattleResources->bufferB[battler][1] = 0xFF;
    sKuro.forfeitPromptPending = TRUE; // The box's colour codes would corrupt a message still printing.
    SetPhase(KURO_UI_FORFEIT);
}

static void OpenDecision(void)
{
    sKuro.sawTurn = FALSE;
    sKuro.reopen = FALSE;
    sKuro.teraSelected = FALSE;
    sKuro.forfeitPromptPending = FALSE;
    KuroLink_SetRomFlags(KURO_ROM_FLAG_BOOTED | KURO_ROM_FLAG_IN_BATTLE | KURO_ROM_FLAG_AWAITING_CHOICE);
    ApplyRequestParty();
    if (sKuro.request[REQUEST_FLAGS] & KURO_REQUEST_FORCE_SWITCH)
        OpenPartyMenu(TRUE);
    else
        OpenActionMenu();
}

static void SendChoice(u8 opcode, const u8 *body, u8 length)
{
    enum BattlerId battler = PlayerBattler();

    // The choice menu starts the idle bounce; a faint slide never finishes while it runs.
    BtlController_EmitEndBounceEffect(battler, B_COMM_TO_CONTROLLER);
    MarkBattlerForControllerExec(battler);

    QueueTxBody(opcode, body, length);
    sKuro.requestAnswered = TRUE;
    KuroLink_SetRomFlags(KURO_ROM_FLAG_BOOTED | KURO_ROM_FLAG_IN_BATTLE);
    ShowBattleText(sText_WaitingForOpponent);
    SetPhase(KURO_UI_WAITING);
}

static bool32 HasMoveToShow(void)
{
    u32 i;

    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        if (ReadU16(&sKuro.request[REQUEST_MOVES + i * REQUEST_MOVE_SIZE]) != MOVE_NONE)
            return TRUE;
    }
    return FALSE;
}

// Handles the player's input once the open menu's controller has gone idle.
static void RunDecision(void)
{
    enum BattlerId battler = PlayerBattler();
    const u8 *reply = gBattleResources->bufferB[battler];
    u8 chosenMon;
    u8 body[4];

    if (sKuro.labelTimer != 0 && --sKuro.labelTimer == 0)
        BattlePutTextOnWindow(sText_BattleMenu, B_WIN_ACTION_MENU);
    if (gBattleControllerExecFlags != 0)
        return;

    if (sKuro.forfeitPromptPending)
    {
        if (!IsTextPrinterActiveOnWindow(B_WIN_MSG))
        {
            sKuro.forfeitPromptPending = FALSE;
            BtlController_EmitYesNoBox(battler, B_COMM_TO_CONTROLLER);
            MarkBattlerForControllerExec(battler);
        }
        return;
    }

    switch (sKuro.phase)
    {
    case KURO_UI_ACTION:
        switch (reply[1])
        {
        case B_ACTION_USE_MOVE:
            if (HasMoveToShow())
            {
                OpenMoveMenu();
            }
            else
            {
                // Forced moves with no expansion match, like Recharge, have nothing to list.
                body[0] = 0;
                body[1] = 1;
                body[2] = 0;
                body[3] = 0;
                SendChoice(KURO_OP_CHOOSE_MOVE, body, 4);
            }
            break;
        case B_ACTION_SWITCH:
            if (sKuro.request[REQUEST_FLAGS] & KURO_REQUEST_TRAPPED)
            {
                ShowBattleText(sText_CantSwitch);
                OpenActionMenu();
            }
            else
            {
                OpenPartyMenu(FALSE);
            }
            break;
        case B_ACTION_RUN:
            OpenForfeitConfirm();
            break;
        default:
            OpenActionMenu();
            break;
        }
        break;
    case KURO_UI_MOVE:
        if ((reply[2] | (reply[3] << 8)) == 0xFFFF)
        {
            OpenActionMenu();
        }
        else
        {
            u8 cursor = reply[2] & ~RET_GIMMICK;

            if (sKuro.request[REQUEST_MOVES + cursor * REQUEST_MOVE_SIZE + 5])
            {
                ShowBattleText(sText_NoPP);
                OpenMoveMenu();
                break;
            }
            body[0] = 0;
            body[1] = cursor + 1;
            body[2] = 0;
            body[3] = sKuro.teraSelected ? KURO_CHOOSE_FLAG_TERA : 0;
            SendChoice(KURO_OP_CHOOSE_MOVE, body, 4);
        }
        break;
    case KURO_UI_PARTY:
        // The fainted-pick menu leaves its answer in gSelectedMonPartyId and always replies "cancelled".
        chosenMon = (sKuro.request[REQUEST_FLAGS] & KURO_REQUEST_REVIVING) ? gSelectedMonPartyId : reply[1];
        if (chosenMon >= PARTY_SIZE)
        {
            if (sKuro.request[REQUEST_FLAGS] & KURO_REQUEST_FORCE_SWITCH)
                OpenPartyMenu(TRUE);
            else
                OpenActionMenu();
        }
        else
        {
            body[0] = 0;
            body[1] = chosenMon + 1;
            SendChoice(KURO_OP_CHOOSE_SWITCH, body, 2);
        }
        break;
    case KURO_UI_FORFEIT:
        if (reply[1] == B_ACTION_NOTHING_FAINTED)
            SendChoice(KURO_OP_FORFEIT, NULL, 0);
        else
            OpenActionMenu();
        break;
    default:
        break;
    }
}

static void HideHud(void)
{
    if (!sKuro.hudShown)
        return;
    sKuro.hudShown = FALSE;
    ClearWindowTilemap(B_WIN_LEVEL_UP_BANNER);
    CopyBgTilemapBufferToVram(2);
}

// Timer and Tera indicators. They only show while the player is deciding.
static void UpdateHud(void)
{
    u8 *end = sKuro.hudText;
    bool32 flashOff;

    if (sKuro.timerOn && sKuro.timerSeconds > 0 && ++sKuro.timerFrames >= KURO_FRAMES_PER_SECOND)
    {
        sKuro.timerFrames = 0;
        sKuro.timerSeconds--;
    }

    flashOff = sKuro.timerOn && sKuro.timerSeconds < KURO_TIMER_WARNING_SECONDS && sKuro.timerFrames >= KURO_FRAMES_PER_SECOND / 2;
    if (sKuro.timerOn && !flashOff)
    {
        end = ConvertIntToDecimalStringN(end, sKuro.timerSeconds, STR_CONV_MODE_LEFT_ALIGN, 3);
        end = StringCopy(end, sText_Seconds);
    }
    if (sKuro.teraSelected)
        end = StringCopy(end, sText_Tera);
    *end = EOS;

    if (sKuro.hudText[0] == EOS)
    {
        HideHud();
        return;
    }
    if (sKuro.timerFrames == 0 || flashOff || !sKuro.hudShown)
    {
        BattlePutTextOnWindow(sKuro.hudText, B_WIN_LEVEL_UP_BANNER);
        sKuro.hudShown = TRUE;
    }
}

static void RunMenuExtras(void)
{
    u8 opcode;
    u16 length;

    if (KuroLink_PeekOpcode() == KURO_OP_TIMER_INFO && KuroLink_ReadFrame(&opcode, sKuro.payload, &length) == KURO_LINK_READ_OK)
        ApplyTimerInfo(sKuro.payload);

    if (sKuro.phase == KURO_UI_MOVE && JOY_NEW(SELECT_BUTTON) && (sKuro.request[REQUEST_FLAGS] & KURO_REQUEST_CAN_TERA))
    {
        sKuro.teraSelected = !sKuro.teraSelected;
        PlaySE(SE_SELECT);
        sKuro.hudShown = FALSE;
    }
    if (sKuro.phase == KURO_UI_ACTION && JOY_NEW(START_BUTTON))
    {
        u8 enable = !sKuro.timerOn;

        QueueTxBody(KURO_OP_TIMER, &enable, 1);
        PlaySE(SE_SELECT);
    }
    UpdateHud();
}

static bool32 IsMenuOpen(void)
{
    return sKuro.phase != KURO_UI_NONE && sKuro.phase != KURO_UI_WAITING;
}

static bool32 IsDecisionReady(void)
{
    return sKuro.isClient && sKuro.phase == KURO_UI_NONE && sKuro.haveRequest && !sKuro.requestAnswered
        && !(sKuro.request[REQUEST_FLAGS] & KURO_REQUEST_WAIT)
        && sKuro.settle >= KURO_SETTLE_FRAMES
        && (sKuro.sawTurn || sKuro.reopen || (sKuro.request[REQUEST_FLAGS] & KURO_REQUEST_FORCE_SWITCH));
}

static void RunIdle(void)
{
    if (!FlushPendingTx())
        return;

    if (IsMenuOpen())
    {
        RunMenuExtras();
        RunDecision();
        return;
    }
    HideHud();

    if (sKuro.phase == KURO_UI_WAITING && JOY_NEW(B_BUTTON))
    {
        QueueTxBody(KURO_OP_UNDO_CHOICE, NULL, 0);
        sKuro.requestAnswered = FALSE;
        sKuro.reopen = TRUE;
        sKuro.settle = KURO_SETTLE_FRAMES;
        SetPhase(KURO_UI_NONE);
        return;
    }

    if (FetchNextEvent())
    {
        sKuro.settle = 0;
        if (sKuro.actionCount != 0 && sKuro.phase == KURO_UI_WAITING)
            SetPhase(KURO_UI_NONE);
        return;
    }

    if (sKuro.settle < KURO_SETTLE_FRAMES)
        sKuro.settle++;
    if (IsDecisionReady())
        OpenDecision();
}

void KuroPlayback_Main(void)
{
    enum BattlerId battler = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);

    switch (sKuro.state)
    {
    case KURO_STATE_INTRO:
        BtlController_EmitIntroSlide(battler, B_COMM_TO_CONTROLLER, gBattleEnvironment);
        MarkBattlerForControllerExec(battler);
        sKuro.state = KURO_STATE_WAIT_INTRO;
        break;
    case KURO_STATE_WAIT_INTRO:
        if (gBattleControllerExecFlags == 0)
            sKuro.state = KURO_STATE_RUN;
        break;
    case KURO_STATE_RUN:
        if (sKuro.actionIndex >= sKuro.actionCount)
        {
            if (sKuro.needsAck)
            {
                gKuroAckSeq = sKuro.ackSeq;
                sKuro.needsAck = FALSE;
                if (sKuro.isWire)
                    QueueTx(KURO_OP_ANIM_DONE, sKuro.ackSeq);
            }
            RunIdle();
        }
        else if (RunAction(&sKuro.actions[sKuro.actionIndex]))
        {
            sKuro.actionIndex++;
            sKuro.subStep = 0;
        }
        break;
    case KURO_STATE_LEAVING:
        if (--sKuro.timer == 0 || JOY_NEW(A_BUTTON))
        {
            BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
            sKuro.state = KURO_STATE_FADING;
        }
        break;
    case KURO_STATE_FADING:
        if (!gPaletteFade.active)
        {
            m4aMPlayAllStop();
            KuroFreeBattleData();
            SetMainCallback2(CB2_KuroMenuStart);
        }
        break;
    default:
        break;
    }
}

// Echoes every frame unchanged, without interpreting it. Used only by the M1 bridge test.
static void CB2_KuroLoopback(void)
{
    static EWRAM_DATA u8 sEcho[KURO_MAX_PAYLOAD + 1] = {0};
    static EWRAM_DATA u8 sEchoOpcode = 0;
    static EWRAM_DATA u16 sEchoLength = 0;
    static EWRAM_DATA bool8 sEchoPending = FALSE;

    for (;;)
    {
        if (sEchoPending)
        {
            if (!KuroLink_SendFrame(sEchoOpcode, sEcho, sEchoLength))
                return;
            sEchoPending = FALSE;
        }
        switch (KuroLink_ReadFrame(&sEchoOpcode, sEcho, &sEchoLength))
        {
        case KURO_LINK_READ_OK:
            sEchoPending = TRUE;
            break;
        case KURO_LINK_READ_CORRUPT:
            gKuroLoopbackCorrupt++;
            return;
        default:
            return;
        }
    }
}

static void StartBattlePlayback(u32 mode)
{
    // Boot seeds the RNG from the RTC; fixed seeds keep presentation identical on every run.
    SeedRng(KURO_RNG_SEED);
    SeedRng2(KURO_RNG_SEED);

    memset(&sKuro, 0, sizeof(sKuro));
    if (mode == KURO_BOOT_WIRE_PLAYBACK || mode == KURO_BOOT_CLIENT)
    {
        sKuro.isWire = TRUE;
        sKuro.isClient = (mode == KURO_BOOT_CLIENT);
    }
    else
    {
        sKuro.frames = sKuroFixtures[mode].frames;
        sKuro.framesSize = sKuroFixtures[mode].size;
    }
    gKuroAckSeq = 0;
    gKuroStatus = KURO_STATUS_RUNNING;
    KuroLink_SetRomFlags(KURO_ROM_FLAG_BOOTED | KURO_ROM_FLAG_IN_BATTLE);

    ZeroPlayerPartyMons();
    ZeroEnemyPartyMons();
    gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_KURO_REMOTE;
    TRAINER_BATTLE_PARAM.opponentA = TRAINER_NONE;
    SetMainCallback2(CB2_InitBattle);
}

void KuroPlayback_StartClientBattle(u8 opcode, const u8 *payload, u16 length)
{
    FreeAllWindowBuffers();
    StartBattlePlayback(KURO_BOOT_CLIENT);
    sKuro.hasStash = TRUE;
    sKuro.stashOpcode = opcode;
    sKuro.stashLength = length;
    memcpy(sKuro.stash, payload, length);
}

void CB2_KuroBoot(void)
{
    u32 mode = gKuroBootMode;

    KuroLink_Init();
    KuroSession_Reset();
    if (mode == KURO_BOOT_LOOPBACK)
        SetMainCallback2(CB2_KuroLoopback);
    else if (mode == KURO_BOOT_CLIENT)
        SetMainCallback2(CB2_KuroMenuStart);
    else
        StartBattlePlayback(mode <= KURO_BOOT_WIRE_PLAYBACK ? mode : KURO_BOOT_FIXTURE_0);
}
