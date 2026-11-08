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
#include "kuro_playback.h"
#include "main.h"
#include "move.h"
#include "pokemon.h"
#include "random.h"
#include "sound.h"
#include "string_util.h"
#include "text.h"
#include "data/kuro_m0_frames.h"

enum KuroOpcode
{
    KURO_OP_BATTLE_START = 0x40,
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
    KURO_OP_BATTLE_END = 0x61,
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
};

#define KURO_MAX_ACTIONS 8
#define KURO_TEXT_PAUSE_FRAMES 40
#define KURO_ABILITY_POPUP_FRAMES 60
#define KURO_PERSONALITY_SEARCH_LIMIT 512
#define KURO_RNG_SEED 0x4B55524F

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
    bool8 needsAck;
    bool8 battlerActive[MAX_BATTLERS_COUNT];
    u8 partyCount[NUM_BATTLE_SIDES];
    u8 opponentName[PLAYER_NAME_LENGTH + 8];
    struct KuroAction actions[KURO_MAX_ACTIONS];
    u8 text[KURO_MAX_ACTIONS][80];
};

EWRAM_DATA volatile u16 gKuroAckSeq = 0;
EWRAM_DATA volatile u8 gKuroStatus = KURO_STATUS_RUNNING;
static EWRAM_DATA struct KuroPlayback sKuro = {0};

static const u8 sText_Foe[] = _("Foe ");
static const u8 sText_GoSpace[] = _("Go! ");
static const u8 sText_SentOut[] = _(" sent out\n");
static const u8 sText_Exclaim[] = _("!");
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
    StringAppend(dst, GetSpeciesName(gBattleMons[battler].species));
}

static struct KuroAction *AddAction(u8 kind, enum BattlerId battler, s16 value)
{
    struct KuroAction *action;

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

static void QueueSwitchIn(const u8 *payload)
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
        StringAppend(text, GetSpeciesName(species));
        StringAppend(text, sText_Exclaim);
    }
    else
    {
        StringCopy(text, sKuro.opponentName);
        StringAppend(text, sText_SentOut);
        StringAppend(text, GetSpeciesName(species));
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

    if (condition != 0 && condition < ARRAY_COUNT(sKuroWeatherAnims) && payload[3] != 1)
        AddAction(KURO_ACT_BATTLE_ANIM, GetBattlerAtPosition(B_POSITION_PLAYER_LEFT), sKuroWeatherAnims[condition]);
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

    if (cursor + 3u > sizeof(sKuroM0Frames))
        return FALSE;
    length = ReadU16(&sKuroM0Frames[cursor + 1]);
    if (cursor + 3u + length + 1u > sizeof(sKuroM0Frames))
        return FALSE;
    for (i = 0; i < 3u + length; i++)
        checksum ^= sKuroM0Frames[cursor + i];
    if (checksum != sKuroM0Frames[cursor + 3 + length])
        return FALSE;
    *outLength = length;
    return TRUE;
}

// Returns FALSE when the stream is exhausted or corrupt.
static bool32 FetchNextEvent(void)
{
    u16 length;
    u8 opcode;
    const u8 *payload;

    if (sKuro.cursor >= sizeof(sKuroM0Frames))
    {
        HaltWithError();
        return FALSE;
    }
    if (!IsFrameValid(sKuro.cursor, &length))
    {
        HaltWithError();
        return FALSE;
    }

    opcode = sKuroM0Frames[sKuro.cursor];
    payload = &sKuroM0Frames[sKuro.cursor + 3];
    sKuro.cursor += 3 + length + 1;
    sKuro.ackSeq = ReadU16(payload);
    sKuro.actionCount = 0;
    sKuro.actionIndex = 0;
    sKuro.subStep = 0;
    sKuro.needsAck = (opcode >= KURO_OP_SWITCH_IN);

    switch (opcode)
    {
    case KURO_OP_BATTLE_START:
        StringCopyN(sKuro.opponentName, &payload[5], sizeof(sKuro.opponentName) - 1);
        sKuro.opponentName[sizeof(sKuro.opponentName) - 1] = EOS;
        break;
    case KURO_OP_TURN:
        break;
    case KURO_OP_SWITCH_IN: QueueSwitchIn(payload); break;
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
        BtlController_EmitReturnMonToBall(battler, B_COMM_TO_CONTROLLER, FALSE);
        sKuro.battlerActive[battler] = FALSE;
        break;
    case KURO_ACT_SWITCH_IN:
        gBattlerPartyIndexes[battler] = action->value;
        PokemonToBattleMon(GetBattlerMon(battler), &gBattleMons[battler]);
        BtlController_EmitSwitchInAnim(battler, B_COMM_TO_CONTROLLER, action->value, FALSE, FALSE);
        sKuro.battlerActive[battler] = TRUE;
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
        sKuro.state = KURO_STATE_HALTED;
        return TRUE;
    default:
        return RunControllerAction(action);
    }
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
            }
            FetchNextEvent();
        }
        else if (RunAction(&sKuro.actions[sKuro.actionIndex]))
        {
            sKuro.actionIndex++;
            sKuro.subStep = 0;
        }
        break;
    default:
        break;
    }
}

void CB2_KuroM0Start(void)
{
    // Boot seeds the RNG from the RTC; fixed seeds keep presentation identical on every run.
    SeedRng(KURO_RNG_SEED);
    SeedRng2(KURO_RNG_SEED);

    memset(&sKuro, 0, sizeof(sKuro));
    gKuroAckSeq = 0;
    gKuroStatus = KURO_STATUS_RUNNING;

    ZeroPlayerPartyMons();
    ZeroEnemyPartyMons();
    gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_KURO_REMOTE;
    TRAINER_BATTLE_PARAM.opponentA = TRAINER_NONE;
    SetMainCallback2(CB2_InitBattle);
}
