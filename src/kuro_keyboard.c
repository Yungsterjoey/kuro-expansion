// The on-screen keyboard, drawn in the front-end menu window (208 x 128).
#include "global.h"
#include "constants/abilities.h"
#include "constants/characters.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/songs.h"
#include "item.h"
#include "kuro_glyphs.h"
#include "kuro_keyboard.h"
#include "kuro_link.h"
#include "kuro_playback.h"
#include "kuro_session.h"
#include "main.h"
#include "kuro_teams.h"
#include "menu.h"
#include "move.h"
#include "pokemon.h"
#include "sound.h"
#include "string_util.h"
#include "strings.h"
#include "text.h"
#include "window.h"

enum KeyRow
{
    ROW_PREDICT,
    ROW_NUMBERS,
    ROW_TOP,
    ROW_HOME,
    ROW_BOTTOM,
    ROW_BAR,
    ROW_COUNT,
};

enum BarKey
{
    BAR_SHIFT,
    BAR_SYMBOLS,
    BAR_SPACE,
    BAR_BACKSPACE,
    BAR_SEND,
    BAR_COUNT,
};

enum ShiftState
{
    SHIFT_OFF,
    SHIFT_ONCE,
    SHIFT_CAPS,
};

enum ScanStage
{
    SCAN_SPECIES,
    SCAN_MOVES,
    SCAN_ITEMS,
    SCAN_ABILITIES,
    SCAN_DONE,
};

#define WINDOW_WIDTH 208
#define KEY_WIDTH 20
#define ROW_HEIGHT 16
#define HELP_Y 112
#define PREDICT_WIDTH 69
#define PREDICT_COUNT 3
#define SUGGESTION_SIZE 20
#define FIELD_WIDTH 146
#define MAX_DOTS 24
#define DOT_WIDTH 6
#define REPEAT_DELAY 18
#define REPEAT_RATE 4
#define CLEAR_WORD_FRAMES 30
#define DOUBLE_TAP_FRAMES 20
#define CHORD_FRAMES 3
#define SCAN_BUDGET 256
#define DRAW_BUDGET 3
#define COUNT_X 150
#define FIELD_COUNT_WIDTH 24
#define MIN_LEARNED_WORD 3

#define COLOR_KEY 3
#define COLOR_CURSOR 9

#define CHAR_LOWER_A 0xD5
#define CHAR_LOWER_Z 0xEE
#define CHAR_UPPER_A 0xBB
#define CHAR_UPPER_Z 0xD4
#define CASE_OFFSET (CHAR_LOWER_A - CHAR_UPPER_A)

struct KuroKeyboard
{
    bool8 open;
    bool8 secure;
    bool8 predictive;
    bool8 learn;
    bool8 confirming;
    bool8 hadDraft;
    u8 symbols; // Index into sPages.
    bool8 cursorHidden; // Secure mode: no highlight on the key just pressed, until the player moves.
    bool8 drawnCursorHidden;
    bool8 clearedWord;
    u8 row;
    u8 column;
    u8 shift;
    u8 shiftBeforeTap;
    u8 limit;
    u8 length;
    u8 repeat;
    u8 sinceL;
    u8 sinceR;
    u8 backHeld;
    u8 scanStage;
    u8 suggestionCount;
    u16 scanIndex;
    u8 lastHeldDirection;
    bool8 drawAll;
    bool8 drawField;
    bool8 drawPredict;
    bool8 drawnSymbols;
    bool8 drawnConfirming;
    u8 drawnShift;
    u8 drawnRow;
    u8 drawnColumn;
    u8 drawnLength;
    u8 fieldStart;
    u8 paintRow; // Keys still to draw, a few per frame, so a whole page never holds up input.
    u8 paintColumn;
    u8 paintLast;
    const u8 *title;
    u8 *target;
    u8 edit[KURO_KEYBOARD_MAX_LENGTH + 1];
    u8 prefix[SUGGESTION_SIZE];
    u8 suggestions[PREDICT_COUNT][SUGGESTION_SIZE];
};

static EWRAM_DATA struct KuroKeyboard sKb = {0};

static const u8 sRowLengths[ROW_COUNT] = {PREDICT_COUNT, 10, 10, 9, 7, BAR_COUNT};
static const u8 sRowY[ROW_COUNT] = {16, 32, 48, 64, 80, 96};
static const u8 sBarX[BAR_COUNT] = {0, 40, 80, 128, 168};
static const u8 sBarWidth[BAR_COUNT] = {40, 40, 48, 40, 40};

static const u8 sLetterRow0[] = _("1234567890");
static const u8 sLetterRow1[] = _("qwertyuiop");
static const u8 sLetterRow2[] = _("asdfghjkl");
static const u8 sLetterRow3[] = _("zxcvbnm");
static const u8 sSymbolRow0[] = _("!?.,-/:;()");
static const u8 sSymbolRow1[] = _("&+=%~<>…“”");
static const u8 sSymbolRow2[] = _("‘’♂♀¥×·¿¡");
static const u8 sSymbolRow3[] = _("éàèñüöß");
static const u8 sAsciiRow0[] = {CHAR_KURO_AT, CHAR_KURO_HASH, CHAR_KURO_DOLLAR, CHAR_KURO_CARET, CHAR_KURO_ASTERISK, CHAR_KURO_UNDERSCORE, CHAR_KURO_QUOTE, CHAR_SGL_QUOTE_RIGHT, CHAR_KURO_BACKTICK, CHAR_KURO_PIPE, EOS};
static const u8 sAsciiRow1[] = {CHAR_KURO_LBRACKET, CHAR_KURO_RBRACKET, CHAR_KURO_LBRACE, CHAR_KURO_RBRACE, CHAR_KURO_BACKSLASH, EOS, EOS, EOS, EOS, EOS, EOS};
static const u8 sEmptyRow[] = {EOS, EOS, EOS, EOS, EOS, EOS, EOS, EOS, EOS, EOS, EOS};
static const u8 *const sLetters[ROW_COUNT] = {NULL, sLetterRow0, sLetterRow1, sLetterRow2, sLetterRow3, NULL};
static const u8 *const sSymbols[ROW_COUNT] = {NULL, sSymbolRow0, sSymbolRow1, sSymbolRow2, sSymbolRow3, NULL};
static const u8 *const sAscii[ROW_COUNT] = {NULL, sAsciiRow0, sAsciiRow1, sEmptyRow, sEmptyRow, NULL};
static const u8 *const *const sPages[] = {sLetters, sSymbols, sAscii};

static const u8 sText_Shift[] = _("Shift");
static const u8 sText_Caps[] = _("CAPS");
static const u8 sText_Symbols[] = _("Sym");
static const u8 sText_Symbols2[] = _("Sym 2");
static const u8 sText_Letters[] = _("Abc");
static const u8 sText_Space[] = _("Space");
static const u8 sText_Delete[] = _("Del");
static const u8 sText_Send[] = _("Send");
static const u8 sText_Help[] = _("B del  L shift  R space  SEL symbols");
static const u8 sText_Confirm[] = _("Discard this draft? A: yes  B: no");
static const u8 sText_Slash[] = _("/");
static const u8 sText_Dot[] = _("·");

static const u8 sWord_Gg[] = _("gg");
static const u8 sWord_GlHf[] = _("gl hf");
static const u8 sWord_Wp[] = _("wp");
static const u8 sWord_Ty[] = _("ty");
static const u8 sWord_Np[] = _("np");
static const u8 sWord_Brb[] = _("brb");
static const u8 *const sCommonWords[] = {sWord_Gg, sWord_GlHf, sWord_Wp, sWord_Ty, sWord_Np, sWord_Brb};

static const u8 sTextColors[3] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_LIGHT_GRAY};

static u8 FoldCase(u8 c)
{
    return (c >= CHAR_UPPER_A && c <= CHAR_UPPER_Z) ? c + CASE_OFFSET : c;
}

static bool32 MatchesPrefix(const u8 *name)
{
    u32 i;

    for (i = 0; sKb.prefix[i] != EOS; i++)
        if (name[i] == EOS || FoldCase(name[i]) != FoldCase(sKb.prefix[i]))
            return FALSE;
    return name[i] != EOS;
}

static void AddSuggestion(const u8 *word)
{
    u32 i;

    if (sKb.suggestionCount >= PREDICT_COUNT || StringLength(word) >= SUGGESTION_SIZE)
        return;
    for (i = 0; i < sKb.suggestionCount; i++)
        if (StringCompare(sKb.suggestions[i], word) == 0)
            return;
    StringCopy(sKb.suggestions[sKb.suggestionCount++], word);
}

static void RebuildSuggestions(void)
{
    u32 i, start = sKb.length;

    if (!sKb.predictive)
        return;
    while (start > 0 && sKb.edit[start - 1] != CHAR_SPACE)
        start--;
    sKb.suggestionCount = 0;
    sKb.prefix[0] = EOS;
    if (sKb.length - start < SUGGESTION_SIZE)
        StringCopy(sKb.prefix, &sKb.edit[start]);

    for (i = 0; i < KURO_RECENT_WORDS; i++)
        if (gKuroRecentWords[i][0] != EOS && (sKb.prefix[0] == EOS || MatchesPrefix(gKuroRecentWords[i])))
            AddSuggestion(gKuroRecentWords[i]);
    for (i = 0; i < ARRAY_COUNT(sCommonWords); i++)
        if (sKb.prefix[0] == EOS || MatchesPrefix(sCommonWords[i]))
            AddSuggestion(sCommonWords[i]);

    // ROM names are searched a slice at a time, so typing never costs a frame.
    sKb.scanStage = (StringLength(sKb.prefix) >= 2) ? SCAN_SPECIES : SCAN_DONE;
    sKb.scanIndex = 1;
}

static const u8 *ScanName(u32 stage, u32 index)
{
    switch (stage)
    {
    case SCAN_SPECIES:
        if (index >= NUM_SPECIES)
            return NULL;
        return IsSpeciesEnabled(index) ? gSpeciesInfo[index].speciesName : gText_EmptyString2;
    case SCAN_MOVES:
        return index < MOVES_COUNT ? GetMoveName(index) : NULL;
    case SCAN_ITEMS:
        return index < ITEMS_COUNT ? GetItemName(index) : NULL;
    default:
        return index < ABILITIES_COUNT ? gAbilitiesInfo[index].name : NULL;
    }
}

static bool32 ScanNames(void)
{
    u32 budget = SCAN_BUDGET, before = sKb.suggestionCount;

    while (budget-- > 0 && sKb.scanStage != SCAN_DONE && sKb.suggestionCount < PREDICT_COUNT)
    {
        const u8 *name = ScanName(sKb.scanStage, sKb.scanIndex);

        if (name == NULL)
        {
            sKb.scanStage++;
            sKb.scanIndex = 1;
            continue;
        }
        if (MatchesPrefix(name))
            AddSuggestion(name);
        sKb.scanIndex++;
    }
    if (sKb.suggestionCount >= PREDICT_COUNT)
        sKb.scanStage = SCAN_DONE;
    if (sKb.suggestionCount != before)
        sKb.drawPredict = TRUE;
    return sKb.suggestionCount != before;
}

static bool32 RowUsable(u32 row)
{
    return row != ROW_PREDICT || (sKb.predictive && sKb.suggestionCount > 0);
}

static void TextChanged(void)
{
    sKb.drawField = TRUE;
    sKb.drawPredict = TRUE;
    RebuildSuggestions();
    if (sKb.row == ROW_PREDICT && (!RowUsable(ROW_PREDICT) || sKb.column >= sKb.suggestionCount))
    {
        sKb.row = ROW_TOP;
        sKb.column = 0;
    }
}

void KuroKeyboard_Open(const struct KuroKeyboardOptions *options)
{
    memset(&sKb, 0, sizeof(sKb));
    sKb.open = TRUE;
    gKuroUiScreen = options->secure ? KURO_UI_SECURE_KEYBOARD : KURO_UI_KEYBOARD;
    sKb.drawAll = TRUE;
    sKb.secure = options->secure;
    sKb.predictive = options->predictive && !options->secure;
    sKb.learn = options->learn && !options->secure;
    sKb.title = options->title;
    sKb.row = ROW_TOP;
    sKb.sinceL = DOUBLE_TAP_FRAMES;
    sKb.sinceR = CHORD_FRAMES;
    sKb.scanStage = SCAN_DONE;
    sKb.limit = sKb.secure ? KURO_SECURE_MAX_LENGTH : min(options->limit, KURO_KEYBOARD_MAX_LENGTH);
    sKb.edit[0] = EOS;
    if (!sKb.secure)
    {
        sKb.target = options->text;
        StringCopyN(sKb.edit, options->text, sKb.limit);
        sKb.edit[sKb.limit] = EOS;
        sKb.length = StringLength(sKb.edit);
        sKb.hadDraft = sKb.length > 0;
    }
    TextChanged();
}

void KuroKeyboard_Close(void)
{
    memset(&sKb, 0, sizeof(sKb));
}

bool32 KuroKeyboard_IsOpen(void)
{
    return sKb.open;
}

static bool32 SendSecureKey(u8 key)
{
    if (!KuroLink_SendFrame(KURO_OP_SECURE_KEY, &key, 1))
        return FALSE;
    KuroLink_MarkSecureTx();
    return TRUE;
}

static void TypeCharacter(u8 c)
{
    if (sKb.length >= sKb.limit)
        return;
    if (sKb.secure)
    {
        if (SendSecureKey(c))
            sKb.length++;
        sKb.drawField = TRUE;
        sKb.cursorHidden = TRUE;
        return;
    }
    sKb.edit[sKb.length++] = c;
    sKb.edit[sKb.length] = EOS;
    TextChanged();
}

static void TypeKey(u8 c)
{
    if (c == EOS)
        return;
    if (sKb.shift != SHIFT_OFF && c >= CHAR_LOWER_A && c <= CHAR_LOWER_Z)
        c -= CASE_OFFSET;
    if (sKb.shift == SHIFT_ONCE)
        sKb.shift = SHIFT_OFF;
    sKb.sinceL = DOUBLE_TAP_FRAMES;
    TypeCharacter(c);
}

static void RemoveLast(void)
{
    if (sKb.secure)
    {
        if (SendSecureKey(KURO_SECURE_BACKSPACE))
            sKb.length--;
        sKb.drawField = TRUE;
        return;
    }
    sKb.edit[--sKb.length] = EOS;
    TextChanged();
}

static void ClearWord(void)
{
    while (sKb.length > 0 && sKb.edit[sKb.length - 1] == CHAR_SPACE)
        sKb.length--;
    while (sKb.length > 0 && sKb.edit[sKb.length - 1] != CHAR_SPACE)
        sKb.length--;
    sKb.edit[sKb.length] = EOS;
    TextChanged();
}

static void ApplySuggestion(u32 index)
{
    u32 start = sKb.length;
    u32 size = StringLength(sKb.suggestions[index]);

    while (start > 0 && sKb.edit[start - 1] != CHAR_SPACE)
        start--;
    if (start + size > sKb.limit)
        return;
    StringCopy(&sKb.edit[start], sKb.suggestions[index]);
    sKb.drawnLength = 0; // The word's start changes too, so the line is redrawn whole.
    sKb.length = start + size;
    if (sKb.length < sKb.limit)
    {
        sKb.edit[sKb.length++] = CHAR_SPACE;
        sKb.edit[sKb.length] = EOS;
    }
    TextChanged();
}

static bool32 LearnWord(const u8 *word, u32 size)
{
    u8 entry[KURO_RECENT_WORD_SIZE];
    u32 i, found = KURO_RECENT_WORDS - 1;

    if (size < MIN_LEARNED_WORD || size >= KURO_RECENT_WORD_SIZE)
        return FALSE;
    memcpy(entry, word, size);
    entry[size] = EOS;
    if (StringCompare(gKuroRecentWords[0], entry) == 0)
        return FALSE;
    for (i = 0; i < KURO_RECENT_WORDS; i++)
    {
        if (StringCompare(gKuroRecentWords[i], entry) == 0)
        {
            found = i;
            break;
        }
    }
    for (i = found; i > 0; i--)
        memcpy(gKuroRecentWords[i], gKuroRecentWords[i - 1], KURO_RECENT_WORD_SIZE);
    memset(gKuroRecentWords[0], EOS, KURO_RECENT_WORD_SIZE);
    memcpy(gKuroRecentWords[0], entry, size + 1);
    return TRUE;
}

static void LearnWords(void)
{
    u32 start = 0, i;
    bool32 changed = FALSE;

    for (i = 0; i <= sKb.length; i++)
    {
        if (sKb.edit[i] == CHAR_SPACE || sKb.edit[i] == EOS)
        {
            if (LearnWord(&sKb.edit[start], i - start))
                changed = TRUE;
            start = i + 1;
        }
    }
    if (changed)
        KuroTeams_Save();
}

// Returns TRUE when the keyboard has finished.
static bool32 TrySend(void)
{
    if (sKb.secure)
    {
        if (!SendSecureKey(KURO_SECURE_SUBMIT))
            return FALSE;
    }
    else
    {
        if (sKb.length == 0)
            return FALSE;
        if (sKb.learn)
            LearnWords();
        StringCopy(sKb.target, sKb.edit);
    }
    KuroKeyboard_Close();
    return TRUE;
}

static void PressShift(void)
{
    if (sKb.symbols)
        return;
    if (sKb.sinceL < DOUBLE_TAP_FRAMES && sKb.shiftBeforeTap != SHIFT_CAPS)
        sKb.shift = SHIFT_CAPS;
    else
        sKb.shift = (sKb.shift == SHIFT_OFF) ? SHIFT_ONCE : SHIFT_OFF;
}

static void ToggleSymbols(void)
{
    sKb.symbols = (sKb.symbols + 1) % ARRAY_COUNT(sPages);
    sKb.shift = SHIFT_OFF;
}

// Returns TRUE when the keyboard has finished.
static bool32 PressKey(void)
{
    if (sKb.row == ROW_PREDICT)
    {
        ApplySuggestion(sKb.column);
    }
    else if (sKb.row != ROW_BAR)
    {
        TypeKey(sPages[sKb.symbols][sKb.row][sKb.column]);
    }
    else
    {
        switch (sKb.column)
        {
        case BAR_SHIFT: sKb.shiftBeforeTap = sKb.shift; PressShift(); sKb.sinceL = 0; break;
        case BAR_SYMBOLS: ToggleSymbols(); break;
        case BAR_SPACE: TypeCharacter(CHAR_SPACE); break;
        case BAR_BACKSPACE:
            if (sKb.length > 0)
                RemoveLast();
            break;
        case BAR_SEND: return TrySend();
        }
    }
    return FALSE;
}

static u32 KeyX(u32 row, u32 column)
{
    if (row == ROW_BAR)
        return sBarX[column];
    if (row == ROW_PREDICT)
        return column * PREDICT_WIDTH;
    return (WINDOW_WIDTH - sRowLengths[row] * KEY_WIDTH) / 2 + column * KEY_WIDTH;
}

static u32 KeyWidth(u32 row, u32 column)
{
    if (row == ROW_BAR)
        return sBarWidth[column];
    return row == ROW_PREDICT ? PREDICT_WIDTH : KEY_WIDTH;
}

static u32 RowLength(u32 row)
{
    return row == ROW_PREDICT ? sKb.suggestionCount : sRowLengths[row];
}

static void MoveRow(s32 step)
{
    s32 center = KeyX(sKb.row, sKb.column) + KeyWidth(sKb.row, sKb.column) / 2;
    s32 best = 0, bestDistance = WINDOW_WIDTH * 2;
    u32 i, row = sKb.row;

    do
        row = (row + step + ROW_COUNT) % ROW_COUNT;
    while (!RowUsable(row));

    for (i = 0; i < RowLength(row); i++)
    {
        s32 distance = abs((s32)(KeyX(row, i) + KeyWidth(row, i) / 2) - center);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    sKb.row = row;
    sKb.column = best;
}

static bool32 HandleDirection(void)
{
    u32 direction = gMain.heldKeys & DPAD_ANY;

    if (direction == 0 || (direction & (direction - 1)) != 0)
    {
        sKb.lastHeldDirection = 0;
        return FALSE;
    }
    if (direction != sKb.lastHeldDirection)
    {
        sKb.lastHeldDirection = direction;
        sKb.repeat = REPEAT_DELAY;
    }
    else if (--sKb.repeat != 0)
    {
        return FALSE;
    }
    else
    {
        sKb.repeat = REPEAT_RATE;
    }

    sKb.cursorHidden = FALSE;
    if (direction == DPAD_UP)
        MoveRow(-1);
    else if (direction == DPAD_DOWN)
        MoveRow(1);
    else
        sKb.column = (sKb.column + (direction == DPAD_LEFT ? -1 : 1) + RowLength(sKb.row)) % RowLength(sKb.row);
    return TRUE;
}

static void JumpToPredictions(void)
{
    if (RowUsable(ROW_PREDICT))
    {
        sKb.row = ROW_PREDICT;
        sKb.column = 0;
    }
}

static bool32 HandleShoulders(void)
{
    bool32 newL = JOY_NEW(L_BUTTON), newR = JOY_NEW(R_BUTTON);

    if (sKb.sinceL < 0xFF)
        sKb.sinceL++;
    if (sKb.sinceR < 0xFF)
        sKb.sinceR++;
    if (!newL && !newR)
        return FALSE;

    PlaySE(SE_SELECT);
    if (!sKb.secure && newL && newR)
    {
        JumpToPredictions();
    }
    else if (newL)
    {
        // The second half of an L+R chord takes back what the first press did.
        if (!sKb.secure && sKb.sinceR < CHORD_FRAMES && sKb.length > 0 && sKb.edit[sKb.length - 1] == CHAR_SPACE)
        {
            RemoveLast();
            JumpToPredictions();
        }
        else
        {
            sKb.shiftBeforeTap = sKb.shift;
            PressShift();
            sKb.sinceL = 0;
        }
    }
    else if (!sKb.secure && sKb.sinceL < CHORD_FRAMES)
    {
        sKb.shift = sKb.shiftBeforeTap;
        sKb.sinceL = DOUBLE_TAP_FRAMES;
        JumpToPredictions();
    }
    else
    {
        TypeCharacter(CHAR_SPACE);
        sKb.sinceR = 0;
    }
    return TRUE;
}

// Returns the keyboard's result for a press of B.
static enum KuroKeyboardResult PressBack(void)
{
    if (sKb.length > 0)
    {
        RemoveLast();
        return KURO_KEYBOARD_NONE;
    }
    if (sKb.hadDraft)
    {
        sKb.confirming = TRUE;
        return KURO_KEYBOARD_NONE;
    }
    if (sKb.secure && !SendSecureKey(KURO_SECURE_CANCEL))
        return KURO_KEYBOARD_NONE;
    KuroKeyboard_Close();
    return KURO_KEYBOARD_CLOSED;
}

static bool32 HandleHeldBack(void)
{
    if (!JOY_HELD(B_BUTTON))
    {
        sKb.backHeld = 0;
        sKb.clearedWord = FALSE;
        return FALSE;
    }
    if (sKb.secure || sKb.clearedWord || ++sKb.backHeld < CLEAR_WORD_FRAMES)
        return FALSE;
    sKb.clearedWord = TRUE;
    ClearWord();
    return TRUE;
}

enum KuroKeyboardResult KuroKeyboard_Update(bool32 *dirty)
{
    enum KuroKeyboardResult result = KURO_KEYBOARD_NONE;

    if (!sKb.open)
        return KURO_KEYBOARD_NONE;

    if (sKb.confirming)
    {
        if (JOY_NEW(A_BUTTON))
        {
            PlaySE(SE_SELECT);
            KuroKeyboard_Close();
            return KURO_KEYBOARD_CLOSED;
        }
        if (JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            sKb.confirming = FALSE;
            sKb.hadDraft = FALSE;
            *dirty = TRUE;
        }
        return KURO_KEYBOARD_NONE;
    }

    if (sKb.drawPredict || (sKb.paintRow != 0 && sKb.paintRow <= sKb.paintLast))
        *dirty = TRUE;
    if (sKb.predictive && sKb.scanStage != SCAN_DONE && ScanNames())
        *dirty = TRUE;
    if (HandleDirection())
        *dirty = TRUE;
    if (HandleShoulders())
        *dirty = TRUE;
    if (HandleHeldBack())
        *dirty = TRUE;

    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        *dirty = TRUE;
        if (PressKey())
            return KURO_KEYBOARD_SENT;
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        *dirty = TRUE;
        result = PressBack();
    }
    else if (JOY_NEW(SELECT_BUTTON))
    {
        PlaySE(SE_SELECT);
        ToggleSymbols();
        *dirty = TRUE;
    }
    else if (JOY_NEW(START_BUTTON))
    {
        PlaySE(SE_SELECT);
        *dirty = TRUE;
        if (TrySend())
            return KURO_KEYBOARD_SENT;
    }
    return result;
}

static void PrintAt(u32 windowId, u32 font, const u8 *text, u32 x, u32 y)
{
    AddTextPrinterParameterized4(windowId, font, x, y, 0, 0, sTextColors, TEXT_SKIP_DRAW, text);
}

static void DrawLabel(u32 windowId, u32 row, u32 column, const u8 *label, u32 font)
{
    u32 x = KeyX(row, column), y = sRowY[row], width = KeyWidth(row, column);
    u32 textWidth = GetStringWidth(font, label, 0);
    bool32 selected = (row == sKb.row && column == sKb.column && !sKb.cursorHidden);
    s32 inset = (s32)(width - 2 - textWidth) / 2;

    // Outlines are thin and fills are small: a per-pixel fill of every key is what costs frames.
    FillWindowPixelRect(windowId, PIXEL_FILL(COLOR_KEY), x, y, width - 1, 1);
    FillWindowPixelRect(windowId, PIXEL_FILL(COLOR_KEY), x, y + ROW_HEIGHT - 2, width - 1, 1);
    FillWindowPixelRect(windowId, PIXEL_FILL(COLOR_KEY), x, y, 1, ROW_HEIGHT - 1);
    FillWindowPixelRect(windowId, PIXEL_FILL(COLOR_KEY), x + width - 2, y, 1, ROW_HEIGHT - 1);
    FillWindowPixelRect(windowId, PIXEL_FILL(selected ? COLOR_CURSOR : TEXT_COLOR_WHITE), x + 1, y + 1, width - 3, ROW_HEIGHT - 3);
    PrintAt(windowId, font, label, x + 1 + max(inset, 0), y);
}

static void DrawKeyCell(u32 windowId, u32 row, u32 column)
{
    static const u8 *const sBarLabels[BAR_COUNT] = {sText_Shift, sText_Symbols, sText_Space, sText_Delete, sText_Send};
    const u8 *label;
    u8 glyph[2];

    if (row == ROW_PREDICT)
    {
        if (column < sKb.suggestionCount)
            DrawLabel(windowId, row, column, sKb.suggestions[column], FONT_SMALL);
        return;
    }
    if (row == ROW_BAR)
    {
        label = sBarLabels[column];
        if (column == BAR_SHIFT && sKb.shift == SHIFT_CAPS)
            label = sText_Caps;
        else if (column == BAR_SYMBOLS && sKb.symbols != 0)
            label = sKb.symbols == 1 ? sText_Symbols2 : sText_Letters;
        DrawLabel(windowId, row, column, label, FONT_SMALL);
        return;
    }
    glyph[0] = sPages[sKb.symbols][row][column];
    glyph[1] = EOS;
    if (sKb.shift != SHIFT_OFF && glyph[0] >= CHAR_LOWER_A && glyph[0] <= CHAR_LOWER_Z)
        glyph[0] -= CASE_OFFSET;
    DrawLabel(windowId, row, column, glyph, FONT_NORMAL);
}

static void DrawCount(u32 windowId)
{
    u8 buffer[8];

    FillWindowPixelRect(windowId, PIXEL_FILL(TEXT_COLOR_WHITE), COUNT_X, 0, FIELD_COUNT_WIDTH, 12);
    ConvertIntToDecimalStringN(buffer, sKb.length, STR_CONV_MODE_RIGHT_ALIGN, 3);
    PrintAt(windowId, FONT_SMALL, buffer, COUNT_X, 0);
}

static void DrawLimit(u32 windowId)
{
    u8 buffer[8];
    u8 *end = StringCopy(buffer, sText_Slash);

    ConvertIntToDecimalStringN(end, sKb.limit, STR_CONV_MODE_LEFT_ALIGN, 3);
    PrintAt(windowId, FONT_SMALL, buffer, COUNT_X + FIELD_COUNT_WIDTH, 0);
}

static void DrawWholeField(u32 windowId)
{
    u32 i, start = 0;

    FillWindowPixelRect(windowId, PIXEL_FILL(TEXT_COLOR_WHITE), 0, 0, COUNT_X, 14);
    if (sKb.length == 0)
    {
        PrintAt(windowId, FONT_NORMAL, sKb.title, 0, 0);
    }
    else if (sKb.secure)
    {
        for (i = 0; i < min(sKb.length, MAX_DOTS); i++)
            PrintAt(windowId, FONT_NORMAL, sText_Dot, i * DOT_WIDTH, 0);
    }
    else
    {
        while (GetStringWidth(FONT_NORMAL, &sKb.edit[start], 0) > FIELD_WIDTH)
            start++;
        PrintAt(windowId, FONT_NORMAL, &sKb.edit[start], 0, 0);
        sKb.fieldStart = start;
    }
}

// Typing adds or removes a little at the end of the line, so that is all that gets drawn.
static bool32 TryDrawFieldEnd(u32 windowId)
{
    u32 x;
    u8 saved;

    if (sKb.length == 0 || sKb.drawnLength == 0)
        return FALSE;
    if (sKb.secure)
    {
        if (sKb.length > MAX_DOTS || sKb.drawnLength > MAX_DOTS)
            return FALSE;
        if (sKb.length > sKb.drawnLength)
            PrintAt(windowId, FONT_NORMAL, sText_Dot, sKb.drawnLength * DOT_WIDTH, 0);
        else
            FillWindowPixelRect(windowId, PIXEL_FILL(TEXT_COLOR_WHITE), sKb.length * DOT_WIDTH, 0, (sKb.drawnLength - sKb.length) * DOT_WIDTH + 2, 14);
        return TRUE;
    }
    if (GetStringWidth(FONT_NORMAL, &sKb.edit[sKb.fieldStart], 0) > FIELD_WIDTH || sKb.fieldStart != 0)
        return FALSE;
    if (sKb.length > sKb.drawnLength)
    {
        saved = sKb.edit[sKb.drawnLength];
        sKb.edit[sKb.drawnLength] = EOS;
        x = GetStringWidth(FONT_NORMAL, sKb.edit, 0);
        sKb.edit[sKb.drawnLength] = saved;
        PrintAt(windowId, FONT_NORMAL, &sKb.edit[sKb.drawnLength], x, 0);
    }
    else
    {
        x = GetStringWidth(FONT_NORMAL, sKb.edit, 0);
        FillWindowPixelRect(windowId, PIXEL_FILL(TEXT_COLOR_WHITE), x, 0, COUNT_X - x, 14);
    }
    return TRUE;
}

static void DrawField(u32 windowId)
{
    DrawWholeField(windowId);
    DrawCount(windowId);
    DrawLimit(windowId);
    FillWindowPixelRect(windowId, PIXEL_FILL(COLOR_KEY), 0, 14, WINDOW_WIDTH, 1);
}

static void DrawPredictions(u32 windowId)
{
    u32 column;

    FillWindowPixelRect(windowId, PIXEL_FILL(TEXT_COLOR_WHITE), 0, sRowY[ROW_PREDICT], WINDOW_WIDTH, ROW_HEIGHT);
    if (sKb.predictive)
        for (column = 0; column < sKb.suggestionCount; column++)
            DrawKeyCell(windowId, ROW_PREDICT, column);
}

static void PaintSomeKeys(u32 windowId, s32 budget)
{
    while (budget-- > 0 && sKb.paintRow != 0 && sKb.paintRow <= sKb.paintLast)
    {
        if (sKb.paintRow == ROW_COUNT)
        {
            PrintAt(windowId, FONT_SMALL, sKb.confirming ? sText_Confirm : sText_Help, 0, HELP_Y);
            sKb.paintRow++;
            return;
        }
        DrawKeyCell(windowId, sKb.paintRow, sKb.paintColumn);
        if (++sKb.paintColumn >= sRowLengths[sKb.paintRow])
        {
            sKb.paintColumn = 0;
            sKb.paintRow++;
        }
    }
}

// Only what changed is drawn, and only a few keys a frame, so input is never held up.
void KuroKeyboard_Draw(u32 windowId)
{
    s32 budget = DRAW_BUDGET;

    gKuroUiScreen = sKb.secure ? KURO_UI_SECURE_KEYBOARD : KURO_UI_KEYBOARD;
    if (sKb.drawAll || sKb.symbols != sKb.drawnSymbols || sKb.confirming != sKb.drawnConfirming)
    {
        FillWindowPixelBuffer(windowId, PIXEL_FILL(TEXT_COLOR_WHITE));
        DrawField(windowId);
        sKb.paintRow = ROW_NUMBERS;
        sKb.paintColumn = 0;
        sKb.paintLast = ROW_COUNT;
        sKb.drawPredict = sKb.predictive;
        sKb.drawField = FALSE;
        sKb.drawAll = FALSE;
        sKb.drawnLength = sKb.length;
        budget = 0;
    }
    if (sKb.row != sKb.drawnRow || sKb.column != sKb.drawnColumn || sKb.cursorHidden != sKb.drawnCursorHidden)
    {
        DrawKeyCell(windowId, sKb.drawnRow, sKb.drawnColumn);
        DrawKeyCell(windowId, sKb.row, sKb.column);
        budget -= 2;
    }
    if (sKb.drawField)
    {
        if (!TryDrawFieldEnd(windowId))
        {
            DrawWholeField(windowId);
            budget -= 2;
        }
        DrawCount(windowId);
        sKb.drawField = FALSE;
        sKb.drawnLength = sKb.length;
        budget -= 1;
    }
    if (sKb.shift != sKb.drawnShift)
    {
        DrawKeyCell(windowId, ROW_BAR, BAR_SHIFT);
        if ((sKb.shift != SHIFT_OFF) != (sKb.drawnShift != SHIFT_OFF))
        {
            sKb.paintRow = ROW_TOP;
            sKb.paintColumn = 0;
            sKb.paintLast = ROW_BOTTOM;
        }
        budget -= 1;
    }
    if (sKb.drawPredict && budget >= DRAW_BUDGET)
    {
        DrawPredictions(windowId);
        sKb.drawPredict = FALSE;
        budget = 0;
    }
    PaintSomeKeys(windowId, budget);
    sKb.drawnSymbols = sKb.symbols;
    sKb.drawnConfirming = sKb.confirming;
    sKb.drawnShift = sKb.shift;
    sKb.drawnRow = sKb.row;
    sKb.drawnColumn = sKb.column;
    sKb.drawnCursorHidden = sKb.cursorHidden;
}
