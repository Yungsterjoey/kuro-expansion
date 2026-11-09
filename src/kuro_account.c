// Sign-in, the account menu, avatars and ratings. The relay does the real login and keeps the password.
#include "global.h"
#include "constants/characters.h"
#include "constants/songs.h"
#include "kuro_account.h"
#include "kuro_keyboard.h"
#include "kuro_link.h"
#include "kuro_playback.h"
#include "kuro_session.h"
#include "main.h"
#include "menu.h"
#include "strings.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "text.h"
#include "trainer_pokemon_sprites.h"
#include "window.h"

enum AccountState
{
    AS_CLOSED,
    AS_LIST,
    AS_USERNAME,
    AS_PASSWORD,
    AS_WAIT,
    AS_REMEMBER,
    AS_MESSAGE,
    AS_REGISTER,
    AS_MENU,
    AS_CONFIRM_FORGET,
    AS_AVATARS,
    AS_RATINGS,
};

enum MenuRow
{
    MENU_SWITCH,
    MENU_SIGN_OUT,
    MENU_FORGET,
    MENU_AVATAR,
    MENU_RATINGS,
    MENU_BACK,
    MENU_COUNT,
};

enum ListExtra
{
    LIST_ADD,
    LIST_GUEST,
    LIST_REGISTER,
    LIST_EXTRA_COUNT,
};

// Where a message goes when it is dismissed.
enum AfterMessage
{
    AFTER_LIST,
    AFTER_USERNAME,
    AFTER_PASSWORD,
};

#define LINE_HEIGHT 16
#define VISIBLE_LIST_ROWS 7
#define SPINNER_FRAMES 15
#define AVATARS_PER_PAGE 3
#define AVATAR_SPACING 64
#define AVATAR_Y 72
#define AVATAR_BASE_X 56

static EWRAM_DATA u8 sState = AS_CLOSED;
static EWRAM_DATA u8 sCursor = 0;
static EWRAM_DATA u8 sTop = 0;
static EWRAM_DATA u8 sAfterMessage = AFTER_LIST;
static EWRAM_DATA u8 sSpinner = 0;
static EWRAM_DATA bool8 sHasWayBack = FALSE;
static EWRAM_DATA bool8 sTypedLogin = FALSE;
static EWRAM_DATA bool8 sSpritesUp = FALSE;
static EWRAM_DATA u8 sAvatarPage = 0;
static EWRAM_DATA u16 sAvatarSprites[AVATARS_PER_PAGE] = {0};
static EWRAM_DATA u8 sUsername[KURO_USERNAME_SIZE] = {0};
static EWRAM_DATA u8 sMessage[120] = {0};

static const u8 sText_ChooseAccount[] = _("Choose an account");
static const u8 sText_AddAccount[] = _("Add account");
static const u8 sText_Guest[] = _("Play as guest");
static const u8 sText_Register[] = _("How to register");
static const u8 sText_Username[] = _("Username");
static const u8 sText_Password[] = _("Password");
static const u8 sText_SigningIn[] = _("Signing in");
static const u8 sText_Dot[] = _(".");
static const u8 sText_Remember[] = _("Remember this account\non this PC?\n{RIGHT_ARROW} A: Yes    B: No");
static const u8 sText_RegisterHelp[] = _("Register on the website, on your\nPC, then add the account here.\nA: show the address on the PC\nB: back");
static const u8 sText_PressA[] = _("\nPress A to continue.");
static const u8 sText_Cursor[] = _("{RIGHT_ARROW} ");
static const u8 sText_Indent[] = _("  ");
static const u8 sText_AccountTitle[] = _("Account: ");
static const u8 sText_ConfirmForget[] = _("Forget this account on this PC?\nA: forget    B: keep");
static const u8 sText_ChooseAvatar[] = _("Choose an avatar");
static const u8 sText_NoAvatars[] = _("Waiting for the avatar list…");
static const u8 sText_RatingsTitle[] = _("Ratings");
static const u8 sText_NoRatings[] = _("No ladder games yet.");
static const u8 sText_Colon[] = _(": ");
static const u8 sText_MenuSwitch[] = _("Switch account");
static const u8 sText_MenuSignOut[] = _("Sign out");
static const u8 sText_MenuForget[] = _("Forget account");
static const u8 sText_MenuAvatar[] = _("Avatar");
static const u8 sText_MenuRatings[] = _("Ratings");
static const u8 sText_MenuBack[] = _("Back");
static const u8 *const sMenuRows[MENU_COUNT] =
{
    [MENU_SWITCH] = sText_MenuSwitch,
    [MENU_SIGN_OUT] = sText_MenuSignOut,
    [MENU_FORGET] = sText_MenuForget,
    [MENU_AVATAR] = sText_MenuAvatar,
    [MENU_RATINGS] = sText_MenuRatings,
    [MENU_BACK] = sText_MenuBack,
};

static void DestroyAvatars(void)
{
    u32 i;

    if (!sSpritesUp)
        return;
    for (i = 0; i < AVATARS_PER_PAGE; i++)
        if (sAvatarSprites[i] != 0xFFFF)
            FreeAndDestroyTrainerPicSprite(sAvatarSprites[i]);
    sSpritesUp = FALSE;
}

static void GoTo(u8 state)
{
    DestroyAvatars();
    sState = state;
    sCursor = 0;
    sTop = 0;
}

// A result from a silent sign-in after a reconnect must not answer this request.
static void GoToWait(void)
{
    gKuroSession.loginResultPending = FALSE;
    sSpinner = 0;
    GoTo(AS_WAIT);
}

static void OpenUsernameKeyboard(void)
{
    struct KuroKeyboardOptions options = {.title = sText_Username, .text = sUsername, .limit = KURO_USERNAME_SIZE - 2};

    GoTo(AS_USERNAME);
    KuroKeyboard_Open(&options);
}

static void OpenPasswordKeyboard(void)
{
    struct KuroKeyboardOptions options = {.title = sText_Password, .secure = TRUE};

    GoTo(AS_PASSWORD);
    KuroKeyboard_Open(&options);
}

static bool32 SendEmpty(u8 opcode)
{
    return KuroLink_SendFrame(opcode, NULL, 0);
}

static void OpenList(void)
{
    gKuroSession.accountListReady = FALSE;
    SendEmpty(KURO_OP_ACCOUNT_LIST_REQ);
    GoTo(AS_LIST);
}

void KuroAccount_Open(enum KuroAccountEntry entry)
{
    sHasWayBack = (entry == KURO_ACCOUNT_MENU);
    if (entry == KURO_ACCOUNT_MENU)
        GoTo(AS_MENU);
    else
        OpenList();
}

// The link went away: whatever was being typed is gone with it.
void KuroAccount_Abort(void)
{
    KuroKeyboard_Close();
    GoTo(AS_CLOSED);
    memset(sMessage, 0, sizeof(sMessage));
    sMessage[0] = EOS;
}

void KuroAccount_ShowPopup(const u8 *text)
{
    if (sState == AS_REGISTER || sState == AS_MENU || sState == AS_AVATARS)
    {
        StringCopyN(sMessage, text, sizeof(sMessage) - 1);
        sMessage[sizeof(sMessage) - 1] = EOS;
        sAfterMessage = AFTER_LIST;
        GoTo(AS_MESSAGE);
    }
}

static void ShowMessage(const u8 *text, u8 after)
{
    u8 *end = StringCopyN(sMessage, text, sizeof(sMessage) - 22);

    *end = EOS;
    StringAppend(sMessage, sText_PressA);
    sAfterMessage = after;
    GoTo(AS_MESSAGE);
}

static u32 ListLength(void)
{
    return gKuroSession.accountCount + LIST_EXTRA_COUNT;
}

static void DrawLine(u32 windowId, const u8 *text, u32 y)
{
    AddTextPrinterParameterized(windowId, FONT_NORMAL, text, 0, y, TEXT_SKIP_DRAW, NULL);
}

static void DrawRow(u32 windowId, const u8 *text, bool32 selected, u32 y)
{
    u8 line[60];

    StringCopy(line, selected ? sText_Cursor : sText_Indent);
    StringAppend(line, text);
    DrawLine(windowId, line, y);
}

static const u8 *ListRowText(u32 row)
{
    if (row < gKuroSession.accountCount)
        return gKuroSession.accountNames[row];
    switch (row - gKuroSession.accountCount)
    {
    case LIST_ADD: return sText_AddAccount;
    case LIST_GUEST: return sText_Guest;
    default: return sText_Register;
    }
}

static void ShowAvatarPage(void)
{
    u32 i;

    DestroyAvatars();
    for (i = 0; i < AVATARS_PER_PAGE; i++)
    {
        u32 index = sAvatarPage * AVATARS_PER_PAGE + i;

        if (index < gKuroSession.avatarCount)
        {
            sAvatarSprites[i] = CreateTrainerPicSprite(gKuroSession.avatars[index], TRUE, AVATAR_BASE_X + i * AVATAR_SPACING, AVATAR_Y, i, TAG_NONE);
            gSprites[sAvatarSprites[i]].oam.priority = 0;
        }
        else
        {
            sAvatarSprites[i] = 0xFFFF;
        }
    }
    sSpritesUp = TRUE;
}

static const u8 *FormatName(u32 index)
{
    u32 i;

    for (i = 0; i < gKuroSession.formatCount; i++)
        if (gKuroSession.formats[i].index == index)
            return gKuroSession.formats[i].name;
    return gText_EmptyString2;
}

static void DrawRatings(u32 windowId)
{
    u8 line[60];
    u32 i;

    DrawLine(windowId, sText_RatingsTitle, 0);
    if (gKuroSession.ratingCount == 0)
        DrawLine(windowId, sText_NoRatings, LINE_HEIGHT * 2);
    for (i = 0; i < gKuroSession.ratingCount; i++)
    {
        u8 *end = StringCopy(line, FormatName(gKuroSession.ratings[i].formatIndex));

        end = StringAppend(end, sText_Colon);
        ConvertIntToDecimalStringN(end, gKuroSession.ratings[i].elo, STR_CONV_MODE_LEFT_ALIGN, 4);
        DrawLine(windowId, line, LINE_HEIGHT * (2 + i));
    }
}

void KuroAccount_Draw(u32 windowId)
{
    u8 line[60];
    u32 i;

    gKuroUiScreen = KURO_UI_ACCOUNT_BASE + sState;
    FillWindowPixelBuffer(windowId, PIXEL_FILL(1));
    switch (sState)
    {
    case AS_LIST:
        DrawLine(windowId, sText_ChooseAccount, 0);
        for (i = 0; i < VISIBLE_LIST_ROWS && sTop + i < ListLength(); i++)
            DrawRow(windowId, ListRowText(sTop + i), sTop + i == sCursor, LINE_HEIGHT * (i + 1));
        break;
    case AS_WAIT:
    {
        u8 *end = StringCopy(line, sText_SigningIn);

        for (i = 0; i < sSpinner % 4; i++)
            end = StringAppend(end, sText_Dot);
        DrawLine(windowId, line, 0);
        break;
    }
    case AS_REMEMBER:
        DrawLine(windowId, sText_Remember, 0);
        break;
    case AS_MESSAGE:
        DrawLine(windowId, sMessage, 0);
        break;
    case AS_REGISTER:
        DrawLine(windowId, sText_RegisterHelp, 0);
        break;
    case AS_MENU:
        StringCopy(line, sText_AccountTitle);
        StringAppend(line, gKuroSession.username);
        DrawLine(windowId, line, 0);
        for (i = 0; i < MENU_COUNT; i++)
            DrawRow(windowId, sMenuRows[i], i == sCursor, LINE_HEIGHT * (i + 1));
        break;
    case AS_CONFIRM_FORGET:
        DrawLine(windowId, sText_ConfirmForget, 0);
        break;
    case AS_AVATARS:
        DrawLine(windowId, gKuroSession.avatarListReady ? sText_ChooseAvatar : sText_NoAvatars, 0);
        if (gKuroSession.avatarListReady)
            AddTextPrinterParameterized(windowId, FONT_NORMAL, sText_Cursor, 8 + (sCursor % AVATARS_PER_PAGE) * AVATAR_SPACING, LINE_HEIGHT * 6, TEXT_SKIP_DRAW, NULL);
        break;
    case AS_RATINGS:
        DrawRatings(windowId);
        break;
    }
}

static void Finish(bool32 *dirty, enum KuroAccountResult *result)
{
    GoTo(AS_CLOSED);
    *result = KURO_ACCOUNT_DONE;
    *dirty = TRUE;
}

static void BeginPasswordEntry(void)
{
    KuroLink_SendFrame(KURO_OP_LOGIN_BEGIN, sUsername, StringLength(sUsername) + 1);
    OpenPasswordKeyboard();
}

static void HandleKeyboardResult(enum KuroKeyboardResult result)
{
    if (sState == AS_USERNAME)
    {
        if (result == KURO_KEYBOARD_SENT)
            BeginPasswordEntry();
        else
            OpenList();
    }
    else if (result == KURO_KEYBOARD_SENT)
    {
        sTypedLogin = TRUE;
        GoToWait();
    }
    else
    {
        OpenUsernameKeyboard();
    }
}

static void HandleLoginResult(bool32 *dirty, enum KuroAccountResult *result)
{
    gKuroSession.loginResultPending = FALSE;
    *dirty = TRUE;
    switch (gKuroSession.loginCode)
    {
    case KURO_LOGIN_OK:
        if (sTypedLogin && gKuroSession.accountStoreAvailable)
            GoTo(AS_REMEMBER);
        else
            Finish(dirty, result);
        break;
    case KURO_LOGIN_WRONG_PASSWORD:
    case KURO_LOGIN_NEEDS_PASSWORD:
        ShowMessage(gKuroSession.loginText, AFTER_PASSWORD);
        break;
    case KURO_LOGIN_NAME_TAKEN:
        ShowMessage(gKuroSession.loginText, AFTER_USERNAME);
        break;
    default:
        ShowMessage(gKuroSession.loginText, AFTER_LIST);
        break;
    }
}

static void MoveCursor(u32 length, bool32 *dirty)
{
    s32 step = JOY_NEW(DPAD_DOWN) ? 1 : (JOY_NEW(DPAD_UP) ? -1 : 0);

    if (step == 0 || length == 0)
        return;
    sCursor = (sCursor + step + length) % length;
    if (sCursor < sTop)
        sTop = sCursor;
    else if (sCursor >= sTop + VISIBLE_LIST_ROWS)
        sTop = sCursor - VISIBLE_LIST_ROWS + 1;
    *dirty = TRUE;
}

static void PickListRow(void)
{
    u8 index = sCursor;

    sTypedLogin = FALSE;
    if (index < gKuroSession.accountCount)
    {
        if (KuroLink_SendFrame(KURO_OP_LOGIN_SAVED, &index, 1))
        {
            GoToWait();
        }
        return;
    }
    switch (index - gKuroSession.accountCount)
    {
    case LIST_ADD:
        sUsername[0] = EOS;
        OpenUsernameKeyboard();
        break;
    case LIST_GUEST:
        if (SendEmpty(KURO_OP_LOGIN_GUEST))
        {
            GoToWait();
        }
        break;
    default:
        GoTo(AS_REGISTER);
        break;
    }
}

static void PickMenuRow(enum KuroAccountResult *result, bool32 *dirty)
{
    u8 forget = 0;

    switch (sCursor)
    {
    case MENU_SWITCH:
        OpenList();
        break;
    case MENU_SIGN_OUT:
        if (KuroLink_SendFrame(KURO_OP_LOGOUT, &forget, 1))
        {
            sHasWayBack = FALSE;
            OpenList();
        }
        break;
    case MENU_FORGET:
        GoTo(AS_CONFIRM_FORGET);
        break;
    case MENU_AVATAR:
        gKuroSession.avatarListReady = FALSE;
        SendEmpty(KURO_OP_AVATAR_LIST_REQ);
        sAvatarPage = 0;
        GoTo(AS_AVATARS);
        break;
    case MENU_RATINGS:
        SendEmpty(KURO_OP_RATINGS_REQ);
        GoTo(AS_RATINGS);
        break;
    default:
        Finish(dirty, result);
        break;
    }
}

static void HandleAvatars(bool32 *dirty)
{
    u32 count = gKuroSession.avatarCount;

    if (!gKuroSession.avatarListReady || count == 0)
        return;
    if (!sSpritesUp)
    {
        ShowAvatarPage();
        *dirty = TRUE;
    }
    if (JOY_NEW(DPAD_LEFT) || JOY_NEW(DPAD_RIGHT))
    {
        u32 page = sAvatarPage;

        sCursor = (sCursor + (JOY_NEW(DPAD_LEFT) ? count - 1 : 1)) % count;
        sAvatarPage = sCursor / AVATARS_PER_PAGE;
        if (page != sAvatarPage)
            ShowAvatarPage();
        *dirty = TRUE;
    }
    else if (JOY_NEW(A_BUTTON))
    {
        u8 body[2];

        body[0] = gKuroSession.avatars[sCursor] & 0xFF;
        body[1] = gKuroSession.avatars[sCursor] >> 8;
        if (KuroLink_SendFrame(KURO_OP_SET_AVATAR, body, sizeof(body)))
        {
            PlaySE(SE_SELECT);
            gKuroSession.currentAvatar = gKuroSession.avatars[sCursor];
            GoTo(AS_MENU);
            *dirty = TRUE;
        }
    }
}

enum KuroAccountResult KuroAccount_Update(bool32 *dirty)
{
    enum KuroAccountResult result = KURO_ACCOUNT_STAY;
    bool32 accept = JOY_NEW(A_BUTTON), cancel = JOY_NEW(B_BUTTON);
    u8 flag;

    if (KuroKeyboard_IsOpen())
    {
        enum KuroKeyboardResult keyboard = KuroKeyboard_Update(dirty);

        if (keyboard != KURO_KEYBOARD_NONE)
        {
            HandleKeyboardResult(keyboard);
            *dirty = TRUE;
        }
        return KURO_ACCOUNT_STAY;
    }

    if (sState != AS_AVATARS && (accept || cancel))
        PlaySE(SE_SELECT);

    switch (sState)
    {
    case AS_LIST:
        if (gKuroSession.accountListReady)
        {
            gKuroSession.accountListReady = FALSE;
            *dirty = TRUE;
        }
        MoveCursor(ListLength(), dirty);
        if (accept)
        {
            PickListRow();
            *dirty = TRUE;
        }
        else if (cancel && sHasWayBack)
        {
            GoTo(AS_MENU);
            *dirty = TRUE;
        }
        break;
    case AS_WAIT:
        if (gKuroSession.loginResultPending)
        {
            HandleLoginResult(dirty, &result);
        }
        else if (gMain.vblankCounter1 % SPINNER_FRAMES == 0)
        {
            sSpinner++;
            *dirty = TRUE;
        }
        break;
    case AS_REMEMBER:
        if (accept || cancel)
        {
            flag = accept;
            if (KuroLink_SendFrame(KURO_OP_LOGIN_REMEMBER, &flag, 1))
                Finish(dirty, &result);
        }
        break;
    case AS_MESSAGE:
        if (accept || cancel)
        {
            if (sAfterMessage == AFTER_PASSWORD)
                BeginPasswordEntry();
            else if (sAfterMessage == AFTER_USERNAME)
                OpenUsernameKeyboard();
            else if (sHasWayBack && gKuroSession.status == KURO_RELAY_LOGGED_IN)
                GoTo(AS_MENU);
            else
                OpenList();
            *dirty = TRUE;
        }
        break;
    case AS_REGISTER:
        if (accept)
        {
            SendEmpty(KURO_OP_OPEN_REGISTER);
        }
        else if (cancel)
        {
            OpenList();
            *dirty = TRUE;
        }
        break;
    case AS_MENU:
        MoveCursor(MENU_COUNT, dirty);
        if (accept)
        {
            PickMenuRow(&result, dirty);
            *dirty = TRUE;
        }
        else if (cancel)
        {
            Finish(dirty, &result);
        }
        break;
    case AS_CONFIRM_FORGET:
        if (accept)
        {
            flag = 1;
            if (KuroLink_SendFrame(KURO_OP_LOGOUT, &flag, 1))
            {
                sHasWayBack = FALSE;
                OpenList();
                *dirty = TRUE;
            }
        }
        else if (cancel)
        {
            GoTo(AS_MENU);
            *dirty = TRUE;
        }
        break;
    case AS_AVATARS:
        if (cancel)
        {
            PlaySE(SE_SELECT);
            GoTo(AS_MENU);
            *dirty = TRUE;
            break;
        }
        HandleAvatars(dirty);
        break;
    case AS_RATINGS:
        if (accept || cancel)
        {
            GoTo(AS_MENU);
            *dirty = TRUE;
        }
        else if (gMain.vblankCounter1 % 30 == 0)
        {
            *dirty = TRUE;
        }
        break;
    }
    return result;
}
