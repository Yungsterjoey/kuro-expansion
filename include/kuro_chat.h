#ifndef GUARD_KURO_CHAT_H
#define GUARD_KURO_CHAT_H

// Chat rooms, PMs, the room and user lists, banners and quick chat. Protocol frames: docs/PROTOCOL.md.

#define KURO_CHAT_PALETTE_SIZE 16
#define KURO_CHAT_ROOMS 3
#define KURO_CHAT_PMS 4
#define KURO_CHAT_CONVERSATIONS (KURO_CHAT_ROOMS + KURO_CHAT_PMS)
#define KURO_CHAT_ROOM_ROWS 24
#define KURO_CHAT_PM_ROWS 12
#define KURO_CHAT_ROW_TEXT 69
#define KURO_CHAT_NAME_SIZE 20
#define KURO_CHAT_TITLE_SIZE 20
#define KURO_QUICK_PHRASES 8
#define KURO_QUICK_PHRASE_SIZE 12

#define KURO_OP_BATTLE_CHAT 0x26
#define KURO_OP_ROOM_LIST_REQ 0x70
#define KURO_OP_ROOM_JOIN 0x71
#define KURO_OP_ROOM_LEAVE 0x72
#define KURO_OP_CHAT_SEND 0x73
#define KURO_OP_PM_SEND 0x74
#define KURO_OP_IGNORE 0x79
#define KURO_OP_REPORT_OPEN 0x7A
#define KURO_OP_USER_LIST_REQ 0x7B

#define KURO_OP_ROOM_LIST 0x82
#define KURO_OP_ROOM_JOINED 0x83
#define KURO_OP_CHAT_LINE 0x84
#define KURO_OP_USER_LIST 0x85
#define KURO_OP_PM_LINE 0x87
#define KURO_OP_NOTIFY 0x8C
#define KURO_OP_BATTLE_CHAT_LINE 0x8D
#define KURO_OP_ROOM_LEFT 0x90
#define KURO_OP_USERS_ONLINE 0x91

enum KuroChatKind
{
    KURO_CHAT_KIND_CHAT,
    KURO_CHAT_KIND_SYSTEM,
    KURO_CHAT_KIND_NOTICE,
};

enum KuroNotifyKind
{
    KURO_NOTIFY_PM,
    KURO_NOTIFY_CHALLENGE,
    KURO_NOTIFY_SYSTEM,
};

// gKuroUiScreen values while chat screens are up.
#define KURO_UI_CHAT 80
#define KURO_UI_CHAT_ROOMS 81
#define KURO_UI_CHAT_USERS 82
#define KURO_UI_CHAT_PMS 83
#define KURO_UI_CHAT_MENU 84
#define KURO_UI_CHAT_QUICK 85
#define KURO_UI_CHAT_OPTIONS 86
#define KURO_UI_CHAT_QUICK_EDIT 87
#define KURO_UI_CHAT_USER_MENU 88
#define KURO_UI_BATTLE_QUICK 8
#define KURO_UI_BATTLE_MENU 6
#define KURO_UI_BATTLE_LOG 7

// Chat options, stored with the teams. Bit flags.
#define KURO_CHAT_FLAG_MUTE_BATTLE 1
#define KURO_CHAT_FLAG_HIDE_LOBBY 2
#define KURO_CHAT_FLAG_PROFANITY_MASK 4

enum KuroChatEntry
{
    KURO_CHAT_ENTRY_LOBBY,
    KURO_CHAT_ENTRY_PMS,
    KURO_CHAT_ENTRY_OPTIONS,
};

enum KuroChatExit
{
    KURO_CHAT_EXIT_NONE,
    KURO_CHAT_EXIT_HUB,
    KURO_CHAT_EXIT_ACCOUNT,
};

// The screen being shown, for the menu loop's drawing.
struct KuroChatOpenOptions
{
    u8 entry;
};

extern u8 gKuroQuickChat[KURO_QUICK_PHRASES][KURO_QUICK_PHRASE_SIZE];
extern u8 gKuroChatFlags;
extern volatile u8 gKuroChatFrameGap;

// Handles a session frame. Returns FALSE if it is not a chat frame.
bool32 KuroChat_HandleFrame(u8 opcode, const u8 *payload, u16 length);
void KuroChat_InitPhrases(void);
void KuroChat_Open(enum KuroChatEntry entry);
void KuroChat_Close(void);
bool32 KuroChat_IsOpen(void);
bool32 KuroChat_IsFullScreen(void);
// Once per frame while the chat screen is on show.
enum KuroChatExit KuroChat_Update(void);
void KuroChat_Draw(u32 windowId);
void KuroChat_Render(void);
// Banners, for every screen of the menu loop.
void KuroChat_BannerTick(void);
void KuroChat_MenuActive(bool32 active);
// Called when a battle takes over the screen: chat windows are gone from then on.
void KuroChat_OnBattleStart(void);
u8 KuroChat_TotalUnread(void);
void KuroChat_DrawHubLine(u8 *buffer);
// Opens the most recent PM with unread lines. Returns FALSE if there is none.
bool32 KuroChat_OpenLatestPm(void);
bool32 KuroChat_BannerVisible(void);

// Battle chat, drawn by kuro_playback on the battle screen.
void KuroChat_BattleLine(const u8 *name, const u8 *text);
bool32 KuroChat_BattleLineMuted(void);
const u8 *KuroChat_LastBattleLine(void);
bool32 KuroChat_TakeBattleBubble(void);
const u8 *KuroChat_BubbleText(void);
void KuroChat_MuteBattle(bool32 mute);
void KuroChat_ResetBattleLog(void);
u32 KuroChat_LogCount(void);
const u8 *KuroChat_LogLine(u32 index);
void KuroChat_LogAdd(const u8 *text);
void KuroChat_OpenBattleKeyboard(void);

// Implemented by the menu, for the chat module's use.
void KuroMenu_RequestDraw(void);
void KuroMenu_ShowMainLayout(void);
void KuroMenu_HideMainLayout(void);

#endif // GUARD_KURO_CHAT_H
