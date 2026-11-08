// The client's front-end screens: connection, main menu, format list and search.
#include "global.h"
#include "bg.h"
#include "constants/characters.h"
#include "gpu_regs.h"
#include "kuro_link.h"
#include "kuro_playback.h"
#include "kuro_session.h"
#include "main.h"
#include "malloc.h"
#include "menu.h"
#include "palette.h"
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
};

#define WINDOW_MAIN 0
#define LINE_HEIGHT 16
#define BORDER_BASE_TILE 0x200
#define BORDER_PALETTE 14
#define TEXT_PALETTE 15

static const struct BgTemplate sBgTemplates[] =
{
    {.bg = 0, .charBaseIndex = 2, .mapBaseIndex = 31, .screenSize = 0, .paletteMode = 0, .priority = 0, .baseTile = 0},
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
static EWRAM_DATA u8 sShownPopup[120] = {0};
static EWRAM_DATA u16 sSearchFrames = 0;

EWRAM_DATA volatile u8 gKuroUiScreen = 0;

static const u8 sText_Title[] = _("KURO");
static const u8 sText_NoBridge[] = _("Bridge not attached.\nLoad the KURO script in mGBA.");
static const u8 sText_NoRelay[] = _("Relay not running.\nStart KURO Relay on this PC.");
static const u8 sText_Waiting[] = _("Connecting to the relay…");
static const u8 sText_MismatchTitle[] = _("Relay and ROM don't match.\nUpdate both to the same version.\n");
static const u8 sText_RomLabel[] = _("ROM: ");
static const u8 sText_RelayLabel[] = _("\nRelay: ");
static const u8 sText_SignedInAs[] = _("Signed in as ");
static const u8 sText_FindBattle[] = _("Find Battle");
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
        StringCopy(buffer, sText_SignedInAs);
        StringAppend(buffer, gKuroSession.username);
        PrintLine(buffer, 0);
        StringCopy(buffer, sText_Cursor);
        StringAppend(buffer, sText_FindBattle);
        PrintLine(buffer, LINE_HEIGHT * 2);
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

static void SetScreen(u8 screen)
{
    sScreen = screen;
    sCursor = 0;
    sDirty = TRUE;
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
        SetScreen(SCREEN_MAIN);
    else if (sScreen != SCREEN_CONNECTING && !(gKuroLink.bridgeFlags & 2))
        SetScreen(SCREEN_CONNECTING);
}

// Returns TRUE when a battle has begun and this screen has handed over.
static bool32 PumpLink(void)
{
    u8 opcode;
    u16 length;

    while (KuroLink_ReadFrame(&opcode, sPayload, &length) == KURO_LINK_READ_OK)
    {
        if (opcode >= KURO_FIRST_BATTLE_OPCODE)
        {
            KuroPlayback_StartClientBattle(opcode, sPayload, length);
            return TRUE;
        }
        KuroSession_HandleFrame(opcode, sPayload, length);
    }
    return FALSE;
}

static void HandleInput(void)
{
    u8 body[2];

    switch (sScreen)
    {
    case SCREEN_MAIN:
        if (JOY_NEW(A_BUTTON))
        {
            PlaySE(SE_SELECT);
            SetScreen(SCREEN_FORMATS);
        }
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
            body[0] = gKuroSession.formats[sCursor].index;
            body[1] = 0xFF;
            if (KuroLink_SendFrame(KURO_OP_SEARCH, body, sizeof(body)))
            {
                PlaySE(SE_SELECT);
                sShownPopup[0] = EOS;
                sSearchFrames = 0;
                SetScreen(SCREEN_SEARCHING);
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
    if (PumpLink())
        return;

    if (gKuroSession.popupPending)
    {
        gKuroSession.popupPending = FALSE;
        StringCopy(sShownPopup, gKuroSession.popup);
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
    if (!gPaletteFade.active)
        HandleInput();
    if (sDirty)
    {
        sDirty = FALSE;
        DrawScreen();
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
    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_0 | DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON | DISPCNT_BG0_ON);
    ShowBg(0);

    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
    SetVBlankCallback(VBlankCB_KuroMenu);

    sScreen = gKuroSession.handshakeDone && !gKuroSession.mismatch ? SCREEN_MAIN : SCREEN_CONNECTING;
    sCursor = 0;
    sDirty = TRUE;
    sShownPopup[0] = EOS;
    SetMainCallback2(CB2_KuroMenu);
}
