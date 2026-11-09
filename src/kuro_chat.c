// Chat rooms, PMs, room and user lists, banners, quick chat and in-battle chat text.
// Rows are drawn only when they change, within a per-frame budget, and the eight chat windows
// rotate instead of redrawing when the view scrolls.
#include "global.h"
#include "bg.h"
#include "constants/characters.h"
#include "constants/rgb.h"
#include "constants/songs.h"
#include "gpu_regs.h"
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
#include "sound.h"
#include "string_util.h"
#include "strings.h"
#include "reshow_battle_screen.h"
#include "scanline_effect.h"
#include "sprite.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "window.h"

#define CHAT_VIEW_ROWS 8
#define CHAT_ROW_WIDTH_TILES 30
#define CHAT_ROW_HEIGHT_TILES 2
#define CHAT_FIRST_ROW_TILE 2 // Tile row of the first chat line; the header takes rows 0 and 1.
#define CHAT_FOOTER_TILE 18
#define CHAT_PALETTE_FIRST 4 // BG palette slots, one per chat row window.
#define CHAT_PALETTE_NAME_SLOT 4
#define CHAT_PALETTE_GREY_SLOT 5
#define CHAT_BASE_TILE_ROWS 1
#define CHAT_BASE_TILE_HEADER 528
#define CHAT_BASE_TILE_FOOTER 588
#define CHAT_ROW_PIXEL_X 2
#define RENDER_ROW_BUDGET 2
#define KEYBOARD_LIMIT 120
#define BANNER_FRAMES 180
#define BANNER_SLIDE_SPEED 2
#define BANNER_HEIGHT_PIXELS 16
#define MAX_LISTED_ROOMS 32
#define MAX_LISTED_USERS 32
#define ROOM_ID_SIZE 21
#define LIST_VISIBLE_ROWS 7
#define LIST_ROW_HEIGHT 16
#define BUBBLE_FRAMES 180
#define BATTLE_LOG_LINES 12
#define BATTLE_LOG_LINE_SIZE 56

enum ChatScreen
{
    CHAT_SCREEN_CLOSED,
    CHAT_SCREEN_CHAT,
    CHAT_SCREEN_ROOMS,
    CHAT_SCREEN_USERS,
    CHAT_SCREEN_USER_MENU,
    CHAT_SCREEN_PMS,
    CHAT_SCREEN_ROOM_MENU,
    CHAT_SCREEN_OPTIONS,
    CHAT_SCREEN_QUICK_EDIT,
};

enum KeyboardTarget
{
    KEYBOARD_CHAT,
    KEYBOARD_PM_NAME,
    KEYBOARD_PM_TEXT,
    KEYBOARD_QUICK_EDIT,
};

enum RoomMenuRow
{
    ROOM_MENU_USERS,
    ROOM_MENU_ROOMS,
    ROOM_MENU_LEAVE,
    ROOM_MENU_BACK,
    ROOM_MENU_COUNT,
};

enum PmMenuRow
{
    PM_MENU_IGNORE,
    PM_MENU_REPORT,
    PM_MENU_BACK,
    PM_MENU_COUNT,
};

enum OptionsRow
{
    OPTIONS_ACCOUNT,
    OPTIONS_QUICK,
    OPTIONS_MUTE,
    OPTIONS_HIDE_LOBBY,
    OPTIONS_PROFANITY,
    OPTIONS_BACK,
    OPTIONS_COUNT,
};

// sChatPalette[i] is the RGB555 value of the relay's CHAT_PALETTE[i] (relay/src/colours.ts).
static const u16 sChatPalette[KURO_CHAT_PALETTE_SIZE] =
{
    0x0015, 0x0118, 0x0194, 0x0210, 0x0227, 0x2200, 0x4200, 0x59C0,
    0x6100, 0x60C8, 0x588E, 0x5014, 0x3818, 0x28D4, 0x294A, 0x40C6,
};

struct ChatRow
{
    u8 kind;
    u8 colour;
    u8 nameLength; // Bytes of text drawn in the name colour, rank symbol included.
    u8 text[KURO_CHAT_ROW_TEXT];
};

struct Conversation
{
    bool8 used;
    bool8 isPm;
    u8 head;
    u8 count;
    u8 unread;
    u16 users;
    u8 title[KURO_CHAT_TITLE_SIZE];
};

struct RoomEntry
{
    u8 id[ROOM_ID_SIZE];
    u8 title[KURO_CHAT_TITLE_SIZE];
    u16 users;
    bool8 official;
};

struct UserEntry
{
    u8 rank;
    u8 status;
    u8 name[KURO_CHAT_NAME_SIZE];
};

static EWRAM_DATA struct Conversation sConvs[KURO_CHAT_CONVERSATIONS] = {0};
static EWRAM_DATA struct ChatRow sRoomRows[KURO_CHAT_ROOMS][KURO_CHAT_ROOM_ROWS] = {0};
static EWRAM_DATA struct ChatRow sPmRows[KURO_CHAT_PMS][KURO_CHAT_PM_ROWS] = {0};
static EWRAM_DATA struct RoomEntry sRooms[MAX_LISTED_ROOMS] = {0};
static EWRAM_DATA u8 sRoomCount = 0;
static EWRAM_DATA bool8 sRoomListComplete = FALSE;
static EWRAM_DATA struct UserEntry sUsers[MAX_LISTED_USERS] = {0};
static EWRAM_DATA u8 sUserCount = 0;
static EWRAM_DATA bool8 sUserListComplete = FALSE;
static EWRAM_DATA u16 sUsersOnline = 0;
static EWRAM_DATA u8 sLastUnreadPm = 0; // Conversation + 1; 0 means none.

EWRAM_DATA u8 gKuroQuickChat[KURO_QUICK_PHRASES][KURO_QUICK_PHRASE_SIZE] = {0};
EWRAM_DATA u8 gKuroChatFlags = 0;
EWRAM_DATA volatile u8 gKuroChatFrameGap = 0;

// Screen state: reset by KuroChat_Open.
static EWRAM_DATA struct
{
    u8 screen;
    u8 conv;
    u8 cursor;
    u8 top;
    u8 scrollOffset; // Rows above the newest row at the bottom of the view.
    u8 quickActive;
    u8 quickIndex;
    u8 userMenuCursor;
    u8 optionsCursor;
    u8 keyboardTarget;
    bool8 windowsUp;
    bool8 mainHidden;
    u8 rowWindows[CHAT_VIEW_ROWS];
    u8 headerWindow;
    u8 footerWindow;
    u8 windowOrder[CHAT_VIEW_ROWS]; // windowOrder[i]: the window on visual row i.
    u8 dirtyRows;
    bool8 headerDirty;
    bool8 footerDirty;
    u8 lastLoopVblank;
    u32 lastFrame;
    u8 message[80];
    u8 menuKind;
    u8 joining;
    bool8 needUserList;
} sCh = {0};

static EWRAM_DATA u8 sDraft[KEYBOARD_LIMIT + 1] = {0};
static EWRAM_DATA u8 sDraftConv = 0; // Conversation + 1; 0 means none.
static EWRAM_DATA u8 sPmTarget[KURO_CHAT_NAME_SIZE] = {0};
static EWRAM_DATA u8 sQuickEditBuffer[KURO_QUICK_PHRASE_SIZE] = {0};
static EWRAM_DATA u8 sPlainBuffer[KURO_CHAT_ROW_TEXT + 1] = {0};

// Banner state.
static EWRAM_DATA struct
{
    bool8 active;
    bool8 pending;
    u8 kind;
    u8 phase;
    s8 offset;
    u16 timer;
    u8 text[80];
    u8 windowId;
    bool8 menuUp;
} sBanner = {0};

// In-battle chat, read by the battle screen. Survives the menu loop.
static EWRAM_DATA u8 sBattleLog[BATTLE_LOG_LINES][BATTLE_LOG_LINE_SIZE] = {0};
static EWRAM_DATA u8 sBattleLogHead = 0;
static EWRAM_DATA u8 sBattleLogCount = 0;
static EWRAM_DATA u8 sBubbleText[BATTLE_LOG_LINE_SIZE] = {0};
static EWRAM_DATA bool8 sBubblePending = FALSE;
static EWRAM_DATA bool8 sMutedThisBattle = FALSE;

static const u8 sText_ChatHelp[] = _("A: write  START: menu  L/R: rooms  SELECT: quick");
static const u8 sText_Joining[] = _("Joining the room…");
static const u8 sText_WaitingList[] = _("Waiting for the list…");
static const u8 sText_NoRooms[] = _("No rooms open.");
static const u8 sText_RoomsTitle[] = _("Rooms");
static const u8 sText_UsersTitle[] = _("Users in ");
static const u8 sText_PmsTitle[] = _("Private messages");
static const u8 sText_NewPm[] = _("New message…");
static const u8 sText_NoPms[] = _("No messages yet.");
static const u8 sText_Away[] = _(" (away)");
static const u8 sText_Official[] = _("{KURO_ASTERISK} ");
static const u8 sText_PersonIndent[] = _("  ");
static const u8 sText_OnlineCount[] = _(" users");
static const u8 sText_MenuUsers[] = _("Users");
static const u8 sText_MenuRooms[] = _("Room list");
static const u8 sText_MenuLeave[] = _("Leave room");
static const u8 sText_MenuBack[] = _("Back");
static const u8 sText_MenuIgnore[] = _("Ignore");
static const u8 sText_MenuReport[] = _("Report on website");
static const u8 sText_MenuPm[] = _("Send a message");
static const u8 sText_OptionsTitle[] = _("Options");
static const u8 sText_OptionAccount[] = _("Account");
static const u8 sText_OptionQuick[] = _("Quick chat phrases");
static const u8 sText_OptionMute[] = _("Mute battle chat");
static const u8 sText_OptionHide[] = _("Hide lobby chat");
static const u8 sText_OptionMask[] = _("Mask bad words");
static const u8 sText_On[] = _("  On");
static const u8 sText_Off[] = _("  Off");
static const u8 sText_QuickTitle[] = _("Quick chat: pick a phrase");
static const u8 sText_ComposeTo[] = _("Message");
static const u8 sText_ComposeName[] = _("Send to");
static const u8 sText_Colon[] = _(": ");
static const u8 sText_You[] = _("You");
static const u8 sText_Cursor[] = _("{RIGHT_ARROW} ");
static const u8 sText_Lobby[] = _("lobby");
static const u8 sText_Scrolled[] = _(" (older)");
static const u8 sText_UnreadMark[] = _("  PM!");
static const u8 sText_Hidden[] = _("Lobby chat is hidden in Options.");
static const u8 sText_LBracket[] = _("< ");
static const u8 sText_RBracket[] = _(" >  A send  B back");
static const u8 sText_NewBanner[] = _("  (L: open)");

static const u8 sPhrase0[] = _("gl hf");
static const u8 sPhrase1[] = _("gg");
static const u8 sPhrase2[] = _("wp");
static const u8 sPhrase3[] = _("ty");
static const u8 sPhrase4[] = _("nice");
static const u8 sPhrase5[] = _("oops");
static const u8 sPhrase6[] = _("brb");
static const u8 sPhrase7[] = _("one more?");
static const u8 *const sDefaultPhrases[KURO_QUICK_PHRASES] = {sPhrase0, sPhrase1, sPhrase2, sPhrase3, sPhrase4, sPhrase5, sPhrase6, sPhrase7};

// A short display-only list. Each letter becomes an asterisk when the mask is on.
static const u8 sMaskWord0[] = _("damn");
static const u8 sMaskWord1[] = _("hell");
static const u8 sMaskWord2[] = _("crap");
static const u8 sMaskWord3[] = _("bitch");
static const u8 sMaskWord4[] = _("shit");
static const u8 sMaskWord5[] = _("fuck");
static const u8 *const sMaskWords[] = {sMaskWord0, sMaskWord1, sMaskWord2, sMaskWord3, sMaskWord4, sMaskWord5};

static const u8 sTextNormal[3] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_LIGHT_GRAY};
static const u8 sTextName[3] = {TEXT_COLOR_TRANSPARENT, CHAT_PALETTE_NAME_SLOT, TEXT_COLOR_LIGHT_GRAY};
static const u8 sTextGrey[3] = {TEXT_COLOR_TRANSPARENT, CHAT_PALETTE_GREY_SLOT, TEXT_COLOR_LIGHT_GRAY};

void KuroChat_InitPhrases(void)
{
    u32 i;

    for (i = 0; i < KURO_QUICK_PHRASES; i++)
        StringCopy(gKuroQuickChat[i], sDefaultPhrases[i]);
}

// ---- Storage ---------------------------------------------------------------

static struct ChatRow *RowAt(u32 conv, u32 logical)
{
    const struct Conversation *c = &sConvs[conv];
    u32 cap = c->isPm ? KURO_CHAT_PM_ROWS : KURO_CHAT_ROOM_ROWS;
    u32 index = (c->head + logical) % cap;

    if (c->isPm)
        return &sPmRows[conv - KURO_CHAT_ROOMS][index];
    return &sRoomRows[conv][index];
}

static u32 RowCapacity(u32 conv)
{
    return sConvs[conv].isPm ? KURO_CHAT_PM_ROWS : KURO_CHAT_ROOM_ROWS;
}

static void ClearConversation(u32 conv, bool32 isPm)
{
    sConvs[conv].used = TRUE;
    sConvs[conv].isPm = isPm;
    sConvs[conv].head = 0;
    sConvs[conv].count = 0;
    sConvs[conv].unread = 0;
    sConvs[conv].users = 0;
    sConvs[conv].title[0] = EOS;
}

static void CopyField(u8 *dest, u32 destSize, const u8 *source, u32 available, u32 *consumed)
{
    u32 i;

    for (i = 0; i < available && source[i] != EOS; i++)
        if (i < destSize - 1)
            dest[i] = source[i];
    dest[min(i, destSize - 1)] = EOS;
    *consumed = i < available ? i + 1 : i;
}

// Adds a row to the end of a conversation. Returns the logical index it landed on.
static void AppendRow(u32 conv, u32 kind, u32 colour, u32 rank, const u8 *name, const u8 *text)
{
    struct Conversation *c = &sConvs[conv];
    u32 cap = RowCapacity(conv);
    struct ChatRow *row;
    u8 *dest;
    u32 nameLength = 0;

    if (c->count == cap)
        c->head = (c->head + 1) % cap; // Oldest row overwritten.
    else
        c->count++;
    row = RowAt(conv, c->count - 1);
    row->kind = kind;
    row->colour = colour;
    dest = row->text;
    if (name[0] != EOS)
    {
        if (rank != 0)
        {
            *dest++ = rank;
            nameLength++;
        }
        while (*name != EOS && dest < &row->text[KURO_CHAT_ROW_TEXT - 2])
        {
            *dest++ = *name++;
            nameLength++;
        }
        *dest++ = CHAR_COLON;
        *dest++ = CHAR_SPACE;
        nameLength += 2;
    }
    row->nameLength = nameLength;
    while (*text != EOS && dest < &row->text[KURO_CHAT_ROW_TEXT - 1])
        *dest++ = *text++;
    *dest = EOS;
}

// The relay wraps text, so a frame's text is exactly one row.
static void ChatAddRow(u32 conv, u32 kind, u32 colour, u32 rank, const u8 *name, const u8 *text)
{
    bool32 showing = sCh.screen == CHAT_SCREEN_CHAT && sCh.conv == conv;

    if (sConvs[conv].count == RowCapacity(conv) && !showing)
        ;
    AppendRow(conv, kind, colour, rank, name, text);
    if (showing)
    {
        // Keep the view on what the player is reading, and follow the newest row otherwise.
        if (sCh.scrollOffset > 0)
        {
            if (sCh.scrollOffset < sConvs[conv].count - min(sConvs[conv].count, CHAT_VIEW_ROWS))
                sCh.scrollOffset++;
        }
        else if (sConvs[conv].count > CHAT_VIEW_ROWS)
        {
            u8 firstWindow = sCh.windowOrder[0];
            u32 i;

            for (i = 0; i + 1 < CHAT_VIEW_ROWS; i++)
                sCh.windowOrder[i] = sCh.windowOrder[i + 1];
            sCh.windowOrder[CHAT_VIEW_ROWS - 1] = firstWindow;
            sCh.dirtyRows |= 1 << (CHAT_VIEW_ROWS - 1);
            sCh.headerDirty |= 2; // The window order changed: the tilemap needs refreshing.
        }
        else
        {
            sCh.dirtyRows |= 1 << (sConvs[conv].count - 1);
        }
        sCh.headerDirty |= 1;
    }
    else if (kind == KURO_CHAT_KIND_CHAT)
    {
        if (sConvs[conv].unread != 0xFF)
            sConvs[conv].unread++;
    }
}

// ---- Frames ----------------------------------------------------------------

static void HandleRoomList(const u8 *payload, u16 length)
{
    u32 offset = 2, consumed;

    if (length < 2)
        return;
    if (payload[0] == 0)
        sRoomCount = 0;
    while (offset < length && sRoomCount < MAX_LISTED_ROOMS)
    {
        struct RoomEntry *room = &sRooms[sRoomCount];

        CopyField(room->id, sizeof(room->id), &payload[offset], length - offset, &consumed);
        offset += consumed;
        CopyField(room->title, sizeof(room->title), &payload[offset], length - offset, &consumed);
        offset += consumed;
        if (offset + 3 > length)
            return;
        room->users = payload[offset] | (payload[offset + 1] << 8);
        room->official = payload[offset + 2];
        offset += 3;
        sRoomCount++;
    }
    if (payload[1])
        sRoomListComplete = TRUE;
}

static void OnRoomJoined(const u8 *payload, u16 length)
{
    u32 consumed;
    u32 handle = payload[0];
    struct Conversation *c;

    if (length < 2 || handle >= KURO_CHAT_ROOMS)
        return;
    c = &sConvs[handle];
    if (!c->used)
        ClearConversation(handle, FALSE);
    CopyField(c->title, sizeof(c->title), &payload[1], length - 1, &consumed);
    if (1 + consumed + 2 <= length)
        c->users = payload[1 + consumed] | (payload[2 + consumed] << 8);
    sCh.joining = 0;
    if (sCh.screen == CHAT_SCREEN_CHAT && sCh.conv == 0xFF)
    {
        sCh.conv = handle;
        sCh.scrollOffset = 0;
        sCh.dirtyRows = (1 << CHAT_VIEW_ROWS) - 1;
        sCh.headerDirty |= 1;
        sCh.footerDirty = TRUE;
    }
    else if (sCh.screen == CHAT_SCREEN_ROOMS)
    {
        sCh.conv = handle;
        sCh.scrollOffset = 0;
        sCh.screen = CHAT_SCREEN_CHAT; // A room picked from the list opens as soon as it has a slot.
    }
    if (sCh.screen == CHAT_SCREEN_CHAT && sCh.conv == handle)
        sCh.headerDirty |= 1;
}

static void OnRoomLeft(u32 handle)
{
    if (handle >= KURO_CHAT_CONVERSATIONS)
        return;
    sConvs[handle].used = FALSE;
    if (sCh.screen == CHAT_SCREEN_CHAT && sCh.conv == handle)
    {
        u32 i;

        sCh.conv = 0xFF;
        for (i = 0; i < KURO_CHAT_CONVERSATIONS; i++)
        {
            if (sConvs[i].used)
            {
                sCh.conv = i;
                break;
            }
        }
        sCh.scrollOffset = 0;
        sCh.dirtyRows = (1 << CHAT_VIEW_ROWS) - 1;
        sCh.headerDirty |= 1;
    }
}

static void OnChatLine(const u8 *payload, u16 length)
{
    u8 name[KURO_CHAT_NAME_SIZE];
    u8 text[KURO_CHAT_ROW_TEXT];
    u32 offset = 3, consumed;
    u32 handle, kind, rank, colour;

    if (length < 5 || payload[0] >= KURO_CHAT_ROOMS)
        return;
    handle = payload[0];
    if (!sConvs[handle].used)
        ClearConversation(handle, FALSE);
    kind = payload[1];
    rank = payload[2];
    CopyField(name, sizeof(name), &payload[offset], length - offset, &consumed);
    offset += consumed;
    if (offset >= length)
        return;
    colour = payload[offset++] % KURO_CHAT_PALETTE_SIZE;
    CopyField(text, sizeof(text), &payload[offset], length - offset, &consumed);
    ChatAddRow(handle, kind, colour, rank, name, text);
}

static void OnPmLine(const u8 *payload, u16 length)
{
    u8 partner[KURO_CHAT_NAME_SIZE];
    u8 text[KURO_CHAT_ROW_TEXT];
    u32 offset = 2, consumed;
    u32 handle, dir, colour;

    if (length < 4 || payload[0] < KURO_CHAT_ROOMS || payload[0] >= KURO_CHAT_CONVERSATIONS)
        return;
    handle = payload[0];
    dir = payload[1];
    CopyField(partner, sizeof(partner), &payload[offset], length - offset, &consumed);
    offset += consumed;
    if (offset >= length)
        return;
    colour = payload[offset++] % KURO_CHAT_PALETTE_SIZE;
    CopyField(text, sizeof(text), &payload[offset], length - offset, &consumed);

    if (partner[0] != EOS && (!sConvs[handle].used || StringCompare(sConvs[handle].title, partner) != 0))
    {
        if (sConvs[handle].used && sConvs[handle].title[0] != EOS)
            ClearConversation(handle, TRUE);
        else if (!sConvs[handle].used)
            ClearConversation(handle, TRUE);
        StringCopy(sConvs[handle].title, partner);
    }
    if (!sConvs[handle].used)
        ClearConversation(handle, TRUE);
    if (dir == 0 && partner[0] != EOS)
        sLastUnreadPm = handle + 1;
    if (dir == 1 && partner[0] != EOS)
        ChatAddRow(handle, KURO_CHAT_KIND_CHAT, colour, 0, sText_You, text);
    else if (partner[0] != EOS)
        ChatAddRow(handle, KURO_CHAT_KIND_CHAT, colour, 0, partner, text);
    else
        ChatAddRow(handle, KURO_CHAT_KIND_CHAT, 0, 0, gText_EmptyString2, text);
}

static void HandleUserList(const u8 *payload, u16 length)
{
    u32 offset = 3, consumed;

    if (length < 3 || payload[0] >= KURO_CHAT_ROOMS || (sCh.conv != payload[0]))
        return;
    if (payload[1] == 0)
        sUserCount = 0;
    while (offset + 1 < length && sUserCount < MAX_LISTED_USERS)
    {
        struct UserEntry *user = &sUsers[sUserCount];

        user->rank = payload[offset];
        user->status = payload[offset + 1];
        offset += 2;
        CopyField(user->name, sizeof(user->name), &payload[offset], length - offset, &consumed);
        offset += consumed;
        sUserCount++;
    }
    if (payload[2])
        sUserListComplete = TRUE;
}

static void ShowBanner(u32 kind, const u8 *payload, u16 length)
{
    u32 consumed;

    CopyField(sBanner.text, sizeof(sBanner.text), payload, length, &consumed);
    sBanner.kind = kind;
    sBanner.pending = TRUE;
}

bool32 KuroChat_HandleFrame(u8 opcode, const u8 *payload, u16 length)
{
    switch (opcode)
    {
    case KURO_OP_ROOM_LIST:
        HandleRoomList(payload, length);
        break;
    case KURO_OP_ROOM_JOINED:
        OnRoomJoined(payload, length);
        break;
    case KURO_OP_CHAT_LINE:
        OnChatLine(payload, length);
        break;
    case KURO_OP_USER_LIST:
        HandleUserList(payload, length);
        break;
    case KURO_OP_PM_LINE:
        OnPmLine(payload, length);
        break;
    case KURO_OP_NOTIFY:
        if (length >= 1)
            ShowBanner(payload[0], &payload[1], length - 1);
        break;
    case KURO_OP_BATTLE_CHAT_LINE:
        if (length >= 1)
        {
            u8 name[KURO_CHAT_NAME_SIZE];
            u8 text[BATTLE_LOG_LINE_SIZE];
            u32 consumed;

            CopyField(name, sizeof(name), payload, length, &consumed);
            if (consumed < length)
            {
                CopyField(text, sizeof(text), &payload[consumed], length - consumed, &consumed);
                KuroChat_BattleLine(name, text);
            }
        }
        break;
    case KURO_OP_ROOM_LEFT:
        if (length >= 1)
            OnRoomLeft(payload[0]);
        break;
    case KURO_OP_USERS_ONLINE:
        if (length >= 2)
            sUsersOnline = payload[0] | (payload[1] << 8);
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

// ---- Masking and display ---------------------------------------------------

static u8 FoldLetter(u8 c)
{
    return (c >= CHAR_A && c <= CHAR_Z) ? c - CHAR_A + CHAR_a : c;
}

static bool32 WordAt(const u8 *text, const u8 *word)
{
    u32 i;

    for (i = 0; word[i] != EOS; i++)
        if (text[i] == EOS || FoldLetter(text[i]) != FoldLetter(word[i]))
            return FALSE;
    return TRUE;
}

// A display copy of the row text, with the bad-word list masked if the option is on.
static const u8 *DisplayText(const u8 *text)
{
    u32 i, w;

    if (!(gKuroChatFlags & KURO_CHAT_FLAG_PROFANITY_MASK))
        return text;
    StringCopy(sPlainBuffer, text);
    for (i = 0; sPlainBuffer[i] != EOS; i++)
    {
        for (w = 0; w < ARRAY_COUNT(sMaskWords); w++)
        {
            if (WordAt(&sPlainBuffer[i], sMaskWords[w]))
            {
                u32 k;

                for (k = 0; sMaskWords[w][k] != EOS; k++)
                    sPlainBuffer[i + k] = 0x3E; // CHAR_KURO_ASTERISK
                break;
            }
        }
    }
    return sPlainBuffer;
}

// ---- Chat windows ----------------------------------------------------------

static u32 BottomLogical(u32 conv)
{
    return sConvs[conv].count - sCh.scrollOffset;
}

static s32 LogicalAtVisual(u32 conv, u32 visual)
{
    s32 bottom = BottomLogical(conv);
    s32 top = bottom - CHAT_VIEW_ROWS;

    if (top < 0)
        top = 0;
    return top + visual < bottom ? top + visual : -1;
}

static bool32 RoomIsLobby(u32 conv)
{
    return !sConvs[conv].isPm && StringCompareN(sConvs[conv].title, sText_Lobby, 5) == 0;
}

static void CreateChatWindows(void)
{
    struct WindowTemplate tmpl;
    u32 i;

    if (sCh.windowsUp)
        return;
    tmpl.bg = 0;
    tmpl.tilemapLeft = 0;
    tmpl.width = CHAT_ROW_WIDTH_TILES;
    tmpl.height = CHAT_ROW_HEIGHT_TILES;
    tmpl.baseBlock = CHAT_BASE_TILE_ROWS;
    for (i = 0; i < CHAT_VIEW_ROWS; i++)
    {
        tmpl.tilemapTop = CHAT_FIRST_ROW_TILE + i * CHAT_ROW_HEIGHT_TILES;
        tmpl.paletteNum = CHAT_PALETTE_FIRST + i;
        sCh.rowWindows[i] = AddWindow(&tmpl);
        sCh.windowOrder[i] = i;
        tmpl.baseBlock += CHAT_ROW_WIDTH_TILES * CHAT_ROW_HEIGHT_TILES;
        Menu_LoadStdPalAt(BG_PLTT_ID(CHAT_PALETTE_FIRST + i));
        gPlttBufferUnfaded[BG_PLTT_ID(CHAT_PALETTE_FIRST + i) + CHAT_PALETTE_GREY_SLOT] = RGB(14, 14, 14);
        gPlttBufferFaded[BG_PLTT_ID(CHAT_PALETTE_FIRST + i) + CHAT_PALETTE_GREY_SLOT] = RGB(14, 14, 14);
    }
    tmpl.tilemapTop = 0;
    tmpl.paletteNum = 15;
    tmpl.baseBlock = CHAT_BASE_TILE_HEADER;
    sCh.headerWindow = AddWindow(&tmpl);
    tmpl.tilemapTop = CHAT_FOOTER_TILE;
    tmpl.baseBlock = CHAT_BASE_TILE_FOOTER;
    sCh.footerWindow = AddWindow(&tmpl);
    sCh.windowsUp = TRUE;
    sCh.dirtyRows = (1 << CHAT_VIEW_ROWS) - 1;
    sCh.headerDirty = 3;
    sCh.footerDirty = TRUE;
}

static void DestroyChatWindows(void)
{
    u32 i;

    if (!sCh.windowsUp)
        return;
    sCh.windowsUp = FALSE;
    ClearWindowTilemap(sCh.headerWindow);
    ClearWindowTilemap(sCh.footerWindow);
    for (i = 0; i < CHAT_VIEW_ROWS; i++)
    {
        ClearWindowTilemap(sCh.rowWindows[i]);
        RemoveWindow(sCh.rowWindows[i]);
    }
    RemoveWindow(sCh.headerWindow);
    RemoveWindow(sCh.footerWindow);
}

static void EnterFullScreen(void)
{
    u32 i;

    if (sCh.mainHidden)
        return;
    KuroMenu_HideMainLayout();
    CreateChatWindows();
    for (i = 0; i < CHAT_VIEW_ROWS; i++)
        FillWindowPixelBuffer(sCh.rowWindows[i], PIXEL_FILL(TEXT_COLOR_WHITE));
    FillWindowPixelBuffer(sCh.headerWindow, PIXEL_FILL(TEXT_COLOR_WHITE));
    FillWindowPixelBuffer(sCh.footerWindow, PIXEL_FILL(TEXT_COLOR_WHITE));
    sCh.mainHidden = TRUE;
    sCh.headerDirty = 3;
    sCh.footerDirty = TRUE;
    sCh.dirtyRows = (1 << CHAT_VIEW_ROWS) - 1;
}

static void LeaveFullScreen(void)
{
    if (!sCh.mainHidden)
        return;
    sCh.mainHidden = FALSE;
    DestroyChatWindows();
    KuroMenu_ShowMainLayout();
}

static void DrawChatRow(u32 visual)
{
    u32 windowIndex = sCh.windowOrder[visual];
    u32 windowId = sCh.rowWindows[windowIndex];
    s32 logical = (sCh.conv == 0xFF) ? -1 : LogicalAtVisual(sCh.conv, visual);

    FillWindowPixelBuffer(windowId, PIXEL_FILL(TEXT_COLOR_WHITE));
    if (logical >= 0 && (RoomIsLobby(sCh.conv) ? !(gKuroChatFlags & KURO_CHAT_FLAG_HIDE_LOBBY) : TRUE))
    {
        const struct ChatRow *row = RowAt(sCh.conv, logical);
        u32 palette = BG_PLTT_ID(gWindows[windowId].window.paletteNum);
        u32 x = CHAT_ROW_PIXEL_X;
        const u8 *text = row->text;

        if (row->nameLength > 0)
        {
            u8 namePart[KURO_CHAT_NAME_SIZE + 3];
            u32 length = min(row->nameLength, sizeof(namePart) - 1);

            gPlttBufferUnfaded[palette + CHAT_PALETTE_NAME_SLOT] = sChatPalette[row->colour];
            gPlttBufferFaded[palette + CHAT_PALETTE_NAME_SLOT] = sChatPalette[row->colour];
            memcpy(namePart, text, length);
            namePart[length] = EOS;
            AddTextPrinterParameterized4(windowId, FONT_NARROW, x, 0, 0, 0, sTextName, TEXT_SKIP_DRAW, namePart);
            x += GetStringWidth(FONT_NARROW, namePart, 0);
            text += length;
        }
        AddTextPrinterParameterized4(windowId, FONT_NARROW, x, 0, 0, 0, row->kind == KURO_CHAT_KIND_CHAT ? sTextNormal : sTextGrey,
                                     TEXT_SKIP_DRAW, DisplayText(text));
    }
    else if (logical >= 0)
    {
        AddTextPrinterParameterized4(windowId, FONT_NARROW, CHAT_ROW_PIXEL_X, 0, 0, 0, sTextGrey, TEXT_SKIP_DRAW, sText_Hidden);
    }
    CopyWindowToVram(windowId, COPYWIN_GFX);
}

static void RefreshTilemap(void)
{
    u32 i;

    for (i = 0; i < CHAT_VIEW_ROWS; i++)
    {
        u32 windowId = sCh.rowWindows[sCh.windowOrder[i]];

        gWindows[windowId].window.tilemapTop = CHAT_FIRST_ROW_TILE + i * CHAT_ROW_HEIGHT_TILES;
        PutWindowTilemap(windowId);
    }
    PutWindowTilemap(sCh.headerWindow);
    PutWindowTilemap(sCh.footerWindow);
    CopyBgTilemapBufferToVram(0);
}

static void DrawHeader(void)
{
    u8 buffer[64];
    u8 *end = buffer;

    FillWindowPixelBuffer(sCh.headerWindow, PIXEL_FILL(TEXT_COLOR_WHITE));
    if (sCh.conv == 0xFF)
    {
        StringCopy(buffer, sText_Joining);
    }
    else
    {
        const struct Conversation *c = &sConvs[sCh.conv];

        end = StringCopy(end, c->title);
        if (!c->isPm)
        {
            *end++ = CHAR_SPACE;
            *end++ = 0x5C; // '('
            end = ConvertIntToDecimalStringN(end, c->users, STR_CONV_MODE_LEFT_ALIGN, 4);
            *end++ = 0x5D; // ')'
            *end = EOS;
        }
        if (sCh.scrollOffset > 0)
            end = StringCopy(end, sText_Scrolled);
        if (KuroChat_TotalUnread() > 0)
            end = StringCopy(end, sText_UnreadMark);
    }
    AddTextPrinterParameterized4(sCh.headerWindow, FONT_NARROW, CHAT_ROW_PIXEL_X, 0, 0, 0, sTextNormal, TEXT_SKIP_DRAW, buffer);
    CopyWindowToVram(sCh.headerWindow, COPYWIN_GFX);
}

static void DrawFooter(void)
{
    FillWindowPixelBuffer(sCh.footerWindow, PIXEL_FILL(TEXT_COLOR_WHITE));
    if (sCh.quickActive)
    {
        u8 buffer[64];
        u8 *end = StringCopy(buffer, sText_LBracket);

        end = StringCopy(end, gKuroQuickChat[sCh.quickIndex]);
        StringCopy(end, sText_RBracket);
        AddTextPrinterParameterized4(sCh.footerWindow, FONT_NARROW, CHAT_ROW_PIXEL_X, 0, 0, 0, sTextNormal, TEXT_SKIP_DRAW, buffer);
    }
    else
    {
        AddTextPrinterParameterized4(sCh.footerWindow, FONT_SMALL_NARROW, CHAT_ROW_PIXEL_X, 1, 0, 0, sTextGrey, TEXT_SKIP_DRAW, sText_ChatHelp);
    }
    CopyWindowToVram(sCh.footerWindow, COPYWIN_GFX);
}

// Draws what changed: new rows, a few per frame, so input is never held up.
void KuroChat_Render(void)
{
    u32 budget = RENDER_ROW_BUDGET;
    u32 visual;

    if (!sCh.windowsUp || KuroKeyboard_IsOpen())
        return;
    if (sCh.headerDirty & 2)
    {
        RefreshTilemap();
        sCh.headerDirty &= ~2;
    }
    for (visual = 0; visual < CHAT_VIEW_ROWS && budget > 0; visual++)
    {
        if (sCh.dirtyRows & (1 << visual))
        {
            sCh.dirtyRows &= ~(1 << visual);
            DrawChatRow(visual);
            budget--;
        }
    }
    if (budget > 0 && (sCh.headerDirty & 1))
    {
        DrawHeader();
        sCh.headerDirty &= ~1;
        budget--;
    }
    if (budget > 0 && sCh.footerDirty)
    {
        DrawFooter();
        sCh.footerDirty = FALSE;
    }
}

// ---- Views -----------------------------------------------------------------

static void SetView(u32 conv)
{
    sCh.conv = conv;
    sCh.scrollOffset = 0;
    sCh.dirtyRows = (1 << CHAT_VIEW_ROWS) - 1;
    sCh.headerDirty |= 1;
    sCh.footerDirty = TRUE;
    if (conv != 0xFF)
        sConvs[conv].unread = 0;
}

static void ScrollView(s32 delta)
{
    const struct Conversation *c = &sConvs[sCh.conv];
    s32 maxOffset = c->count > CHAT_VIEW_ROWS ? c->count - CHAT_VIEW_ROWS : 0;
    s32 next = sCh.scrollOffset + delta;
    u32 i;

    if (next < 0)
        next = 0;
    if (next > maxOffset)
        next = maxOffset;
    if (next == sCh.scrollOffset)
        return;
    delta = next - sCh.scrollOffset;
    sCh.scrollOffset = next;
    // Scrolling by one rotates the windows; the row that came into view is the only redraw.
    if (delta == 1)
    {
        u8 last = sCh.windowOrder[CHAT_VIEW_ROWS - 1];

        for (i = CHAT_VIEW_ROWS - 1; i > 0; i--)
            sCh.windowOrder[i] = sCh.windowOrder[i - 1];
        sCh.windowOrder[0] = last;
        sCh.dirtyRows |= 1;
    }
    else if (delta == -1)
    {
        u8 first = sCh.windowOrder[0];

        for (i = 0; i + 1 < CHAT_VIEW_ROWS; i++)
            sCh.windowOrder[i] = sCh.windowOrder[i + 1];
        sCh.windowOrder[CHAT_VIEW_ROWS - 1] = first;
        sCh.dirtyRows |= 1 << (CHAT_VIEW_ROWS - 1);
    }
    else
    {
        sCh.dirtyRows = (1 << CHAT_VIEW_ROWS) - 1;
    }
    sCh.headerDirty |= 2;
    sCh.headerDirty |= 1;
}

u8 KuroChat_TotalUnread(void)
{
    u32 i, total = 0;

    for (i = 0; i < KURO_CHAT_CONVERSATIONS; i++)
        if (sConvs[i].used && sConvs[i].isPm)
            total += sConvs[i].unread;
    return min(total, 99);
}

static void NextConversation(s32 step)
{
    s32 next = sCh.conv == 0xFF ? 0 : sCh.conv;
    u32 i;

    for (i = 0; i < KURO_CHAT_CONVERSATIONS; i++)
    {
        next = (next + step + KURO_CHAT_CONVERSATIONS) % KURO_CHAT_CONVERSATIONS;
        if (sConvs[next].used)
            break;
    }
    if (sConvs[next].used && next != sCh.conv)
    {
        PlaySE(SE_SELECT);
        SetView(next);
    }
}

void KuroChat_Open(enum KuroChatEntry entry)
{
    u32 i;

    memset(&sCh, 0, sizeof(sCh));
    sCh.conv = 0xFF;
    sCh.screen = CHAT_SCREEN_CHAT;
    switch (entry)
    {
    case KURO_CHAT_ENTRY_LOBBY:
        for (i = 0; i < KURO_CHAT_ROOMS; i++)
        {
            if (sConvs[i].used)
            {
                sCh.conv = i;
                break;
            }
        }
        if (sCh.conv == 0xFF)
        {
            KuroLink_SendFrame(KURO_OP_ROOM_JOIN, sText_Lobby, StringLength(sText_Lobby) + 1);
            sCh.joining = 1;
        }
        break;
    case KURO_CHAT_ENTRY_PMS:
        sCh.screen = CHAT_SCREEN_PMS;
        break;
    case KURO_CHAT_ENTRY_OPTIONS:
        sCh.screen = CHAT_SCREEN_OPTIONS;
        break;
    }
    if (sCh.screen == CHAT_SCREEN_CHAT)
    {
        EnterFullScreen();
        if (sCh.conv != 0xFF)
            SetView(sCh.conv);
    }
}

void KuroChat_Close(void)
{
    LeaveFullScreen();
    sCh.screen = CHAT_SCREEN_CLOSED;
}

bool32 KuroChat_IsOpen(void)
{
    return sCh.screen != CHAT_SCREEN_CLOSED;
}

bool32 KuroChat_IsFullScreen(void)
{
    return sCh.screen == CHAT_SCREEN_CHAT && !KuroKeyboard_IsOpen();
}

void KuroChat_OnBattleStart(void)
{
    // FreeAllWindowBuffers took the chat windows with it.
    sCh.windowsUp = FALSE;
    sCh.mainHidden = FALSE;
    sCh.screen = CHAT_SCREEN_CLOSED;
    if (KuroKeyboard_IsOpen())
        KuroKeyboard_Close();
    sBanner.menuUp = FALSE;
    sBanner.active = FALSE;
}

// ---- Sending ---------------------------------------------------------------

static void OpenComposeKeyboard(u32 target, const u8 *title)
{
    struct KuroKeyboardOptions options = {.title = title, .text = sDraft, .limit = KEYBOARD_LIMIT, .predictive = TRUE, .learn = TRUE};

    sCh.keyboardTarget = target;
    if (target == KEYBOARD_PM_NAME)
        options.limit = KURO_CHAT_NAME_SIZE - 2;
    if (target != KEYBOARD_CHAT || sDraftConv != sCh.conv + 1)
    {
        sDraft[0] = EOS;
        sDraftConv = target == KEYBOARD_CHAT ? sCh.conv + 1 : 0;
    }
    LeaveFullScreen();
    KuroKeyboard_Open(&options);
}

static void AfterKeyboardToChat(void)
{
    EnterFullScreen();
    sCh.dirtyRows = (1 << CHAT_VIEW_ROWS) - 1;
    sCh.headerDirty = 3;
    sCh.footerDirty = TRUE;
    gKuroUiScreen = KURO_UI_CHAT;
}

static bool32 SendToConversation(u32 conv, const u8 *text)
{
    u8 body[KURO_CHAT_NAME_SIZE + KEYBOARD_LIMIT + 3];
    u32 length;

    if (sConvs[conv].isPm)
    {
        u8 *end = StringCopy(body, sConvs[conv].title);

        *end++ = EOS;
        end = StringCopy(end, text);
        length = end - body + 1;
        return KuroLink_SendFrame(KURO_OP_PM_SEND, body, length);
    }
    body[0] = conv;
    length = StringCopy(&body[1], text) - body + 1;
    return KuroLink_SendFrame(KURO_OP_CHAT_SEND, body, length);
}

// Quick chat in a conversation: SELECT, rotate with the D-pad, A sends, B cancels.
static bool32 HandleQuick(void)
{
    if (!sCh.quickActive)
        return FALSE;
    if (JOY_NEW(B_BUTTON) || JOY_NEW(SELECT_BUTTON))
    {
        PlaySE(SE_SELECT);
        sCh.quickActive = FALSE;
        sCh.footerDirty = TRUE;
    }
    else if (JOY_NEW(DPAD_LEFT) || JOY_NEW(DPAD_UP) || JOY_NEW(DPAD_RIGHT) || JOY_NEW(DPAD_DOWN))
    {
        PlaySE(SE_SELECT);
        sCh.quickIndex = (sCh.quickIndex + ((JOY_NEW(DPAD_LEFT) || JOY_NEW(DPAD_UP)) ? KURO_QUICK_PHRASES - 1 : 1)) % KURO_QUICK_PHRASES;
        sCh.footerDirty = TRUE;
    }
    else if (JOY_NEW(A_BUTTON) && sCh.conv != 0xFF)
    {
        PlaySE(SE_SELECT);
        SendToConversation(sCh.conv, gKuroQuickChat[sCh.quickIndex]);
        sCh.quickActive = FALSE;
        sCh.footerDirty = TRUE;
    }
    return TRUE;
}

// ---- Lists -----------------------------------------------------------------

static void DrawRow(u32 windowId, const u8 *text, u32 row, u32 y)
{
    u8 buffer[64];
    u8 *end = StringCopy(buffer, row == sCh.cursor ? sText_Cursor : sText_PersonIndent);

    StringCopy(end, text);
    AddTextPrinterParameterized(windowId, FONT_NARROW, buffer, 0, y, TEXT_SKIP_DRAW, NULL);
}

// A list that scrolls under a cursor: keeps the cursor within the visible rows.
static void ClampTop(u32 count)
{
    if (sCh.cursor < sCh.top)
        sCh.top = sCh.cursor;
    else if (sCh.cursor >= sCh.top + LIST_VISIBLE_ROWS)
        sCh.top = sCh.cursor - LIST_VISIBLE_ROWS + 1;
    if (count <= LIST_VISIBLE_ROWS)
        sCh.top = 0;
}

static bool32 ListInput(u32 count)
{
    if (count == 0)
        return FALSE;
    if (JOY_REPEAT(DPAD_UP))
    {
        sCh.cursor = (sCh.cursor + count - 1) % count;
        ClampTop(count);
        return TRUE;
    }
    if (JOY_REPEAT(DPAD_DOWN))
    {
        sCh.cursor = (sCh.cursor + 1) % count;
        ClampTop(count);
        return TRUE;
    }
    return FALSE;
}

static void DrawRoomList(u32 windowId)
{
    u32 i;
    u8 buffer[56];

    AddTextPrinterParameterized(windowId, FONT_NORMAL, sText_RoomsTitle, 0, 0, TEXT_SKIP_DRAW, NULL);
    if (sRoomCount == 0)
    {
        AddTextPrinterParameterized(windowId, FONT_NORMAL, sText_WaitingList, 0, LIST_ROW_HEIGHT * 2, TEXT_SKIP_DRAW, NULL);
        return;
    }
    for (i = sCh.top; i < sRoomCount && i < sCh.top + LIST_VISIBLE_ROWS; i++)
    {
        u8 *end = buffer;

        if (sRooms[i].official)
            end = StringCopy(end, sText_Official);
        end = StringCopy(end, sRooms[i].title);
        end = StringCopy(end, sText_PersonIndent);
        end = ConvertIntToDecimalStringN(end, sRooms[i].users, STR_CONV_MODE_LEFT_ALIGN, 4);
        StringCopy(end, sText_OnlineCount);
        DrawRow(windowId, buffer, i, (i - sCh.top + 1) * LIST_ROW_HEIGHT);
    }
}

static void DrawUserList(u32 windowId)
{
    u8 title[32];
    u32 i;

    if (sCh.conv == 0xFF)
        return;
    StringCopy(StringCopy(title, sText_UsersTitle), sConvs[sCh.conv].title);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, title, 0, 0, TEXT_SKIP_DRAW, NULL);
    for (i = sCh.top; i < sUserCount && i < sCh.top + LIST_VISIBLE_ROWS; i++)
    {
        u8 buffer[KURO_CHAT_NAME_SIZE + 12];
        u8 *end = buffer;

        if (sUsers[i].rank != 0)
        {
            *end++ = sUsers[i].rank;
            *end = EOS;
        }
        end = StringCopy(end, sUsers[i].name);
        if (sUsers[i].status != 0)
            StringCopy(end, sText_Away);
        DrawRow(windowId, buffer, i, (i - sCh.top + 1) * LIST_ROW_HEIGHT);
    }
    if (!sUserListComplete && sUserCount == 0)
        AddTextPrinterParameterized(windowId, FONT_NORMAL, sText_WaitingList, 0, LIST_ROW_HEIGHT * 2, TEXT_SKIP_DRAW, NULL);
}

static void DrawPmList(u32 windowId)
{
    u32 i, row = 0;

    AddTextPrinterParameterized(windowId, FONT_NORMAL, sText_PmsTitle, 0, 0, TEXT_SKIP_DRAW, NULL);
    DrawRow(windowId, sText_NewPm, row, LIST_ROW_HEIGHT);
    row++;
    for (i = KURO_CHAT_ROOMS; i < KURO_CHAT_CONVERSATIONS; i++)
    {
        if (sConvs[i].used)
        {
            u8 buffer[KURO_CHAT_TITLE_SIZE + 8];
            u8 *end = StringCopy(buffer, sConvs[i].title);

            if (sConvs[i].unread > 0)
            {
                end = StringCopy(end, sText_PersonIndent);
                *end++ = 0x5C; // '('
                end = ConvertIntToDecimalStringN(end, sConvs[i].unread, STR_CONV_MODE_LEFT_ALIGN, 2);
                *end++ = 0x5D;
                *end = EOS;
            }
            DrawRow(windowId, buffer, row, (row + 1) * LIST_ROW_HEIGHT);
            row++;
        }
    }
    if (row == 1)
        AddTextPrinterParameterized(windowId, FONT_NORMAL, sText_NoPms, 0, LIST_ROW_HEIGHT * 3, TEXT_SKIP_DRAW, NULL);
}

static u32 PmRowCount(void)
{
    u32 i, count = 1;

    for (i = KURO_CHAT_ROOMS; i < KURO_CHAT_CONVERSATIONS; i++)
        if (sConvs[i].used)
            count++;
    return count;
}

static u32 PmFromRow(u32 row)
{
    u32 i, seen = 0;

    for (i = KURO_CHAT_ROOMS; i < KURO_CHAT_CONVERSATIONS; i++)
    {
        if (sConvs[i].used)
        {
            if (seen == row)
                return i;
            seen++;
        }
    }
    return 0xFF;
}

static void DrawMenu(u32 windowId, const u8 *const *rows, u32 count)
{
    u32 i;

    if (sCh.conv != 0xFF)
        AddTextPrinterParameterized(windowId, FONT_NORMAL, sConvs[sCh.conv].title, 0, 0, TEXT_SKIP_DRAW, NULL);
    for (i = 0; i < count; i++)
        DrawRow(windowId, rows[i], i, (i + 1) * LIST_ROW_HEIGHT);
}

static const u8 *const sRoomMenuRows[ROOM_MENU_COUNT] = {sText_MenuUsers, sText_MenuRooms, sText_MenuLeave, sText_MenuBack};
static const u8 *const sPmMenuRows[PM_MENU_COUNT] = {sText_MenuIgnore, sText_MenuReport, sText_MenuBack};

static void DrawOptionRow(u32 windowId, const u8 *label, u32 flag, u32 row)
{
    u8 buffer[48];
    u8 *end = StringCopy(buffer, label);

    if (flag != 0)
        StringCopy(end, (gKuroChatFlags & flag) ? sText_On : sText_Off);
    DrawRow(windowId, buffer, row, (row + 1) * LIST_ROW_HEIGHT);
}

static void DrawOptions(u32 windowId)
{
    AddTextPrinterParameterized(windowId, FONT_NORMAL, sText_OptionsTitle, 0, 0, TEXT_SKIP_DRAW, NULL);
    DrawOptionRow(windowId, sText_OptionAccount, 0, OPTIONS_ACCOUNT);
    DrawOptionRow(windowId, sText_OptionQuick, 0, OPTIONS_QUICK);
    DrawOptionRow(windowId, sText_OptionMute, KURO_CHAT_FLAG_MUTE_BATTLE, OPTIONS_MUTE);
    DrawOptionRow(windowId, sText_OptionHide, KURO_CHAT_FLAG_HIDE_LOBBY, OPTIONS_HIDE_LOBBY);
    DrawOptionRow(windowId, sText_OptionMask, KURO_CHAT_FLAG_PROFANITY_MASK, OPTIONS_PROFANITY);
    DrawOptionRow(windowId, sText_MenuBack, 0, OPTIONS_BACK);
}

static void DrawQuickEdit(u32 windowId)
{
    u32 i;

    AddTextPrinterParameterized(windowId, FONT_NORMAL, sText_QuickTitle, 0, 0, TEXT_SKIP_DRAW, NULL);
    for (i = sCh.top; i < KURO_QUICK_PHRASES && i < sCh.top + LIST_VISIBLE_ROWS; i++)
        DrawRow(windowId, gKuroQuickChat[i], i, (i - sCh.top + 1) * LIST_ROW_HEIGHT);
}

void KuroChat_Draw(u32 windowId)
{
    switch (sCh.screen)
    {
    case CHAT_SCREEN_ROOMS:
        DrawRoomList(windowId);
        break;
    case CHAT_SCREEN_USERS:
        DrawUserList(windowId);
        break;
    case CHAT_SCREEN_PMS:
        DrawPmList(windowId);
        break;
    case CHAT_SCREEN_ROOM_MENU:
        if (sConvs[sCh.conv].isPm)
            DrawMenu(windowId, sPmMenuRows, PM_MENU_COUNT);
        else
            DrawMenu(windowId, sRoomMenuRows, ROOM_MENU_COUNT);
        break;
    case CHAT_SCREEN_USER_MENU:
        AddTextPrinterParameterized(windowId, FONT_NORMAL, sPmTarget, 0, 0, TEXT_SKIP_DRAW, NULL);
        DrawRow(windowId, sText_MenuPm, 0, LIST_ROW_HEIGHT);
        DrawRow(windowId, sText_MenuIgnore, 1, LIST_ROW_HEIGHT * 2);
        DrawRow(windowId, sText_MenuBack, 2, LIST_ROW_HEIGHT * 3);
        break;
    case CHAT_SCREEN_OPTIONS:
        DrawOptions(windowId);
        break;
    case CHAT_SCREEN_QUICK_EDIT:
        DrawQuickEdit(windowId);
        break;
    default:
        break;
    }
}

static u8 ScreenUi(void)
{
    switch (sCh.screen)
    {
    case CHAT_SCREEN_ROOMS: return KURO_UI_CHAT_ROOMS;
    case CHAT_SCREEN_USERS: return KURO_UI_CHAT_USERS;
    case CHAT_SCREEN_USER_MENU: return KURO_UI_CHAT_USER_MENU;
    case CHAT_SCREEN_PMS: return KURO_UI_CHAT_PMS;
    case CHAT_SCREEN_ROOM_MENU: return KURO_UI_CHAT_MENU;
    case CHAT_SCREEN_OPTIONS: return KURO_UI_CHAT_OPTIONS;
    case CHAT_SCREEN_QUICK_EDIT: return KURO_UI_CHAT_QUICK_EDIT;
    default: return KURO_UI_CHAT;
    }
}

static void Go(u32 screen)
{
    sCh.screen = screen;
    sCh.cursor = 0;
    sCh.top = 0;
}

static void BackToChat(void)
{
    sCh.screen = CHAT_SCREEN_CHAT;
    EnterFullScreen();
    SetView(sCh.conv);
    gKuroUiScreen = KURO_UI_CHAT;
}

static void SendUserListReq(void)
{
    u8 body = sCh.conv;

    sUserCount = 0;
    sUserListComplete = FALSE;
    KuroLink_SendFrame(KURO_OP_USER_LIST_REQ, &body, 1);
}

static void SendIgnore(const u8 *name, bool32 on)
{
    u8 body[KURO_CHAT_NAME_SIZE + 2];
    u8 *end = StringCopy(body, name);

    *end++ = EOS;
    *end = on;
    KuroLink_SendFrame(KURO_OP_IGNORE, body, end - body + 1);
}

// Returns TRUE when the keyboard finished this frame and the screen should be drawn again.
static bool32 HandleKeyboardResult(void)
{
    enum KuroKeyboardResult result;
    bool32 dirty = FALSE;

    if (!KuroKeyboard_IsOpen())
        return FALSE;
    result = KuroKeyboard_Update(&dirty);
    if (result == KURO_KEYBOARD_NONE)
    {
        // The keyboard paints only what changed, so the menu loop's redraw must keep running.
        if (dirty)
            KuroMenu_RequestDraw();
        return dirty;
    }
    switch (sCh.keyboardTarget)
    {
    case KEYBOARD_CHAT:
        if (result == KURO_KEYBOARD_SENT && sCh.conv != 0xFF)
        {
            SendToConversation(sCh.conv, sDraft);
            sDraft[0] = EOS;
            sDraftConv = 0;
        }
        AfterKeyboardToChat();
        return TRUE;
    case KEYBOARD_PM_NAME:
        if (result == KURO_KEYBOARD_SENT && sDraft[0] != EOS)
        {
            StringCopy(sPmTarget, sDraft);
            OpenComposeKeyboard(KEYBOARD_PM_TEXT, sText_ComposeTo);
            return TRUE;
        }
        KuroMenu_ShowMainLayout();
        return TRUE;
    case KEYBOARD_PM_TEXT:
        if (result == KURO_KEYBOARD_SENT && sDraft[0] != EOS)
        {
            u8 body[KURO_CHAT_NAME_SIZE + KEYBOARD_LIMIT + 3];
            u8 *end = StringCopy(body, sPmTarget);

            *end++ = EOS;
            end = StringCopy(end, sDraft);
            KuroLink_SendFrame(KURO_OP_PM_SEND, body, end - body + 1);
            sDraft[0] = EOS;
        }
        KuroMenu_ShowMainLayout();
        return TRUE;
    case KEYBOARD_QUICK_EDIT:
        if (result == KURO_KEYBOARD_SENT && sQuickEditBuffer[0] != EOS)
        {
            StringCopy(gKuroQuickChat[sCh.cursor], sQuickEditBuffer);
            KuroTeams_Save();
        }
        KuroMenu_ShowMainLayout();
        return TRUE;
    }
    return TRUE;
}

static void OpenRoomMenu(void)
{
    sCh.menuKind = sConvs[sCh.conv].isPm;
    Go(CHAT_SCREEN_ROOM_MENU);
    LeaveFullScreen();
}

static enum KuroChatExit UpdateChatScreen(void)
{
    if (HandleQuick())
        return KURO_CHAT_EXIT_NONE;
    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        LeaveFullScreen();
        sCh.screen = CHAT_SCREEN_CLOSED;
        return KURO_CHAT_EXIT_HUB;
    }
    if (sCh.conv == 0xFF)
    {
        if (!sCh.joining && JOY_NEW(START_BUTTON))
            Go(CHAT_SCREEN_ROOMS);
        return KURO_CHAT_EXIT_NONE;
    }
    if (JOY_NEW(START_BUTTON))
    {
        PlaySE(SE_SELECT);
        OpenRoomMenu();
    }
    else if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        OpenComposeKeyboard(KEYBOARD_CHAT, sConvs[sCh.conv].title);
    }
    else if (JOY_NEW(SELECT_BUTTON))
    {
        PlaySE(SE_SELECT);
        sCh.quickActive = TRUE;
        sCh.footerDirty = TRUE;
    }
    else if (JOY_NEW(L_BUTTON) && !sBanner.active)
    {
        NextConversation(-1);
    }
    else if (JOY_NEW(R_BUTTON))
    {
        NextConversation(1);
    }
    else if (JOY_REPEAT(DPAD_UP))
    {
        ScrollView(1);
    }
    else if (JOY_REPEAT(DPAD_DOWN))
    {
        ScrollView(-1);
    }
    return KURO_CHAT_EXIT_NONE;
}

static void UpdateRoomList(bool32 *dirty)
{
    if (ListInput(sRoomCount))
        *dirty = TRUE;
    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        BackToChat();
        *dirty = TRUE;
    }
    else if (JOY_NEW(A_BUTTON) && sRoomCount > 0)
    {
        PlaySE(SE_SELECT);
        KuroLink_SendFrame(KURO_OP_ROOM_JOIN, sRooms[sCh.cursor].id, StringLength(sRooms[sCh.cursor].id) + 1);
    }
}

static void UpdateRoomMenu(bool32 *dirty)
{
    u32 count = sCh.menuKind ? PM_MENU_COUNT : ROOM_MENU_COUNT;

    if (ListInput(count))
        *dirty = TRUE;
    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        BackToChat();
        return;
    }
    if (!JOY_NEW(A_BUTTON))
        return;
    PlaySE(SE_SELECT);
    if (sCh.menuKind)
    {
        switch (sCh.cursor)
        {
        case PM_MENU_IGNORE:
            SendIgnore(sConvs[sCh.conv].title, TRUE);
            BackToChat();
            break;
        case PM_MENU_REPORT:
        {
            u8 body[KURO_CHAT_NAME_SIZE];

            StringCopy(body, sConvs[sCh.conv].title);
            KuroLink_SendFrame(KURO_OP_REPORT_OPEN, body, StringLength(body) + 1);
            BackToChat();
            break;
        }
        default:
            BackToChat();
            break;
        }
        return;
    }
    switch (sCh.cursor)
    {
    case ROOM_MENU_USERS:
        Go(CHAT_SCREEN_USERS);
        SendUserListReq();
        *dirty = TRUE;
        break;
    case ROOM_MENU_ROOMS:
        Go(CHAT_SCREEN_ROOMS);
        KuroLink_SendFrame(KURO_OP_ROOM_LIST_REQ, NULL, 0);
        *dirty = TRUE;
        break;
    case ROOM_MENU_LEAVE:
    {
        u8 body = sCh.conv;

        KuroLink_SendFrame(KURO_OP_ROOM_LEAVE, &body, 1);
        BackToChat();
        break;
    }
    default:
        BackToChat();
        break;
    }
}

static void UpdateUsers(bool32 *dirty)
{
    if (sCh.needUserList)
    {
        sCh.needUserList = FALSE;
        *dirty = TRUE;
    }
    if (ListInput(sUserCount))
        *dirty = TRUE;
    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        BackToChat();
        return;
    }
    if (JOY_NEW(A_BUTTON) && sUserCount > 0)
    {
        PlaySE(SE_SELECT);
        StringCopy(sPmTarget, sUsers[sCh.cursor].name);
        Go(CHAT_SCREEN_USER_MENU);
        *dirty = TRUE;
    }
}

static void UpdateUserMenu(bool32 *dirty)
{
    if (ListInput(3))
        *dirty = TRUE;
    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        Go(CHAT_SCREEN_USERS);
        *dirty = TRUE;
        return;
    }
    if (!JOY_NEW(A_BUTTON))
        return;
    PlaySE(SE_SELECT);
    if (sCh.cursor == 0)
    {
        // A PM from here starts a conversation with this user.
        OpenComposeKeyboard(KEYBOARD_PM_TEXT, sPmTarget);
    }
    else if (sCh.cursor == 1)
    {
        SendIgnore(sPmTarget, TRUE);
        BackToChat();
    }
    else
    {
        Go(CHAT_SCREEN_USERS);
    }
    *dirty = TRUE;
}

static enum KuroChatExit UpdateOptions(bool32 *dirty)
{
    if (ListInput(OPTIONS_COUNT))
        *dirty = TRUE;
    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        return KURO_CHAT_EXIT_HUB;
    }
    if (!JOY_NEW(A_BUTTON))
        return KURO_CHAT_EXIT_NONE;
    PlaySE(SE_SELECT);
    switch (sCh.cursor)
    {
    case OPTIONS_ACCOUNT:
        return KURO_CHAT_EXIT_ACCOUNT;
    case OPTIONS_QUICK:
        Go(CHAT_SCREEN_QUICK_EDIT);
        *dirty = TRUE;
        break;
    case OPTIONS_MUTE:
        gKuroChatFlags ^= KURO_CHAT_FLAG_MUTE_BATTLE;
        KuroTeams_Save();
        *dirty = TRUE;
        break;
    case OPTIONS_HIDE_LOBBY:
        gKuroChatFlags ^= KURO_CHAT_FLAG_HIDE_LOBBY;
        KuroTeams_Save();
        *dirty = TRUE;
        break;
    case OPTIONS_PROFANITY:
        gKuroChatFlags ^= KURO_CHAT_FLAG_PROFANITY_MASK;
        KuroTeams_Save();
        *dirty = TRUE;
        break;
    default:
        return KURO_CHAT_EXIT_HUB;
    }
    return KURO_CHAT_EXIT_NONE;
}

static void UpdateQuickEdit(bool32 *dirty)
{
    if (ListInput(KURO_QUICK_PHRASES))
        *dirty = TRUE;
    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        Go(CHAT_SCREEN_OPTIONS);
        *dirty = TRUE;
    }
    else if (JOY_NEW(A_BUTTON))
    {
        struct KuroKeyboardOptions options = {.title = sText_QuickTitle, .text = sQuickEditBuffer, .limit = KURO_QUICK_PHRASE_SIZE - 1};

        PlaySE(SE_SELECT);
        StringCopy(sQuickEditBuffer, gKuroQuickChat[sCh.cursor]);
        sCh.keyboardTarget = KEYBOARD_QUICK_EDIT;
        KuroKeyboard_Open(&options);
    }
}

static enum KuroChatExit UpdatePmList(bool32 *dirty)
{
    u32 count = PmRowCount();

    if (ListInput(count))
        *dirty = TRUE;
    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        return KURO_CHAT_EXIT_HUB;
    }
    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        if (sCh.cursor == 0)
        {
            sDraft[0] = EOS;
            OpenComposeKeyboard(KEYBOARD_PM_NAME, sText_ComposeName);
        }
        else
        {
            u32 conv = PmFromRow(sCh.cursor - 1);

            if (conv != 0xFF)
            {
                sCh.conv = conv;
                sCh.screen = CHAT_SCREEN_CHAT;
                EnterFullScreen();
                SetView(conv);
                gKuroUiScreen = KURO_UI_CHAT;
            }
        }
    }
    return KURO_CHAT_EXIT_NONE;
}

enum KuroChatExit KuroChat_Update(void)
{
    bool32 dirty = FALSE;
    enum KuroChatExit result = KURO_CHAT_EXIT_NONE;

    if (HandleKeyboardResult())
        dirty = TRUE;
    if (KuroKeyboard_IsOpen())
        return dirty ? KURO_CHAT_EXIT_NONE : KURO_CHAT_EXIT_NONE;
    switch (sCh.screen)
    {
    case CHAT_SCREEN_CHAT:
        result = UpdateChatScreen();
        break;
    case CHAT_SCREEN_ROOMS:
        UpdateRoomList(&dirty);
        if (sRoomListComplete)
        {
            sRoomListComplete = FALSE;
            dirty = TRUE;
        }
        break;
    case CHAT_SCREEN_ROOM_MENU:
        UpdateRoomMenu(&dirty);
        break;
    case CHAT_SCREEN_USERS:
        UpdateUsers(&dirty);
        if (sUserListComplete && sUserCount > 0)
        {
            sUserListComplete = FALSE;
            dirty = TRUE;
        }
        break;
    case CHAT_SCREEN_USER_MENU:
        UpdateUserMenu(&dirty);
        break;
    case CHAT_SCREEN_PMS:
        result = UpdatePmList(&dirty);
        break;
    case CHAT_SCREEN_OPTIONS:
        result = UpdateOptions(&dirty);
        break;
    case CHAT_SCREEN_QUICK_EDIT:
        UpdateQuickEdit(&dirty);
        break;
    default:
        break;
    }
    if (result != KURO_CHAT_EXIT_NONE)
    {
        LeaveFullScreen();
        sCh.screen = CHAT_SCREEN_CLOSED;
        return result;
    }
    if (dirty && sCh.screen != CHAT_SCREEN_CHAT)
        KuroMenu_RequestDraw();
    gKuroUiScreen = (sCh.screen == CHAT_SCREEN_CHAT && sCh.quickActive) ? KURO_UI_CHAT_QUICK : ScreenUi();
    return KURO_CHAT_EXIT_NONE;
}

void KuroChat_DrawHubLine(u8 *buffer)
{
    u8 *end = buffer;

    *end = EOS;
    if (KuroChat_TotalUnread() > 0)
    {
        end = ConvertIntToDecimalStringN(end, KuroChat_TotalUnread(), STR_CONV_MODE_LEFT_ALIGN, 2);
        end = StringCopy(end, sText_OnlineCount);
    }
    if (sUsersOnline > 0)
    {
        if (end != buffer)
            end = StringCopy(end, sText_PersonIndent);
        end = ConvertIntToDecimalStringN(end, sUsersOnline, STR_CONV_MODE_LEFT_ALIGN, 5);
        StringCopy(end, sText_OnlineCount);
    }
}

bool32 KuroChat_OpenLatestPm(void)
{
    u32 i, target = 0xFF;

    if (sLastUnreadPm != 0 && sConvs[sLastUnreadPm - 1].used)
        target = sLastUnreadPm - 1;
    else
        for (i = KURO_CHAT_ROOMS; i < KURO_CHAT_CONVERSATIONS; i++)
            if (sConvs[i].used && sConvs[i].unread > 0)
                target = i;
    if (target == 0xFF)
        return FALSE;
    if (!KuroChat_IsOpen())
    {
        memset(&sCh, 0, sizeof(sCh));
        sCh.conv = 0xFF;
    }
    sCh.conv = target;
    sCh.screen = CHAT_SCREEN_CHAT;
    EnterFullScreen();
    SetView(target);
    gKuroUiScreen = KURO_UI_CHAT;
    return TRUE;
}

// ---- Banner ----------------------------------------------------------------

#define BANNER_HIDDEN 0
#define BANNER_SLIDE_IN 1
#define BANNER_SHOW 2
#define BANNER_SLIDE_OUT 3

void KuroChat_MenuActive(bool32 active)
{
    sBanner.menuUp = active;
    if (active)
    {
        struct WindowTemplate tmpl = {.bg = 1, .tilemapLeft = 0, .tilemapTop = 0, .width = 30, .height = 2, .paletteNum = 15, .baseBlock = 648};

        sBanner.windowId = AddWindow(&tmpl);
        FillWindowPixelBuffer(sBanner.windowId, PIXEL_FILL(TEXT_COLOR_WHITE));
        CopyWindowToVram(sBanner.windowId, COPYWIN_FULL);
        PutWindowTilemap(sBanner.windowId);
        CopyBgTilemapBufferToVram(1);
        SetGpuReg(REG_OFFSET_BG1VOFS, BANNER_HEIGHT_PIXELS);
        sBanner.offset = BANNER_HEIGHT_PIXELS;
        sBanner.phase = BANNER_HIDDEN;
        sBanner.active = FALSE;
    }
}

bool32 KuroChat_BannerVisible(void)
{
    return sBanner.active && sBanner.phase == BANNER_SHOW;
}

void KuroChat_BannerTick(void)
{
    if (!sBanner.menuUp)
        return;
    if (sBanner.pending && sBanner.phase == BANNER_HIDDEN)
    {
        u8 line[96];
        const u8 *text = sBanner.text;

        sBanner.pending = FALSE;
        if (sBanner.kind == KURO_NOTIFY_PM)
        {
            StringCopy(StringCopy(line, sBanner.text), sText_NewBanner);
            text = line;
        }
        FillWindowPixelBuffer(sBanner.windowId, PIXEL_FILL(TEXT_COLOR_WHITE));
        AddTextPrinterParameterized4(sBanner.windowId, FONT_NARROW, CHAT_ROW_PIXEL_X, 0, 0, 0, sTextNormal, TEXT_SKIP_DRAW, text);
        CopyWindowToVram(sBanner.windowId, COPYWIN_GFX);
        sBanner.phase = BANNER_SLIDE_IN;
        sBanner.active = TRUE;
        sBanner.timer = 0;
        PlaySE(sBanner.kind == KURO_NOTIFY_SYSTEM ? SE_SELECT : SE_PIN);
    }
    switch (sBanner.phase)
    {
    case BANNER_SLIDE_IN:
        sBanner.offset -= BANNER_SLIDE_SPEED;
        if (sBanner.offset <= 0)
        {
            sBanner.offset = 0;
            sBanner.phase = BANNER_SHOW;
        }
        break;
    case BANNER_SHOW:
        if (++sBanner.timer >= BANNER_FRAMES)
            sBanner.phase = BANNER_SLIDE_OUT;
        break;
    case BANNER_SLIDE_OUT:
        sBanner.offset += BANNER_SLIDE_SPEED;
        if (sBanner.offset >= BANNER_HEIGHT_PIXELS)
        {
            sBanner.offset = BANNER_HEIGHT_PIXELS;
            sBanner.phase = BANNER_HIDDEN;
            sBanner.active = FALSE;
        }
        break;
    default:
        break;
    }
    SetGpuReg(REG_OFFSET_BG1VOFS, sBanner.offset);
}

// ---- In-battle chat ---------------------------------------------------------

void KuroChat_ResetBattleLog(void)
{
    sBattleLogHead = 0;
    sBattleLogCount = 0;
    sMutedThisBattle = FALSE;
    sBubblePending = FALSE;
}

void KuroChat_LogAdd(const u8 *text)
{
    u32 index;

    index = (sBattleLogHead + sBattleLogCount) % BATTLE_LOG_LINES;
    if (sBattleLogCount == BATTLE_LOG_LINES)
    {
        sBattleLogHead = (sBattleLogHead + 1) % BATTLE_LOG_LINES;
    }
    else
    {
        sBattleLogCount++;
    }
    StringCopyN(sBattleLog[index], text, BATTLE_LOG_LINE_SIZE - 1);
    sBattleLog[index][BATTLE_LOG_LINE_SIZE - 1] = EOS;
}

u32 KuroChat_LogCount(void)
{
    return sBattleLogCount;
}

const u8 *KuroChat_LogLine(u32 index)
{
    return sBattleLog[(sBattleLogHead + index) % BATTLE_LOG_LINES];
}

bool32 KuroChat_BattleLineMuted(void)
{
    return sMutedThisBattle || (gKuroChatFlags & KURO_CHAT_FLAG_MUTE_BATTLE);
}

void KuroChat_MuteBattle(bool32 mute)
{
    sMutedThisBattle = mute;
}

void KuroChat_BattleLine(const u8 *name, const u8 *text)
{
    u32 i;

    KuroChat_LogAdd(text);
    // Our own line goes in the log but never into a bubble.
    if (StringCompare(name, gKuroSession.username) == 0 || KuroChat_BattleLineMuted())
        return;
    for (i = 0; i < BATTLE_LOG_LINE_SIZE - 1 && text[i] != EOS; i++)
        sBubbleText[i] = text[i];
    sBubbleText[i] = EOS;
    sBubblePending = TRUE;
}

bool32 KuroChat_TakeBattleBubble(void)
{
    bool32 pending = sBubblePending;

    sBubblePending = FALSE;
    return pending;
}

const u8 *KuroChat_BubbleText(void)
{
    return sBubbleText;
}

// ---- Keyboard over a battle ------------------------------------------------

static const struct BgTemplate sBattleKeyboardBgs[] =
{
    {.bg = 0, .charBaseIndex = 2, .mapBaseIndex = 31, .screenSize = 0, .paletteMode = 0, .priority = 0, .baseTile = 0},
};

static const struct WindowTemplate sBattleKeyboardWindows[] =
{
    {.bg = 0, .tilemapLeft = 2, .tilemapTop = 2, .width = 26, .height = 16, .paletteNum = 15, .baseBlock = 1},
    DUMMY_WIN_TEMPLATE,
};

static EWRAM_DATA u8 sBattleKeyboardText[KEYBOARD_LIMIT + 1] = {0};

static void VBlankCB_BattleKeyboard(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void CloseBattleKeyboard(bool32 sent)
{
    if (sent && sBattleKeyboardText[0] != EOS)
        KuroLink_SendFrame(KURO_OP_BATTLE_CHAT, sBattleKeyboardText, StringLength(sBattleKeyboardText) + 1);
    FreeAllWindowBuffers();
    SetMainCallback2(ReshowBattleScreenAfterMenu);
}

static void CB2_BattleKeyboard(void)
{
    bool32 dirty = FALSE;
    enum KuroKeyboardResult result;
    u8 opcode;
    u16 length;
    s32 next;

    // Battle frames wait in the ring until the battle resumes; chat and status frames must not.
    for (next = KuroLink_PeekOpcode(); next >= 0 && KuroSession_IsSessionOpcode(next); next = KuroLink_PeekOpcode())
    {
        static EWRAM_DATA u8 sPayload[KURO_MAX_PAYLOAD + 1] = {0};

        if (KuroLink_ReadFrame(&opcode, sPayload, &length) != KURO_LINK_READ_OK)
            break;
        KuroSession_HandleFrame(opcode, sPayload, length);
    }
    result = KuroKeyboard_Update(&dirty);
    if (result != KURO_KEYBOARD_NONE)
    {
        CloseBattleKeyboard(result == KURO_KEYBOARD_SENT);
        return;
    }
    if (dirty || gMain.state == 0)
    {
        KuroKeyboard_Draw(0);
        CopyWindowToVram(0, COPYWIN_FULL);
        gMain.state = 1;
    }
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    RunTextPrinters();
    UpdatePaletteFade();
}

void KuroChat_OpenBattleKeyboard(void)
{
    struct KuroKeyboardOptions options = {.title = sText_ComposeTo, .text = sBattleKeyboardText, .limit = KEYBOARD_LIMIT, .predictive = TRUE, .learn = TRUE};

    SetVBlankCallback(NULL);
    SetHBlankCallback(NULL);
    SetGpuReg(REG_OFFSET_DISPCNT, 0);
    ScanlineEffect_Stop();
    DmaClearLarge16(3, (void *)VRAM, VRAM_SIZE, 0x1000);
    DmaClear32(3, OAM, OAM_SIZE);
    DmaClear16(3, PLTT, PLTT_SIZE);
    ResetPaletteFade();
    ResetSpriteData();
    FreeAllSpritePalettes();
    FreeAllWindowBuffers();
    ResetBgsAndClearDma3BusyFlags(0);
    InitBgsFromTemplates(0, sBattleKeyboardBgs, ARRAY_COUNT(sBattleKeyboardBgs));
    SetBgTilemapBuffer(0, AllocZeroed(BG_SCREEN_SIZE));
    InitWindows(sBattleKeyboardWindows);
    DeactivateAllTextPrinters();
    LoadPalette(GetOverworldTextboxPalettePtr(), BG_PLTT_ID(14), PLTT_SIZE_4BPP);
    LoadUserWindowBorderGfx(0, 0x200, BG_PLTT_ID(14));
    Menu_LoadStdPalAt(BG_PLTT_ID(15));
    PutWindowTilemap(0);
    DrawStdFrameWithCustomTileAndPalette(0, FALSE, 0x200, 14);
    FillWindowPixelBuffer(0, PIXEL_FILL(TEXT_COLOR_WHITE));
    CopyBgTilemapBufferToVram(0);
    SetGpuReg(REG_OFFSET_BLDCNT, 0);
    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_0 | DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON | DISPCNT_BG0_ON);
    ShowBg(0);
    sBattleKeyboardText[0] = EOS;
    KuroKeyboard_Open(&options);
    gMain.state = 0;
    SetVBlankCallback(VBlankCB_BattleKeyboard);
    SetMainCallback2(CB2_BattleKeyboard);
}
