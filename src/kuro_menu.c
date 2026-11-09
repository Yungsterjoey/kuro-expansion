// The client's front-end screens: connection, main menu, format list and search.
#include "global.h"
#include "bg.h"
#include "constants/characters.h"
#include "gpu_regs.h"
#include "kuro_account.h"
#include "kuro_builder.h"
#include "kuro_chat.h"
#include "kuro_keyboard.h"
#include "kuro_link.h"
#include "kuro_playback.h"
#include "kuro_session.h"
#include "kuro_teams.h"
#include "main.h"
#include "malloc.h"
#include "menu.h"
#include "palette.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "scanline_effect.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "window.h"
#include "constants/rgb.h"
#include "constants/songs.h"

enum KuroMenuScreen
{
    SCREEN_CONNECTING,
    SCREEN_MISMATCH,
    SCREEN_MAIN,
    SCREEN_FORMATS,
    SCREEN_SEARCHING,
    SCREEN_TEAM_PICK,
    SCREEN_BUILDER,
    SCREEN_PREVIEW,
    SCREEN_RESULT,
    SCREEN_ACCOUNT,
    SCREEN_KEYBOARD_TEST,
    SCREEN_CHAT,
};

#define WINDOW_MAIN 0
#define LINE_HEIGHT 16
#define BORDER_BASE_TILE 0x200
#define BORDER_PALETTE 14
#define TEXT_PALETTE 15

static const struct BgTemplate sBgTemplates[] =
{
    {.bg = 0, .charBaseIndex = 2, .mapBaseIndex = 31, .screenSize = 0, .paletteMode = 0, .priority = 1, .baseTile = 0},
    {.bg = 1, .charBaseIndex = 2, .mapBaseIndex = 30, .screenSize = 0, .paletteMode = 0, .priority = 0, .baseTile = 0}, // The banner, above everything.
};

static const struct WindowTemplate sWindowTemplates[] =
{
    [WINDOW_MAIN] = {.bg = 0, .tilemapLeft = 2, .tilemapTop = 2, .width = 26, .height = 16, .paletteNum = TEXT_PALETTE, .baseBlock = 1},
    DUMMY_WIN_TEMPLATE,
};

static EWRAM_DATA u8 sScreen = 0;
static EWRAM_DATA u8 sCursor = 0;
static EWRAM_DATA bool8 sDirty = FALSE;
static EWRAM_DATA u8 sLastStatus = 0;
static EWRAM_DATA u8 sLastFormatCount = 0;
static EWRAM_DATA u8 sLastSearchState = 0;
static EWRAM_DATA u8 sLastBridgeFlags = 0;
static EWRAM_DATA u8 sPayload[KURO_MAX_PAYLOAD + 1] = {0};
static EWRAM_DATA u8 sShownPopup[240] = {0};
static EWRAM_DATA u16 sSearchFrames = 0;
static EWRAM_DATA u8 sFormatCursor = 0;
static EWRAM_DATA u8 sSearchTeam = 0;
static EWRAM_DATA bool8 sTeamsLoaded = FALSE;
static EWRAM_DATA u8 sLastTeamSlot = 0;
static EWRAM_DATA u8 sPendingStart[KURO_MAX_PAYLOAD + 1] = {0};
static EWRAM_DATA u16 sPendingStartLength = 0;
static EWRAM_DATA bool8 sHavePendingStart = FALSE;
static EWRAM_DATA bool8 sPreviewAnswered = FALSE;
static EWRAM_DATA bool8 sResyncAtMenu = FALSE;
static EWRAM_DATA u8 sOurCount = 0;
static EWRAM_DATA u8 sFoeCount = 0;
static EWRAM_DATA u16 sOurSpecies[PARTY_SIZE] = {0};
static EWRAM_DATA u16 sFoeSpecies[PARTY_SIZE] = {0};
static EWRAM_DATA u8 sOrder[PARTY_SIZE] = {0};
static EWRAM_DATA u8 sOrderCount = 0;
static EWRAM_DATA u8 sIconIds[PARTY_SIZE * 2] = {0};
static EWRAM_DATA bool8 sIconsUp = FALSE;
static EWRAM_DATA u8 sKeyboardText[KURO_KEYBOARD_MAX_LENGTH + 1] = {0};

static EWRAM_DATA u32 sLastLoopVblank = 0;
static EWRAM_DATA u32 sChatLoopVblank = 0;

EWRAM_DATA volatile u8 gKuroUiScreen = 0;
// The most frames one trip round the menu loop took with the keyboard open. Read by tools/regress.
EWRAM_DATA volatile u8 gKuroKeyboardFrameGap = 0;

static const u8 sText_Title[] = _("KURO");
static const u8 sText_NoBridge[] = _("Bridge not attached.\nLoad the KURO script in mGBA.");
static const u8 sText_NoRelay[] = _("Relay not running.\nStart KURO Relay on this PC.");
static const u8 sText_Waiting[] = _("Connecting to the relay…");
static const u8 sText_MismatchTitle[] = _("Relay and ROM don't match.\nUpdate both to the same version.\n");
static const u8 sText_RomLabel[] = _("ROM: ");
static const u8 sText_RelayLabel[] = _("\nRelay: ");
static const u8 sText_SignedInAs[] = _("Signed in as ");
static const u8 sText_FindBattle[] = _("Find Battle");
static const u8 sText_Lobby[] = _("Lobby");
static const u8 sText_TeamBuilder[] = _("Team Builder");
static const u8 sText_Pms[] = _("Private Messages");
static const u8 sText_Options[] = _("Options");
static const u8 sText_GuestSearch[] = _("Sign in to search for a battle.");
static const u8 sText_GuestAs[] = _("Playing as guest ");
static const u8 sText_KeyboardTest[] = _("Keyboard test");
static const u8 sText_KeyboardSent[] = _("Sent:\n");
static const u8 *const sMainRows[] = {sText_FindBattle, sText_Lobby, sText_TeamBuilder, sText_Pms, sText_Options};
enum MainRow { MAIN_FIND, MAIN_LOBBY, MAIN_TEAMS, MAIN_PMS, MAIN_OPTIONS };
static const u8 sText_GuestChat[] = _("Sign in to chat.");
static const u8 sText_ChooseTeam[] = _("Choose a team");
static const u8 sText_NoTeams[] = _("No teams yet. Build one first.");
static const u8 sText_PressAToContinue[] = _("Press A to continue.");
static const u8 sText_Indent[] = _("  ");
static const u8 sText_PreviewTitle[] = _("Team Preview");
static const u8 sText_PreviewHelp[] = _("A: choose order  B: undo");
static const u8 sText_PreviewYours[] = _("Your team");
static const u8 sText_PreviewFoe[] = _("Their team");
static const u8 sText_PreviewWaiting[] = _("Waiting for the opponent…");
static const u8 sText_YouWon[] = _("You won!");
static const u8 sText_YouLost[] = _("You lost.");
static const u8 sText_Tied[] = _("It was a tie.");
static const u8 sText_Versus[] = _("vs. ");
static const u8 sText_Rating[] = _("Rating ");
static const u8 sText_ResultMenu[] = _("Main menu");
static const u8 sText_ResultRematch[] = _("Battle again");
static const u8 sText_ResultReplay[] = _("Save replay");
static const u8 sText_Plus[] = _("+");
static const u8 sText_Minus[] = _("-");
static const u8 sText_Open[] = _(" (");
static const u8 sText_Close[] = _(")");
static const u8 *const sResultRows[] = {sText_ResultMenu, sText_ResultRematch, sText_ResultReplay};
static const u8 sText_ChooseFormat[] = _("Choose a format");
static const u8 sText_NoFormats[] = _("Waiting for the format list…");
static const u8 sText_Searching[] = _("Searching for a battle…");
static const u8 sText_PressBToCancel[] = _("Press B to cancel.");
static const u8 sText_Cursor[] = _("{RIGHT_ARROW} ");
static const u8 sText_Colon[] = _(":");

static void VBlankCB_KuroMenu(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void PrintLine(const u8 *text, u32 y)
{
    AddTextPrinterParameterized(WINDOW_MAIN, FONT_NORMAL, text, 0, y, TEXT_SKIP_DRAW, NULL);
}

static void DrawScreen(void)
{
    u8 buffer[100];
    u32 i, y;

    // The keyboard draws only what changed, so the window must not be cleared under it.
    if (KuroKeyboard_IsOpen())
    {
        KuroKeyboard_Draw(WINDOW_MAIN);
        CopyWindowToVram(WINDOW_MAIN, COPYWIN_FULL);
        return;
    }

    FillWindowPixelBuffer(WINDOW_MAIN, PIXEL_FILL(1));
    switch (sScreen)
    {
    case SCREEN_CONNECTING:
        PrintLine(sText_Title, 0);
        if (!(gKuroLink.bridgeFlags & 1))
            PrintLine(sText_NoBridge, LINE_HEIGHT * 2);
        else if (!(gKuroLink.bridgeFlags & 2))
            PrintLine(sText_NoRelay, LINE_HEIGHT * 2);
        else if (gKuroSession.detail[0] != EOS)
            PrintLine(gKuroSession.detail, LINE_HEIGHT * 2);
        else
            PrintLine(sText_Waiting, LINE_HEIGHT * 2);
        break;
    case SCREEN_MISMATCH:
        PrintLine(sText_MismatchTitle, 0);
        StringCopy(buffer, sText_RomLabel);
        StringAppend(buffer, KuroSession_RomBuildString());
        StringAppend(buffer, sText_RelayLabel);
        StringAppend(buffer, gKuroSession.relayBuild);
        PrintLine(buffer, LINE_HEIGHT * 4);
        break;
    case SCREEN_MAIN:
        StringCopy(buffer, gKuroSession.status == KURO_RELAY_GUEST ? sText_GuestAs : sText_SignedInAs);
        StringAppend(buffer, gKuroSession.username);
        PrintLine(buffer, 0);
        for (i = 0; i < ARRAY_COUNT(sMainRows); i++)
        {
            StringCopy(buffer, sCursor == i ? sText_Cursor : sText_Indent);
            StringAppend(buffer, sMainRows[i]);
            PrintLine(buffer, LINE_HEIGHT * (i + 2));
        }
        KuroChat_DrawHubLine(buffer);
        PrintLine(buffer, LINE_HEIGHT);
        if (sShownPopup[0] != EOS)
            PrintLine(sShownPopup, LINE_HEIGHT * 7);
        break;
    case SCREEN_CHAT:
        KuroChat_Draw(WINDOW_MAIN);
        CopyWindowToVram(WINDOW_MAIN, COPYWIN_FULL);
        return;
    case SCREEN_ACCOUNT:
        KuroAccount_Draw(WINDOW_MAIN);
        CopyWindowToVram(WINDOW_MAIN, COPYWIN_FULL);
        return;
    case SCREEN_KEYBOARD_TEST:
        PrintLine(sText_KeyboardSent, 0);
        PrintLine(sKeyboardText, LINE_HEIGHT);
        break;
    case SCREEN_TEAM_PICK:
        if (sShownPopup[0] != EOS)
        {
            // The server's reason gets the whole window; it can run to several lines.
            PrintLine(sShownPopup, 0);
            PrintLine(sText_PressAToContinue, LINE_HEIGHT * 7);
            break;
        }
        PrintLine(sText_ChooseTeam, 0);
        y = LINE_HEIGHT * 2;
        for (i = 0; i < KURO_TEAM_SLOTS; i++)
        {
            if (gKuroTeams[i].count == 0)
                continue;
            StringCopy(buffer, i == sCursor ? sText_Cursor : sText_Indent);
            StringAppend(buffer, gKuroTeams[i].name);
            PrintLine(buffer, y);
            y += LINE_HEIGHT;
        }
        if (y == LINE_HEIGHT * 2)
            PrintLine(sText_NoTeams, y);
        break;
    case SCREEN_BUILDER:
        KuroBuilder_Draw(WINDOW_MAIN);
        CopyWindowToVram(WINDOW_MAIN, COPYWIN_FULL);
        return;
    case SCREEN_PREVIEW:
        PrintLine(sPreviewAnswered ? sText_PreviewWaiting : sText_PreviewTitle, 0);
        PrintLine(sText_PreviewYours, LINE_HEIGHT);
        for (i = 0; i < sOrderCount; i++)
        {
            ConvertIntToDecimalStringN(buffer, i + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
            AddTextPrinterParameterized(WINDOW_MAIN, FONT_NORMAL, buffer, 12 + sOrder[i] * 32, LINE_HEIGHT * 3, TEXT_SKIP_DRAW, NULL);
        }
        if (!sPreviewAnswered)
            AddTextPrinterParameterized(WINDOW_MAIN, FONT_NORMAL, sText_Cursor, sCursor * 32, LINE_HEIGHT * 3, TEXT_SKIP_DRAW, NULL);
        PrintLine(sText_PreviewFoe, LINE_HEIGHT * 4);
        PrintLine(sText_PreviewHelp, LINE_HEIGHT * 7);
        break;
    case SCREEN_RESULT:
        y = 0;
        PrintLine(gKuroSession.resultKind == 1 ? sText_YouWon : (gKuroSession.resultKind == 2 ? sText_YouLost : sText_Tied), y);
        y += LINE_HEIGHT;
        StringCopy(buffer, sText_Versus);
        StringAppend(buffer, gKuroSession.resultOpponent);
        PrintLine(buffer, y);
        y += LINE_HEIGHT;
        if (gKuroSession.rating != 0)
        {
            u8 *end = StringCopy(buffer, sText_Rating);

            ConvertIntToDecimalStringN(end, gKuroSession.rating, STR_CONV_MODE_LEFT_ALIGN, 4);
            end = StringAppend(end, sText_Open);
            end = StringAppend(end, gKuroSession.ratingChange >= 0 ? sText_Plus : sText_Minus);
            ConvertIntToDecimalStringN(end, abs(gKuroSession.ratingChange), STR_CONV_MODE_LEFT_ALIGN, 4);
            StringAppend(end, sText_Close);
            PrintLine(buffer, y);
        }
        y = LINE_HEIGHT * 3;
        for (i = 0; i < ARRAY_COUNT(sResultRows); i++)
        {
            StringCopy(buffer, i == sCursor ? sText_Cursor : sText_Indent);
            StringAppend(buffer, sResultRows[i]);
            PrintLine(buffer, y);
            y += LINE_HEIGHT;
        }
        if (sShownPopup[0] != EOS)
            PrintLine(sShownPopup, LINE_HEIGHT * 6);
        break;
    case SCREEN_FORMATS:
        PrintLine(sText_ChooseFormat, 0);
        if (gKuroSession.formatCount == 0)
            PrintLine(sText_NoFormats, LINE_HEIGHT * 2);
        y = LINE_HEIGHT * 2;
        for (i = 0; i < gKuroSession.formatCount; i++)
        {
            if (i == sCursor)
            {
                StringCopy(buffer, sText_Cursor);
                StringAppend(buffer, gKuroSession.formats[i].name);
                PrintLine(buffer, y);
            }
            else
            {
                PrintLine(gKuroSession.formats[i].name, y);
            }
            y += LINE_HEIGHT;
        }
        break;
    case SCREEN_SEARCHING:
        PrintLine(sText_Searching, 0);
        ConvertIntToDecimalStringN(buffer, sSearchFrames / 3600, STR_CONV_MODE_LEFT_ALIGN, 2);
        StringAppend(buffer, sText_Colon);
        ConvertIntToDecimalStringN(buffer + StringLength(buffer), (sSearchFrames / 60) % 60, STR_CONV_MODE_LEADING_ZEROS, 2);
        PrintLine(buffer, LINE_HEIGHT);
        PrintLine(sText_PressBToCancel, LINE_HEIGHT * 3);
        if (sShownPopup[0] != EOS)
            PrintLine(sShownPopup, LINE_HEIGHT * 4);
        break;
    }
    CopyWindowToVram(WINDOW_MAIN, COPYWIN_FULL);
    gKuroUiScreen = sScreen + KURO_UI_MENU_BASE;
}

void KuroMenu_RequestDraw(void)
{
    sDirty = TRUE;
}

void KuroMenu_ShowMainLayout(void)
{
    FillBgTilemapBufferRect_Palette0(0, 0, 0, 0, 32, 32);
    PutWindowTilemap(WINDOW_MAIN);
    DrawStdFrameWithCustomTileAndPalette(WINDOW_MAIN, FALSE, BORDER_BASE_TILE, BORDER_PALETTE);
    CopyBgTilemapBufferToVram(0);
    sDirty = TRUE;
}

void KuroMenu_HideMainLayout(void)
{
    FillBgTilemapBufferRect_Palette0(0, 0, 0, 0, 32, 32);
    CopyBgTilemapBufferToVram(0);
}

static void SetScreen(u8 screen)
{
    sScreen = screen;
    sCursor = 0;
    sDirty = TRUE;
}

static void OpenAccount(enum KuroAccountEntry entry)
{
    KuroAccount_Open(entry);
    SetScreen(SCREEN_ACCOUNT);
}

static void UpdateConnection(void)
{
    if (gKuroSession.mismatch)
    {
        if (sScreen != SCREEN_MISMATCH)
            SetScreen(SCREEN_MISMATCH);
        return;
    }
    if (sScreen == SCREEN_CONNECTING && gKuroSession.handshakeDone
     && (gKuroSession.status == KURO_RELAY_GUEST || gKuroSession.status == KURO_RELAY_LOGGED_IN))
    {
        SetScreen(SCREEN_MAIN);
    }
    else if ((sScreen == SCREEN_CONNECTING || sScreen == SCREEN_MAIN || sScreen == SCREEN_FORMATS)
          && gKuroSession.handshakeDone && gKuroSession.status == KURO_RELAY_LOGGED_OUT)
    {
        OpenAccount(KURO_ACCOUNT_SIGN_IN);
    }
    else if (sScreen != SCREEN_CONNECTING && sScreen != SCREEN_KEYBOARD_TEST && !(gKuroLink.bridgeFlags & 2))
    {
        if (sScreen == SCREEN_ACCOUNT)
            KuroAccount_Abort();
        if (sScreen == SCREEN_CHAT)
            KuroChat_Close();
        SetScreen(SCREEN_CONNECTING);
    }
}

static bool32 HandlePreviewFrame(u8 opcode, u16 length);
static void StartSearch(u8 teamSlot);
static bool32 TryStartPendingBattle(void);

// Returns TRUE when a battle has begun and this screen has handed over.
static bool32 PumpLink(void)
{
    u8 opcode;
    u16 length;

    for (;;)
    {
        if (TryStartPendingBattle())
            return TRUE;
        if (KuroLink_ReadFrame(&opcode, sPayload, &length) != KURO_LINK_READ_OK)
            break;
        if (!KuroSession_IsSessionOpcode(opcode))
        {
            if (HandlePreviewFrame(opcode, length))
                continue;
            KuroPlayback_StartClientBattle(opcode, sPayload, length);
            return TRUE;
        }
        KuroSession_HandleFrame(opcode, sPayload, length);
    }
    return FALSE;
}

// Team preview runs here, before the battle scene exists.
#define OP_RESYNC_BEGIN 0x63
#define OP_RESYNC_END 0x64
#define OP_BATTLE_START 0x40
#define REQUEST_FLAGS_OFFSET 2
#define REQUEST_PARTY_COUNT_OFFSET 28
#define REQUEST_PARTY_OFFSET 29
#define REQUEST_MEMBER_SIZE 25

static void DestroyIcons(void)
{
    u32 i;

    if (!sIconsUp)
        return;
    for (i = 0; i < sOurCount + sFoeCount; i++)
        FreeAndDestroyMonIconSprite(&gSprites[sIconIds[i]]);
    FreeMonIconPalettes();
    sIconsUp = FALSE;
}

static void ShowPreview(void)
{
    u32 i;

    DestroyIcons();
    LoadMonIconPalettes();
    for (i = 0; i < sOurCount; i++)
        sIconIds[i] = CreateMonIcon(sOurSpecies[i], SpriteCallbackDummy, 40 + i * 32, 56, 1, 0);
    for (i = 0; i < sFoeCount; i++)
        sIconIds[sOurCount + i] = CreateMonIcon(sFoeSpecies[i], SpriteCallbackDummy, 40 + i * 32, 104, 1, 0);
    for (i = 0; i < sOurCount + sFoeCount; i++)
        gSprites[sIconIds[i]].oam.priority = 0;
    sIconsUp = TRUE;
    sOrderCount = 0;
    sScreen = SCREEN_PREVIEW;
    sCursor = 0;
    sDirty = TRUE;
}

static void StoreRequestParty(u16 length)
{
    u32 i, count = sPayload[REQUEST_PARTY_COUNT_OFFSET];

    if (length < REQUEST_PARTY_OFFSET || count > PARTY_SIZE)
        return;
    sOurCount = count;
    for (i = 0; i < count; i++)
    {
        const u8 *member = &sPayload[REQUEST_PARTY_OFFSET + i * REQUEST_MEMBER_SIZE];

        sOurSpecies[i] = member[0] | (member[1] << 8);
    }
}

// Returns TRUE if the frame belongs to team preview and was consumed.
static bool32 HandlePreviewFrame(u8 opcode, u16 length)
{
    u32 i;

    if (opcode == OP_RESYNC_BEGIN)
    {
        // Hold the replay until it shows whether the battle is still at team preview.
        sResyncAtMenu = !(sHavePendingStart || sScreen == SCREEN_PREVIEW);
        return TRUE;
    }
    if (opcode == OP_RESYNC_END && (sHavePendingStart || sScreen == SCREEN_PREVIEW))
    {
        if (sFoeCount > 0)
            ShowPreview();
        sResyncAtMenu = FALSE;
        return TRUE;
    }
    if (opcode == OP_BATTLE_START)
    {
        memcpy(sPendingStart, sPayload, length);
        sPendingStartLength = length;
        sHavePendingStart = TRUE;
        sPreviewAnswered = FALSE;
        sFoeCount = 0;
        return TRUE;
    }
    if (!sHavePendingStart)
        return FALSE;
    if (opcode == KURO_FRAME_TEAM_PREVIEW)
    {
        sFoeCount = min(sPayload[2], PARTY_SIZE);
        for (i = 0; i < sFoeCount; i++)
            sFoeSpecies[i] = sPayload[3 + i * 3] | (sPayload[4 + i * 3] << 8);
        return TRUE;
    }
    if (opcode == KURO_FRAME_REQUEST && sFoeCount > 0 && !sPreviewAnswered && (sPayload[REQUEST_FLAGS_OFFSET] & KURO_REQUEST_FLAG_TEAM_PREVIEW))
    {
        StoreRequestParty(length);
        if (sPayload[REQUEST_FLAGS_OFFSET] & KURO_REQUEST_FLAG_WAIT)
            sPreviewAnswered = TRUE;
        ShowPreview();
        return TRUE;
    }
    return FALSE;
}

static void StartPendingBattle(void)
{
    DestroyIcons();
    sHavePendingStart = FALSE;
    sFoeCount = 0;
    if (sResyncAtMenu)
    {
        sResyncAtMenu = FALSE;
        KuroPlayback_StartClientResync(sPendingStart, sPendingStartLength);
    }
    else
    {
        KuroPlayback_StartClientBattle(OP_BATTLE_START, sPendingStart, sPendingStartLength);
    }
}

// A battle with no team preview, or one whose preview has been answered, starts on the next battle frame.
static bool32 TryStartPendingBattle(void)
{
    s32 next;

    if (!sHavePendingStart)
        return FALSE;
    next = KuroLink_PeekOpcode();
    // A replay that moves on to battle events has passed team preview already.
    if (sFoeCount > 0 && !sPreviewAnswered && !(sResyncAtMenu && next >= 0x50 && next != OP_RESYNC_END))
        return FALSE;
    if (next < 0 || KuroSession_IsSessionOpcode(next) || next == KURO_FRAME_TEAM_PREVIEW || next == OP_RESYNC_BEGIN || next == OP_BATTLE_START)
        return FALSE;
    StartPendingBattle();
    return TRUE;
}

static void HandlePreviewInput(void)
{
    u32 i;
    u8 body[PARTY_SIZE] = {0};

    if (sPreviewAnswered)
        return;
    if (JOY_NEW(DPAD_RIGHT) && sCursor + 1 < sOurCount)
    {
        sCursor++;
        sDirty = TRUE;
    }
    else if (JOY_NEW(DPAD_LEFT) && sCursor > 0)
    {
        sCursor--;
        sDirty = TRUE;
    }
    else if (JOY_NEW(B_BUTTON) && sOrderCount > 0)
    {
        sOrderCount--;
        PlaySE(SE_SELECT);
        sDirty = TRUE;
    }
    else if (JOY_NEW(A_BUTTON))
    {
        for (i = 0; i < sOrderCount; i++)
            if (sOrder[i] == sCursor)
                return;
        sOrder[sOrderCount++] = sCursor;
        PlaySE(SE_SELECT);
        sDirty = TRUE;
        if (sOrderCount == sOurCount)
        {
            for (i = 0; i < sOrderCount; i++)
                body[i] = sOrder[i] + 1;
            if (KuroLink_SendFrame(KURO_OP_CHOOSE_TEAM_ORDER, body, PARTY_SIZE))
                sPreviewAnswered = TRUE;
        }
    }
}

static void HandleResultInput(void)
{
    if (JOY_NEW(DPAD_DOWN) || JOY_NEW(DPAD_UP))
    {
        s32 step = JOY_NEW(DPAD_DOWN) ? 1 : -1;

        sCursor = (sCursor + step + ARRAY_COUNT(sResultRows)) % ARRAY_COUNT(sResultRows);
        sDirty = TRUE;
    }
    else if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        if (sCursor == 0)
            SetScreen(SCREEN_MAIN);
        else if (sCursor == 1)
            StartSearch(sLastTeamSlot);
        else if (KuroLink_SendFrame(KURO_OP_SAVE_REPLAY, NULL, 0))
            sDirty = TRUE;
    }
}

static void StartSearch(u8 teamSlot)
{
    u8 body[2];

    body[0] = gKuroSession.formats[sFormatCursor].index;
    body[1] = teamSlot;
    if (teamSlot != KURO_NO_TEAM_SLOT && !KuroTeams_Upload(teamSlot))
        return;
    sLastTeamSlot = teamSlot;
    if (KuroLink_SendFrame(KURO_OP_SEARCH, body, sizeof(body)))
    {
        PlaySE(SE_SELECT);
        sShownPopup[0] = EOS;
        sSearchFrames = 0;
        sSearchTeam = teamSlot;
        SetScreen(SCREEN_SEARCHING);
    }
}

static u8 FirstTeamSlot(void)
{
    u32 i;

    for (i = 0; i < KURO_TEAM_SLOTS; i++)
        if (gKuroTeams[i].count > 0)
            return i;
    return 0;
}

static void HandleTeamPick(void)
{
    s32 step = JOY_NEW(DPAD_DOWN) ? 1 : (JOY_NEW(DPAD_UP) ? -1 : 0);
    u32 i;

    if (sShownPopup[0] != EOS)
    {
        if (JOY_NEW(A_BUTTON) || JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            sShownPopup[0] = EOS;
            sDirty = TRUE;
        }
        return;
    }
    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        SetScreen(SCREEN_FORMATS);
        return;
    }
    if (step != 0)
    {
        s32 next = sCursor;

        for (i = 0; i < KURO_TEAM_SLOTS; i++)
        {
            next = (next + step + KURO_TEAM_SLOTS) % KURO_TEAM_SLOTS;
            if (gKuroTeams[next].count > 0)
                break;
        }
        sCursor = next;
        sDirty = TRUE;
    }
    if (JOY_NEW(A_BUTTON) && gKuroTeams[sCursor].count > 0)
        StartSearch(sCursor);
}

// A team search has an answer: record the server's verdict for the team's legality badge.
static void UpdateTeamVerdict(void)
{
    if (sSearchTeam == KURO_NO_TEAM_SLOT)
        return;
    if (gKuroSession.searchState == KURO_SEARCH_MATCHED)
    {
        gKuroTeams[sSearchTeam].badge = KURO_BADGE_PASSED;
        KuroTeams_Save();
        sSearchTeam = KURO_NO_TEAM_SLOT;
    }
    else if (gKuroSession.searchState == KURO_SEARCH_REJECTED)
    {
        u8 slot = sSearchTeam;

        gKuroTeams[slot].badge = KURO_BADGE_FAILED;
        KuroTeams_Save();
        sSearchTeam = KURO_NO_TEAM_SLOT;
        SetScreen(SCREEN_TEAM_PICK);
        sCursor = slot;
    }
}

// A banner's L opens the PM behind it, on the screens that leave L free.
static void HandleBannerKey(void)
{
    if (!JOY_NEW(L_BUTTON) || !KuroChat_BannerVisible() || KuroKeyboard_IsOpen())
        return;
    if (sScreen != SCREEN_MAIN && sScreen != SCREEN_FORMATS && sScreen != SCREEN_CHAT)
        return;
    if (KuroChat_OpenLatestPm())
    {
        PlaySE(SE_SELECT);
        sScreen = SCREEN_CHAT;
    }
}

static void HandleInput(void)
{
    switch (sScreen)
    {
    case SCREEN_MAIN:
        if (JOY_NEW(DPAD_UP) || JOY_NEW(DPAD_DOWN))
        {
            sCursor = (sCursor + (JOY_NEW(DPAD_DOWN) ? 1 : ARRAY_COUNT(sMainRows) - 1)) % ARRAY_COUNT(sMainRows);
            sDirty = TRUE;
        }
        else if (JOY_NEW(A_BUTTON))
        {
            PlaySE(SE_SELECT);
            sShownPopup[0] = EOS;
            switch (sCursor)
            {
            case MAIN_FIND:
                // Poka-yoke: a guest gets the reason here instead of a refusal from the relay.
                if (gKuroSession.status == KURO_RELAY_GUEST)
                {
                    StringCopy(sShownPopup, sText_GuestSearch);
                    sDirty = TRUE;
                }
                else
                {
                    SetScreen(SCREEN_FORMATS);
                }
                break;
            case MAIN_LOBBY:
            case MAIN_PMS:
                if (gKuroSession.status == KURO_RELAY_GUEST)
                {
                    StringCopy(sShownPopup, sText_GuestChat);
                    sDirty = TRUE;
                }
                else
                {
                    u8 row = sCursor;

                    SetScreen(SCREEN_CHAT);
                    KuroChat_Open(row == MAIN_LOBBY ? KURO_CHAT_ENTRY_LOBBY : KURO_CHAT_ENTRY_PMS);
                }
                break;
            case MAIN_TEAMS:
                KuroBuilder_Open();
                SetScreen(SCREEN_BUILDER);
                break;
            case MAIN_OPTIONS:
                SetScreen(SCREEN_CHAT);
                KuroChat_Open(KURO_CHAT_ENTRY_OPTIONS);
                break;
            }
        }
        break;
    case SCREEN_ACCOUNT:
    {
        bool32 dirty = FALSE;

        if (KuroAccount_Update(&dirty) == KURO_ACCOUNT_DONE)
            SetScreen(SCREEN_MAIN);
        else if (dirty)
            sDirty = TRUE;
        break;
    }
    case SCREEN_KEYBOARD_TEST:
    {
        bool32 dirty = FALSE;

        if (KuroKeyboard_IsOpen() && KuroKeyboard_Update(&dirty) != KURO_KEYBOARD_NONE)
            dirty = TRUE;
        if (dirty)
            sDirty = TRUE;
        break;
    }
    case SCREEN_CHAT:
        switch (KuroChat_Update())
        {
        case KURO_CHAT_EXIT_HUB:
            SetScreen(SCREEN_MAIN);
            break;
        case KURO_CHAT_EXIT_ACCOUNT:
            OpenAccount(KURO_ACCOUNT_MENU);
            break;
        default:
            break;
        }
        break;
    case SCREEN_BUILDER:
        if (KuroBuilder_HandleInput())
            SetScreen(SCREEN_MAIN);
        break;
    case SCREEN_TEAM_PICK:
        HandleTeamPick();
        break;
    case SCREEN_PREVIEW:
        HandlePreviewInput();
        break;
    case SCREEN_RESULT:
        HandleResultInput();
        break;
    case SCREEN_FORMATS:
        if (JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            SetScreen(SCREEN_MAIN);
        }
        else if (JOY_NEW(DPAD_UP) && sCursor > 0)
        {
            sCursor--;
            sDirty = TRUE;
        }
        else if (JOY_NEW(DPAD_DOWN) && sCursor + 1 < gKuroSession.formatCount)
        {
            sCursor++;
            sDirty = TRUE;
        }
        else if (JOY_NEW(A_BUTTON) && gKuroSession.formatCount > 0)
        {
            sFormatCursor = sCursor;
            if (gKuroSession.formats[sCursor].flags & KURO_FORMAT_NEEDS_TEAM)
            {
                PlaySE(SE_SELECT);
                sShownPopup[0] = EOS;
                SetScreen(SCREEN_TEAM_PICK);
                sCursor = FirstTeamSlot();
            }
            else
            {
                StartSearch(KURO_NO_TEAM_SLOT);
            }
        }
        break;
    case SCREEN_SEARCHING:
        if (JOY_NEW(B_BUTTON) && KuroLink_SendFrame(KURO_OP_CANCEL_SEARCH, NULL, 0))
        {
            PlaySE(SE_SELECT);
            SetScreen(SCREEN_FORMATS);
        }
        break;
    default:
        break;
    }
}

static void CB2_KuroMenu(void)
{
    if (KuroKeyboard_IsOpen() && gMain.vblankCounter1 - sLastLoopVblank > gKuroKeyboardFrameGap)
        gKuroKeyboardFrameGap = gMain.vblankCounter1 - sLastLoopVblank;
    sLastLoopVblank = gMain.vblankCounter1;
    KuroLink_ScrubSecureTx();
    if (PumpLink())
        return;

    if (gKuroSession.popupPending)
    {
        gKuroSession.popupPending = FALSE;
        StringCopy(sShownPopup, gKuroSession.popup);
        KuroBuilder_ShowPopup(sShownPopup);
        if (sScreen == SCREEN_ACCOUNT)
            KuroAccount_ShowPopup(sShownPopup);
        sDirty = TRUE;
    }
    if (gKuroSession.status != sLastStatus || gKuroSession.formatCount != sLastFormatCount
     || gKuroSession.searchState != sLastSearchState || gKuroLink.bridgeFlags != sLastBridgeFlags)
    {
        sLastStatus = gKuroSession.status;
        sLastFormatCount = gKuroSession.formatCount;
        sLastSearchState = gKuroSession.searchState;
        sLastBridgeFlags = gKuroLink.bridgeFlags;
        sDirty = TRUE;
    }

    if (sScreen == SCREEN_SEARCHING && ++sSearchFrames % 60 == 0)
        sDirty = TRUE;

    UpdateConnection();
    UpdateTeamVerdict();
    if (!gPaletteFade.active)
    {
        HandleBannerKey();
        HandleInput();
    }
    if (sScreen == SCREEN_BUILDER && KuroBuilder_Tick())
        sDirty = TRUE;
    if (sDirty)
    {
        sDirty = FALSE;
        DrawScreen();
    }
    if (sScreen == SCREEN_CHAT)
        KuroChat_Render();
    KuroChat_BannerTick();
    if (sScreen == SCREEN_CHAT && !KuroKeyboard_IsOpen())
    {
        if (gMain.vblankCounter1 - sChatLoopVblank > gKuroChatFrameGap && sChatLoopVblank != 0)
            gKuroChatFrameGap = gMain.vblankCounter1 - sChatLoopVblank;
        sChatLoopVblank = gMain.vblankCounter1;
    }
    else
    {
        sChatLoopVblank = 0;
    }

    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    RunTextPrinters();
    UpdatePaletteFade();
}

void CB2_KuroMenuStart(void)
{
    SetVBlankCallback(NULL);
    SetHBlankCallback(NULL);
    SetGpuReg(REG_OFFSET_DISPCNT, 0);
    ScanlineEffect_Stop();
    DmaClearLarge16(3, (void *)VRAM, VRAM_SIZE, 0x1000);
    DmaClear32(3, OAM, OAM_SIZE);
    DmaClear16(3, PLTT, PLTT_SIZE);
    ResetPaletteFade();
    ResetTasks();
    ResetSpriteData();
    FreeAllSpritePalettes();
    FreeAllWindowBuffers();

    ResetBgsAndClearDma3BusyFlags(0);
    InitBgsFromTemplates(0, sBgTemplates, ARRAY_COUNT(sBgTemplates));
    SetBgTilemapBuffer(0, AllocZeroed(BG_SCREEN_SIZE));
    InitWindows(sWindowTemplates);
    DeactivateAllTextPrinters();

    LoadPalette(GetOverworldTextboxPalettePtr(), BG_PLTT_ID(BORDER_PALETTE), PLTT_SIZE_4BPP);
    LoadUserWindowBorderGfx(WINDOW_MAIN, BORDER_BASE_TILE, BG_PLTT_ID(BORDER_PALETTE));
    Menu_LoadStdPalAt(BG_PLTT_ID(TEXT_PALETTE));
    PutWindowTilemap(WINDOW_MAIN);
    DrawStdFrameWithCustomTileAndPalette(WINDOW_MAIN, FALSE, BORDER_BASE_TILE, BORDER_PALETTE);
    CopyBgTilemapBufferToVram(0);

    SetGpuReg(REG_OFFSET_BLDCNT, 0);
    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_0 | DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON | DISPCNT_BG0_ON | DISPCNT_BG1_ON);
    ShowBg(0);
    ShowBg(1);

    KuroChat_MenuActive(TRUE);
    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
    SetVBlankCallback(VBlankCB_KuroMenu);

    if (!sTeamsLoaded)
    {
        KuroTeams_Load();
        sTeamsLoaded = TRUE;
        sSearchTeam = KURO_NO_TEAM_SLOT;
    }
    if (gKuroSession.resultPending)
    {
        gKuroSession.resultPending = FALSE;
        sScreen = SCREEN_RESULT;
    }
    else if (gKuroBootMode == KURO_BOOT_KEYBOARD_TEST)
    {
        struct KuroKeyboardOptions options = {.title = sText_KeyboardTest, .text = sKeyboardText, .limit = KURO_KEYBOARD_MAX_LENGTH, .predictive = TRUE, .learn = TRUE};

        sKeyboardText[0] = EOS;
        KuroKeyboard_Open(&options);
        sScreen = SCREEN_KEYBOARD_TEST;
    }
    else if (KuroBuilder_IsOpen())
        sScreen = SCREEN_BUILDER;
    else
        sScreen = gKuroSession.handshakeDone && !gKuroSession.mismatch ? SCREEN_MAIN : SCREEN_CONNECTING;
    sCursor = 0;
    sDirty = TRUE;
    sShownPopup[0] = EOS;
    sLastLoopVblank = gMain.vblankCounter1;
    SetMainCallback2(CB2_KuroMenu);
}
