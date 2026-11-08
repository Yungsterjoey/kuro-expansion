// Team builder: team list, members, per-field editors and pickers. Pickers are filtered by expansion
// data; the server's validator remains the final judge of legality.
#include <stdlib.h>
#include "global.h"
#include "constants/abilities.h"
#include "constants/characters.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/songs.h"
#include "item.h"
#include "kuro_builder.h"
#include "kuro_link.h"
#include "kuro_playback.h"
#include "kuro_session.h"
#include "kuro_teams.h"
#include "main.h"
#include "malloc.h"
#include "move.h"
#include "naming_screen.h"
#include "pokemon.h"
#include "sound.h"
#include "string_util.h"
#include "text.h"
#include "window.h"
#include "battle_main.h"

#define ROWS 7
#define LINE_HEIGHT 16
#define EV_STEP 4
#define EV_STEP_LARGE 252
#define PAGE_SIZE 10
#define PICK_CAPACITY 1600

void CB2_KuroMenuStart(void);

enum BuilderScreen
{
    BS_CLOSED,
    BS_TEAMS,
    BS_TEAM_MENU,
    BS_MEMBERS,
    BS_MON,
    BS_PICK,
    BS_EVS,
    BS_IVS,
    BS_STATS,
    BS_CONFIRM_DELETE,
    BS_MESSAGE,
    BS_WAIT_EXPORT,
    BS_WAIT_IMPORT,
};

enum TeamMenuRow { TM_EDIT, TM_RENAME, TM_FORMAT, TM_DUPLICATE, TM_MOVE_UP, TM_DELETE, TM_EXPORT, TM_IMPORT, TM_BACK, TM_COUNT };

enum MonRow
{
    MR_SPECIES, MR_NICKNAME, MR_LEVEL, MR_GENDER, MR_SHINY, MR_ITEM, MR_ABILITY,
    MR_MOVE1, MR_MOVE2, MR_MOVE3, MR_MOVE4, MR_NATURE, MR_EVS, MR_IVS, MR_TERA, MR_STATS, MR_REMOVE, MR_BACK, MR_COUNT
};

enum PickKind { PICK_SPECIES, PICK_ITEM, PICK_ABILITY, PICK_MOVE, PICK_NATURE, PICK_TERA };

static EWRAM_DATA u8 sBuilder = BS_CLOSED;
static EWRAM_DATA u8 sReturnTo = BS_CLOSED;
static EWRAM_DATA u8 sTeam = 0;
static EWRAM_DATA u8 sMon = 0;
static EWRAM_DATA u16 sCursor = 0;
static EWRAM_DATA u16 sScroll = 0;
static EWRAM_DATA bool8 sDirty = FALSE;
static EWRAM_DATA u8 sPickKind = 0;
static EWRAM_DATA u8 sPickMoveSlot = 0;
static EWRAM_DATA bool8 sPickIsNewMon = FALSE;
static EWRAM_DATA bool8 sPickAllMoves = FALSE;
static EWRAM_DATA u16 *sPickIds = NULL;
static EWRAM_DATA u16 sPickCount = 0;
static EWRAM_DATA u8 sNameBuffer[POKEMON_NAME_BUFFER_SIZE] = {0};
static EWRAM_DATA bool8 sNamingTeam = FALSE;
static EWRAM_DATA const u8 *sMessage = NULL;
EWRAM_DATA volatile u16 gKuroPickerValue = 0;

static const u8 sText_Teams[] = _("Teams");
static const u8 sText_Empty[] = _("(empty)");
static const u8 sText_Cursor[] = _("{RIGHT_ARROW} ");
static const u8 sText_Indent[] = _("  ");
static const u8 sText_None[] = _("None");
static const u8 sText_Done[] = _("Done");
static const u8 sText_Unchecked[] = _("Unchecked");
static const u8 sText_Passed[] = _("Legal");
static const u8 sText_Failed[] = _("Rejected");
static const u8 sText_Space[] = _("  ");
static const u8 sText_Slash[] = _("/");
static const u8 sText_ConfirmDelete[] = _("Delete this team?\nA: delete  B: keep");
static const u8 sText_NoEmptySlot[] = _("No empty slot to copy into.");
static const u8 sText_EmptyTeam[] = _("This team is empty.");
static const u8 sText_Saved[] = _("Team received and saved.");
static const u8 sText_WaitExport[] = _("Exporting…");
static const u8 sText_WaitImport[] = _("Run kuro-relay import on\nyour PC.  B: cancel");
static const u8 sText_Exported[] = _("Exported to:\n");
static const u8 sText_ExportFailed[] = _("Export failed:\n");
static const u8 sText_PressA[] = _("\nPress A.");
static const u8 sText_SaveFailed[] = _("Could not save.");
static const u8 sText_ExpLine[] = _("Total EVs ");
static const u8 sText_Of510[] = _("/510");

static const u8 sTM0[] = _("Edit members"), sTM1[] = _("Rename"), sTM2[] = _("Format: "), sTM3[] = _("Duplicate"), sTM4[] = _("Move up");
static const u8 sTM5[] = _("Delete"), sTM6[] = _("Export to file"), sTM7[] = _("Import from PC"), sTM8[] = _("Back");
static const u8 *const sTeamMenuLabels[TM_COUNT] = {sTM0, sTM1, sTM2, sTM3, sTM4, sTM5, sTM6, sTM7, sTM8};

static const u8 sMR0[] = _("Species: "), sMR1[] = _("Nickname: "), sMR2[] = _("Level "), sMR3[] = _("Gender: "), sMR4[] = _("Shiny: ");
static const u8 sMR5[] = _("Item: "), sMR6[] = _("Ability: "), sMR7[] = _("Move: "), sMR11[] = _("Nature: ");
static const u8 sMR12[] = _("EVs"), sMR13[] = _("IVs"), sMR14[] = _("Tera: "), sMR15[] = _("Stats"), sMR16[] = _("Remove"), sMR17[] = _("Back");
static const u8 *const sMonLabels[MR_COUNT] = {sMR0, sMR1, sMR2, sMR3, sMR4, sMR5, sMR6, sMR7, sMR7, sMR7, sMR7, sMR11, sMR12, sMR13, sMR14, sMR15, sMR16, sMR17};

static const u8 sGenderAny[] = _("Any"), sGenderM[] = _("Male"), sGenderF[] = _("Female");
static const u8 sYes[] = _("Yes"), sNo[] = _("No");
static const u8 *const sGenderNames[3] = {sGenderAny, sGenderM, sGenderF};

static const u8 sStatHp[] = _("HP "), sStatAtk[] = _("Atk"), sStatDef[] = _("Def"), sStatSpe[] = _("Spe"), sStatSpa[] = _("SpA"), sStatSpd[] = _("SpD");
static const u8 *const sStatNames[NUM_STATS] = {sStatHp, sStatAtk, sStatDef, sStatSpe, sStatSpa, sStatSpd};
static const u8 sPick0[] = _("Choose a species"), sPick1[] = _("Choose an item"), sPick2[] = _("Choose an ability");
static const u8 sPick3[] = _("Moves it learns  SELECT: all"), sPick3All[] = _("All moves  SELECT: learnset"), sPick4[] = _("Choose a nature"), sPick5[] = _("Choose a Tera type");
static const u8 *const sPickTitles[] = {sPick0, sPick1, sPick2, sPick3, sPick4, sPick5};
static const u8 sText_EvTitle[] = _("EVs  Left/Right 4, L/R max or 0");
static const u8 sText_IvTitle[] = _("IVs  Left/Right 1");

static struct KuroTeam *CurrentTeam(void) { return &gKuroTeams[sTeam]; }
static struct KuroTeamMon *CurrentMon(void) { return &CurrentTeam()->mons[sMon]; }

static void GoTo(u8 screen)
{
    sBuilder = screen;
    sCursor = 0;
    sScroll = 0;
    sDirty = TRUE;
}

static void ShowMessage(const u8 *text, u8 returnTo)
{
    sMessage = text;
    sReturnTo = returnTo;
    GoTo(BS_MESSAGE);
}

static void FreePicker(void)
{
    TRY_FREE_AND_SET_NULL(sPickIds);
    sPickCount = 0;
}

static void Pick_Add(u16 id)
{
    if (sPickCount < PICK_CAPACITY)
        sPickIds[sPickCount++] = id;
}

static void AddMovesFrom(u16 species, u8 *seen)
{
    const struct LevelUpMove *levelUp = GetSpeciesLevelUpLearnset(species);
    const u16 *lists[2] = {GetSpeciesTeachableLearnset(species), GetSpeciesEggMoves(species)};
    u32 i, l;

    #define ADD_MOVE(m) do { u16 mv = (m); if (mv != MOVE_NONE && mv < MOVES_COUNT && !(seen[mv / 8] & (1 << (mv % 8)))) { seen[mv / 8] |= 1 << (mv % 8); Pick_Add(mv); } } while (0)
    if (levelUp != NULL)
        for (i = 0; levelUp[i].move != LEVEL_UP_MOVE_END; i++)
            ADD_MOVE(levelUp[i].move);
    for (l = 0; l < ARRAY_COUNT(lists); l++)
        if (lists[l] != NULL)
            for (i = 0; lists[l][i] != MOVE_UNAVAILABLE; i++)
                ADD_MOVE(lists[l][i]);
    #undef ADD_MOVE
}

// A move learnable by a pre-evolution counts too; the server has the final word.
static u16 FindPreEvolution(u16 species)
{
    u32 s, i;

    for (s = 1; s < NUM_SPECIES; s++)
    {
        const struct Evolution *evolutions = IsSpeciesEnabled(s) ? GetSpeciesEvolutions(s) : NULL;

        if (evolutions == NULL)
            continue;
        for (i = 0; evolutions[i].method != EVOLUTIONS_END; i++)
            if (evolutions[i].targetSpecies == species)
                return s;
    }
    return SPECIES_NONE;
}

static int CompareMoves(const void *a, const void *b)
{
    return StringCompare(GetMoveName(*(const u16 *)a), GetMoveName(*(const u16 *)b));
}

static void BuildMovePicker(u16 species)
{
    u8 *seen = AllocZeroed(MOVES_COUNT / 8 + 1);
    u16 base = GET_BASE_SPECIES_ID(species), current = species;
    u32 depth;

    AddMovesFrom(species, seen);
    if (base != species)
        AddMovesFrom(base, seen);
    current = base;
    for (depth = 0; depth < 2; depth++)
    {
        current = FindPreEvolution(current);
        if (current == SPECIES_NONE)
            break;
        AddMovesFrom(current, seen);
    }
    Free(seen);
    qsort(&sPickIds[1], sPickCount - 1, sizeof(u16), CompareMoves);
}

static void OpenPicker(u8 kind, u8 moveSlot, bool32 newMon)
{
    u32 i;
    u16 species = newMon ? SPECIES_NONE : CurrentMon()->species;

    sPickKind = kind;
    sPickMoveSlot = moveSlot;
    sPickIsNewMon = newMon;
    FreePicker();
    sPickIds = AllocZeroed(PICK_CAPACITY * sizeof(u16));
    sPickCount = 0;
    switch (kind)
    {
    case PICK_SPECIES:
        for (i = 1; i < NUM_SPECIES; i++)
            if (IsSpeciesEnabled(i))
                Pick_Add(i);
        break;
    case PICK_ITEM:
        Pick_Add(ITEM_NONE);
        for (i = 1; i < ITEMS_COUNT; i++)
            if (GetItemName(i)[0] != CHAR_QUESTION_MARK)
                Pick_Add(i);
        break;
    case PICK_ABILITY:
        for (i = 0; i < NUM_ABILITY_SLOTS; i++)
        {
            u16 ability = GetSpeciesAbility(species, i);
            u32 j;
            bool32 duplicate = (ability == ABILITY_NONE);

            for (j = 0; j < sPickCount; j++)
                duplicate |= (sPickIds[j] == ability);
            if (!duplicate)
                Pick_Add(ability);
        }
        break;
    case PICK_MOVE:
        Pick_Add(MOVE_NONE);
        if (sPickAllMoves)
        {
            for (i = 1; i < MOVES_COUNT; i++)
                Pick_Add(i);
            qsort(&sPickIds[1], sPickCount - 1, sizeof(u16), CompareMoves);
        }
        else
        {
            BuildMovePicker(species);
        }
        break;
    case PICK_NATURE:
        for (i = 0; i < NUM_NATURES; i++)
            Pick_Add(i);
        break;
    case PICK_TERA:
        for (i = 1; i < NUMBER_OF_MON_TYPES; i++)
            if (i != TYPE_MYSTERY)
                Pick_Add(i);
        break;
    }
    GoTo(BS_PICK);
}

static const u8 *PickName(u16 id)
{
    switch (sPickKind)
    {
    case PICK_SPECIES: return GetSpeciesName(id);
    case PICK_ITEM: return id == ITEM_NONE ? sText_None : GetItemName(id);
    case PICK_ABILITY: return gAbilitiesInfo[id].name;
    case PICK_MOVE: return id == MOVE_NONE ? sText_None : GetMoveName(id);
    case PICK_NATURE: return gNaturesInfo[id].name;
    default: return gTypesInfo[id].name;
    }
}

static void RowText(u8 *out, u32 row)
{
    const struct KuroTeam *team = CurrentTeam();
    const struct KuroTeamMon *mon = CurrentMon();
    u32 i, total;

    out[0] = EOS;
    switch (sBuilder)
    {
    case BS_TEAMS:
        team = &gKuroTeams[row];
        out = StringCopy(out, team->name);
        out = StringAppend(out, sText_Space);
        if (team->count == 0)
        {
            StringAppend(out, sText_Empty);
            break;
        }
        ConvertIntToDecimalStringN(out, team->count, STR_CONV_MODE_LEFT_ALIGN, 1);
        out = StringAppend(out, sText_Slash);
        ConvertIntToDecimalStringN(out, KURO_TEAM_SIZE, STR_CONV_MODE_LEFT_ALIGN, 1);
        out = StringAppend(out, sText_Space);
        StringAppend(out, team->badge == KURO_BADGE_PASSED ? sText_Passed : (team->badge == KURO_BADGE_FAILED ? sText_Failed : sText_Unchecked));
        break;
    case BS_TEAM_MENU:
        out = StringCopy(out, sTeamMenuLabels[row]);
        if (row == TM_FORMAT)
            StringAppend(out, KuroTeams_FormatTagName(team->formatTag));
        break;
    case BS_MEMBERS:
        if (row >= KURO_TEAM_SIZE)
            StringCopy(out, sText_Done);
        else if (row >= team->count)
            StringCopy(out, sText_Empty);
        else
            StringCopy(out, GetSpeciesName(team->mons[row].species));
        break;
    case BS_MON:
        out = StringCopy(out, sMonLabels[row]);
        switch (row)
        {
        case MR_SPECIES: StringAppend(out, GetSpeciesName(mon->species)); break;
        case MR_NICKNAME: StringAppend(out, mon->nickname[0] == EOS ? sText_None : mon->nickname); break;
        case MR_LEVEL: ConvertIntToDecimalStringN(out, mon->level, STR_CONV_MODE_LEFT_ALIGN, 3); break;
        case MR_GENDER: StringAppend(out, sGenderNames[mon->gender % 3]); break;
        case MR_SHINY: StringAppend(out, mon->shiny ? sYes : sNo); break;
        case MR_ITEM: StringAppend(out, mon->item == ITEM_NONE ? sText_None : GetItemName(mon->item)); break;
        case MR_ABILITY: StringAppend(out, gAbilitiesInfo[mon->ability].name); break;
        case MR_MOVE1: case MR_MOVE2: case MR_MOVE3: case MR_MOVE4:
            StringAppend(out, mon->moves[row - MR_MOVE1] == MOVE_NONE ? sText_None : GetMoveName(mon->moves[row - MR_MOVE1]));
            break;
        case MR_NATURE: StringAppend(out, gNaturesInfo[mon->nature].name); break;
        case MR_TERA: StringAppend(out, gTypesInfo[mon->teraType].name); break;
        }
        break;
    case BS_PICK:
        StringCopy(out, PickName(sPickIds[row]));
        break;
    case BS_EVS:
    case BS_IVS:
        if (row < NUM_STATS)
        {
            out = StringCopy(out, sStatNames[row]);
            out = StringAppend(out, sText_Space);
            ConvertIntToDecimalStringN(out, sBuilder == BS_EVS ? mon->evs[row] : mon->ivs[row], STR_CONV_MODE_LEFT_ALIGN, 3);
        }
        else
        {
            for (total = 0, i = 0; i < NUM_STATS; i++)
                total += mon->evs[i];
            if (sBuilder == BS_EVS)
            {
                out = StringCopy(out, sText_ExpLine);
                ConvertIntToDecimalStringN(out, total, STR_CONV_MODE_LEFT_ALIGN, 3);
                StringAppend(out, sText_Of510);
            }
        }
        break;
    }
}

static u32 RowCount(void)
{
    switch (sBuilder)
    {
    case BS_TEAMS: return KURO_TEAM_SLOTS;
    case BS_TEAM_MENU: return TM_COUNT;
    case BS_MEMBERS: return KURO_TEAM_SIZE + 1;
    case BS_MON: return MR_COUNT;
    case BS_PICK: return sPickCount;
    case BS_EVS: return NUM_STATS + 1;
    case BS_IVS: return NUM_STATS;
    default: return 0;
    }
}

static const u8 *Title(void)
{
    switch (sBuilder)
    {
    case BS_TEAMS: return sText_Teams;
    case BS_TEAM_MENU:
    case BS_MEMBERS: return CurrentTeam()->name;
    case BS_MON: return GetSpeciesName(CurrentMon()->species);
    case BS_PICK: return (sPickKind == PICK_MOVE && sPickAllMoves) ? sPick3All : sPickTitles[sPickKind];
    case BS_EVS: return sText_EvTitle;
    case BS_IVS: return sText_IvTitle;
    default: return NULL;
    }
}

static void Print(u32 windowId, const u8 *text, u32 y)
{
    AddTextPrinterParameterized(windowId, FONT_NORMAL, text, 0, y, TEXT_SKIP_DRAW, NULL);
}

static void DrawStats(u32 windowId)
{
    struct Pokemon *temp = AllocZeroed(sizeof(*temp));
    const struct KuroTeamMon *mon = CurrentMon();
    static const u8 statData[NUM_STATS] = {MON_DATA_MAX_HP, MON_DATA_ATK, MON_DATA_DEF, MON_DATA_SPEED, MON_DATA_SPATK, MON_DATA_SPDEF};
    u8 line[40], *out;
    u32 i;
    u8 hiddenNature = mon->nature;

    CreateMon(temp, mon->species, mon->level, 0, OTID_STRUCT_PRESET(0));
    SetMonData(temp, MON_DATA_HIDDEN_NATURE, &hiddenNature);
    for (i = 0; i < NUM_STATS; i++)
    {
        u32 ev = mon->evs[i], iv = mon->ivs[i];

        SetMonData(temp, MON_DATA_HP_EV + i, &ev);
        SetMonData(temp, MON_DATA_HP_IV + i, &iv);
    }
    CalculateMonStats(temp);
    Print(windowId, GetSpeciesName(mon->species), 0);
    for (i = 0; i < NUM_STATS; i++)
    {
        out = StringCopy(line, sStatNames[i]);
        out = StringAppend(out, sText_Space);
        ConvertIntToDecimalStringN(out, GetSpeciesBaseStat(mon->species, i), STR_CONV_MODE_RIGHT_ALIGN, 3);
        out = StringAppend(out + StringLength(out), sText_Slash);
        ConvertIntToDecimalStringN(out, GetMonData(temp, statData[i]), STR_CONV_MODE_RIGHT_ALIGN, 3);
        Print(windowId, line, LINE_HEIGHT * (i + 1));
    }
    Free(temp);
}

bool32 KuroBuilder_IsOpen(void)
{
    return sBuilder != BS_CLOSED;
}

void KuroBuilder_Open(void)
{
    GoTo(BS_TEAMS);
}

void KuroBuilder_Draw(u32 windowId)
{
    u8 line[60];
    u32 count = RowCount();
    u32 i, y = LINE_HEIGHT;

    FillWindowPixelBuffer(windowId, PIXEL_FILL(1));
    gKuroUiScreen = KURO_UI_BUILDER_BASE + sBuilder;
    gKuroPickerValue = (sBuilder == BS_PICK && sCursor < sPickCount) ? sPickIds[sCursor] : sCursor;

    switch (sBuilder)
    {
    case BS_MESSAGE:
    case BS_WAIT_EXPORT:
    case BS_WAIT_IMPORT:
        Print(windowId, sMessage, 0);
        break;
    case BS_CONFIRM_DELETE:
        Print(windowId, sText_ConfirmDelete, 0);
        break;
    case BS_STATS:
        DrawStats(windowId);
        break;
    default:
        Print(windowId, Title(), 0);
        for (i = sScroll; i < count && i < sScroll + ROWS; i++)
        {
            u8 *end = StringCopy(line, i == sCursor ? sText_Cursor : sText_Indent);

            RowText(end, i);
            Print(windowId, line, y);
            y += LINE_HEIGHT;
        }
        break;
    }
}

static void MoveCursor(s32 delta)
{
    s32 count = RowCount();

    if (count == 0)
        return;
    sCursor = ((s32)sCursor + delta % count + count) % count;
    if (sCursor < sScroll)
        sScroll = sCursor;
    else if (sCursor >= sScroll + ROWS)
        sScroll = sCursor - ROWS + 1;
    sDirty = TRUE;
}

static void SaveTeams(void)
{
    if (!KuroTeams_Save())
        ShowMessage(sText_SaveFailed, BS_TEAMS);
}

static void CB2_ReturnFromNaming(void)
{
    u32 size;

    if (sNamingTeam)
    {
        if (sNameBuffer[0] != EOS)
        {
            size = min(StringLength(sNameBuffer), KURO_TEAM_NAME_SIZE - 1);
            memcpy(CurrentTeam()->name, sNameBuffer, size);
            CurrentTeam()->name[size] = EOS;
        }
        sCursor = TM_RENAME;
    }
    else
    {
        struct KuroTeamMon *mon = CurrentMon();

        memset(mon->nickname, EOS, KURO_MON_NICK_SIZE);
        size = min(StringLength(sNameBuffer), KURO_MON_NICK_SIZE - 1);
        memcpy(mon->nickname, sNameBuffer, size);
        sCursor = MR_NICKNAME;
    }
    SaveTeams();
    SetMainCallback2(CB2_KuroMenuStart);
}

static void OpenNaming(bool32 team)
{
    sNamingTeam = team;
    if (team)
    {
        StringCopy(sNameBuffer, CurrentTeam()->name);
        DoNamingScreen(NAMING_SCREEN_BOX, sNameBuffer, 0, 0, 0, CB2_ReturnFromNaming);
    }
    else
    {
        memcpy(sNameBuffer, CurrentMon()->nickname, KURO_MON_NICK_SIZE);
        sNameBuffer[KURO_MON_NICK_SIZE] = EOS;
        DoNamingScreen(NAMING_SCREEN_NICKNAME, sNameBuffer, CurrentMon()->species, 0, 0, CB2_ReturnFromNaming);
    }
}

static void RemoveMon(void)
{
    struct KuroTeam *team = CurrentTeam();
    u32 i;

    for (i = sMon; i + 1 < team->count; i++)
        team->mons[i] = team->mons[i + 1];
    team->count--;
    memset(&team->mons[team->count], 0, sizeof(team->mons[0]));
    team->badge = KURO_BADGE_UNCHECKED;
}

static void AcceptPick(void)
{
    u16 id = sPickIds[sCursor];
    struct KuroTeamMon *mon;

    if (sPickIsNewMon)
    {
        struct KuroTeam *team = CurrentTeam();

        sMon = team->count++;
        KuroTeams_InitMon(&team->mons[sMon], id);
        team->badge = KURO_BADGE_UNCHECKED;
        FreePicker();
        GoTo(BS_MON);
        return;
    }
    mon = CurrentMon();
    switch (sPickKind)
    {
    case PICK_SPECIES:
        if (id != mon->species)
        {
            u8 keptMoves[sizeof(mon->moves)];

            memcpy(keptMoves, mon->moves, sizeof(keptMoves));
            KuroTeams_InitMon(mon, id);
            memcpy(mon->moves, keptMoves, sizeof(keptMoves));
        }
        break;
    case PICK_ITEM: mon->item = id; break;
    case PICK_ABILITY: mon->ability = id; break;
    case PICK_MOVE: mon->moves[sPickMoveSlot] = id; sPickAllMoves = FALSE; break;
    case PICK_NATURE: mon->nature = id; break;
    case PICK_TERA: mon->teraType = id; break;
    }
    CurrentTeam()->badge = KURO_BADGE_UNCHECKED;
    {
        u8 kind = sPickKind, slot = sPickMoveSlot;

        FreePicker();
        GoTo(BS_MON);
        sCursor = (kind == PICK_SPECIES) ? MR_SPECIES : (kind == PICK_ITEM) ? MR_ITEM : (kind == PICK_ABILITY) ? MR_ABILITY
                : (kind == PICK_MOVE) ? MR_MOVE1 + slot : (kind == PICK_NATURE) ? MR_NATURE : MR_TERA;
        sScroll = (sCursor >= ROWS) ? sCursor - ROWS + 1 : 0;
    }
}

static u32 TotalEvsExcept(u32 stat)
{
    u32 i, total = 0;

    for (i = 0; i < NUM_STATS; i++)
        if (i != stat)
            total += CurrentMon()->evs[i];
    return total;
}

static void AdjustEv(u32 stat, s32 delta)
{
    struct KuroTeamMon *mon = CurrentMon();
    s32 value = mon->evs[stat] + delta;
    s32 cap = min(MAX_PER_STAT_EVS, MAX_TOTAL_EVS - (s32)TotalEvsExcept(stat));

    value = max(0, min(value, cap));
    mon->evs[stat] = value;
    CurrentTeam()->badge = KURO_BADGE_UNCHECKED;
    sDirty = TRUE;
}

static void HandleMonRow(void)
{
    u32 row = sCursor;

    switch (row)
    {
    case MR_SPECIES: OpenPicker(PICK_SPECIES, 0, FALSE); break;
    case MR_NICKNAME: OpenNaming(FALSE); break;
    case MR_ITEM: OpenPicker(PICK_ITEM, 0, FALSE); break;
    case MR_ABILITY: OpenPicker(PICK_ABILITY, 0, FALSE); break;
    case MR_MOVE1: case MR_MOVE2: case MR_MOVE3: case MR_MOVE4: OpenPicker(PICK_MOVE, row - MR_MOVE1, FALSE); break;
    case MR_NATURE: OpenPicker(PICK_NATURE, 0, FALSE); break;
    case MR_EVS: GoTo(BS_EVS); break;
    case MR_IVS: GoTo(BS_IVS); break;
    case MR_TERA: OpenPicker(PICK_TERA, 0, FALSE); break;
    case MR_STATS: GoTo(BS_STATS); break;
    case MR_REMOVE: RemoveMon(); SaveTeams(); GoTo(BS_MEMBERS); break;
    case MR_BACK: SaveTeams(); GoTo(BS_MEMBERS); break;
    }
}

static void HandleTeamMenu(void)
{
    struct KuroTeam *team = CurrentTeam();
    u32 i;
    u8 body[1];

    switch (sCursor)
    {
    case TM_EDIT: GoTo(BS_MEMBERS); break;
    case TM_RENAME: OpenNaming(TRUE); break;
    case TM_FORMAT:
        team->formatTag = (team->formatTag + 1) % KURO_FORMAT_TAG_COUNT;
        SaveTeams();
        sDirty = TRUE;
        break;
    case TM_DUPLICATE:
        for (i = 0; i < KURO_TEAM_SLOTS; i++)
            if (gKuroTeams[i].count == 0)
                break;
        if (i == KURO_TEAM_SLOTS)
        {
            ShowMessage(sText_NoEmptySlot, BS_TEAM_MENU);
            break;
        }
        gKuroTeams[i] = *team;
        KuroTeams_DefaultName(i, gKuroTeams[i].name);
        SaveTeams();
        GoTo(BS_TEAMS);
        sCursor = i;
        break;
    case TM_MOVE_UP:
        if (sTeam > 0)
        {
            struct KuroTeam *temp = Alloc(sizeof(*temp));

            *temp = gKuroTeams[sTeam - 1];
            gKuroTeams[sTeam - 1] = gKuroTeams[sTeam];
            gKuroTeams[sTeam] = *temp;
            Free(temp);
            sTeam--;
            SaveTeams();
            sDirty = TRUE;
        }
        break;
    case TM_DELETE: GoTo(BS_CONFIRM_DELETE); break;
    case TM_EXPORT:
        if (team->count == 0)
        {
            ShowMessage(sText_EmptyTeam, BS_TEAM_MENU);
            break;
        }
        body[0] = sTeam;
        if (KuroTeams_Upload(sTeam) && KuroLink_SendFrame(KURO_OP_TEAM_EXPORT, body, 1))
        {
            gKuroSession.exportDone = FALSE;
            sMessage = sText_WaitExport;
            GoTo(BS_WAIT_EXPORT);
        }
        break;
    case TM_IMPORT:
        body[0] = sTeam;
        if (KuroLink_SendFrame(KURO_OP_TEAM_IMPORT_READY, body, 1))
        {
            gKuroSession.teamPushed = FALSE;
            sMessage = sText_WaitImport;
            GoTo(BS_WAIT_IMPORT);
        }
        break;
    case TM_BACK: GoTo(BS_TEAMS); sCursor = sTeam; break;
    }
}

static EWRAM_DATA u8 sExportMessage[80] = {0};

// Watches for replies from the relay. Returns TRUE to redraw.
bool32 KuroBuilder_Tick(void)
{
    if (sBuilder == BS_WAIT_EXPORT && gKuroSession.exportDone)
    {
        u8 *end = StringCopy(sExportMessage, gKuroSession.exportOk ? sText_Exported : sText_ExportFailed);

        end = StringAppend(end, gKuroSession.exportText);
        StringAppend(end, sText_PressA);
        gKuroSession.exportDone = FALSE;
        ShowMessage(sExportMessage, BS_TEAM_MENU);
    }
    if (sBuilder == BS_WAIT_IMPORT && gKuroSession.teamPushed)
    {
        gKuroSession.teamPushed = FALSE;
        SaveTeams();
        ShowMessage(sText_Saved, BS_TEAM_MENU);
    }
    if (sDirty)
    {
        sDirty = FALSE;
        return TRUE;
    }
    return FALSE;
}

// Returns TRUE when the builder has closed.
bool32 KuroBuilder_HandleInput(void)
{
    struct KuroTeam *team = CurrentTeam();
    bool32 accept = JOY_NEW(A_BUTTON), cancel = JOY_NEW(B_BUTTON);

    if (accept || cancel)
        PlaySE(SE_SELECT);

    switch (sBuilder)
    {
    case BS_MESSAGE:
        if (accept || cancel)
            GoTo(sReturnTo);
        return FALSE;
    case BS_WAIT_EXPORT:
        return FALSE;
    case BS_WAIT_IMPORT:
        if (cancel)
            GoTo(BS_TEAM_MENU);
        return FALSE;
    case BS_CONFIRM_DELETE:
        if (accept)
        {
            memset(team->mons, 0, sizeof(team->mons));
            team->count = 0;
            team->badge = KURO_BADGE_UNCHECKED;
            KuroTeams_DefaultName(sTeam, team->name);
            SaveTeams();
            GoTo(BS_TEAMS);
            sCursor = sTeam;
        }
        else if (cancel)
        {
            GoTo(BS_TEAM_MENU);
        }
        return FALSE;
    case BS_STATS:
        if (accept || cancel)
        {
            GoTo(BS_MON);
            sCursor = MR_STATS;
            sScroll = MR_STATS - ROWS + 1;
        }
        return FALSE;
    }

    if (JOY_REPEAT(DPAD_UP)) MoveCursor(-1);
    else if (JOY_REPEAT(DPAD_DOWN)) MoveCursor(1);
    else if (JOY_NEW(L_BUTTON) && sBuilder == BS_PICK) MoveCursor(-PAGE_SIZE);
    else if (JOY_NEW(R_BUTTON) && sBuilder == BS_PICK) MoveCursor(PAGE_SIZE);

    switch (sBuilder)
    {
    case BS_TEAMS:
        if (cancel)
        {
            sBuilder = BS_CLOSED;
            return TRUE;
        }
        if (accept)
        {
            sTeam = sCursor;
            GoTo(BS_TEAM_MENU);
        }
        break;
    case BS_TEAM_MENU:
        if (cancel)
        {
            GoTo(BS_TEAMS);
            sCursor = sTeam;
        }
        else if (accept)
        {
            HandleTeamMenu();
        }
        break;
    case BS_MEMBERS:
        if (cancel || (accept && sCursor == KURO_TEAM_SIZE))
        {
            SaveTeams();
            GoTo(BS_TEAM_MENU);
        }
        else if (accept)
        {
            if (sCursor < team->count)
            {
                sMon = sCursor;
                GoTo(BS_MON);
            }
            else
            {
                OpenPicker(PICK_SPECIES, 0, TRUE);
            }
        }
        break;
    case BS_MON:
    {
        struct KuroTeamMon *mon = CurrentMon();
        s32 direction = JOY_NEW(DPAD_RIGHT) ? 1 : (JOY_NEW(DPAD_LEFT) ? -1 : 0);

        if (cancel)
        {
            SaveTeams();
            GoTo(BS_MEMBERS);
        }
        else if (accept)
        {
            HandleMonRow();
        }
        else if (direction != 0)
        {
            if (sCursor == MR_LEVEL)
                mon->level = max(1, min(MAX_LEVEL, (s32)mon->level + direction));
            else if (sCursor == MR_GENDER)
                mon->gender = (mon->gender + direction + 3) % 3;
            else if (sCursor == MR_SHINY)
                mon->shiny = !mon->shiny;
            sDirty = TRUE;
        }
        break;
    }
    case BS_PICK:
        if (cancel)
        {
            bool32 wasNew = sPickIsNewMon;

            FreePicker();
            GoTo(wasNew ? BS_MEMBERS : BS_MON);
        }
        else if (accept && sPickCount > 0)
        {
            AcceptPick();
        }
        else if (JOY_NEW(SELECT_BUTTON) && sPickKind == PICK_MOVE)
        {
            // The expansion's learnsets omit moves that are legal on the server; let the player pick them.
            sPickAllMoves = !sPickAllMoves;
            PlaySE(SE_SELECT);
            OpenPicker(PICK_MOVE, sPickMoveSlot, FALSE);
        }
        break;
    case BS_EVS:
    case BS_IVS:
        if (cancel)
        {
            u8 row = (sBuilder == BS_EVS) ? MR_EVS : MR_IVS;

            GoTo(BS_MON);
            sCursor = row;
            sScroll = row - ROWS + 1;
        }
        else if (sCursor < NUM_STATS)
        {
            struct KuroTeamMon *mon = CurrentMon();

            if (sBuilder == BS_EVS)
            {
                if (JOY_REPEAT(DPAD_RIGHT)) AdjustEv(sCursor, EV_STEP);
                else if (JOY_REPEAT(DPAD_LEFT)) AdjustEv(sCursor, -EV_STEP);
                else if (JOY_NEW(R_BUTTON)) AdjustEv(sCursor, EV_STEP_LARGE);
                else if (JOY_NEW(L_BUTTON)) AdjustEv(sCursor, -EV_STEP_LARGE);
            }
            else if (JOY_REPEAT(DPAD_RIGHT) || JOY_REPEAT(DPAD_LEFT))
            {
                s32 delta = JOY_REPEAT(DPAD_RIGHT) ? 1 : -1;

                mon->ivs[sCursor] = max(0, min(MAX_PER_STAT_IVS, (s32)mon->ivs[sCursor] + delta));
                CurrentTeam()->badge = KURO_BADGE_UNCHECKED;
                sDirty = TRUE;
            }
        }
        break;
    }
    return FALSE;
}
