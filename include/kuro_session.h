#ifndef GUARD_KURO_SESSION_H
#define GUARD_KURO_SESSION_H

// Handshake and session frames shared by the menus and battle playback.

#define KURO_OP_HELLO 0x00
#define KURO_OP_STATUS 0x02
#define KURO_OP_FORMAT_LIST 0x03
#define KURO_OP_SEARCH_STATE 0x04
#define KURO_OP_POPUP 0x05
#define KURO_OP_TEAM_PUSH 0x06
#define KURO_OP_TEAM_EXPORT_DONE 0x07

#define KURO_OP_ACCOUNT_LIST 0x80
#define KURO_OP_LOGIN_RESULT 0x81
#define KURO_OP_RATINGS 0x8E
#define KURO_OP_AVATAR_LIST 0x8F

#define KURO_OP_HELLO_ACK 0x01
#define KURO_OP_ACCOUNT_LIST_REQ 0x02
#define KURO_OP_LOGIN_SAVED 0x03
#define KURO_OP_LOGIN_BEGIN 0x04
#define KURO_OP_SECURE_KEY 0x05
#define KURO_OP_LOGIN_REMEMBER 0x06
#define KURO_OP_LOGOUT 0x07
#define KURO_OP_LOGIN_GUEST 0x08
#define KURO_OP_SET_AVATAR 0x09
#define KURO_OP_AVATAR_LIST_REQ 0x0A
#define KURO_OP_RATINGS_REQ 0x0B
#define KURO_OP_OPEN_REGISTER 0x0C
#define KURO_OP_SEARCH 0x10
#define KURO_OP_CANCEL_SEARCH 0x11
#define KURO_OP_UPLOAD_TEAM 0x12
#define KURO_OP_TEAM_IMPORT_READY 0x13
#define KURO_OP_TEAM_EXPORT 0x14
#define KURO_OP_SAVE_REPLAY 0x1A
#define KURO_OP_ERR_VERSION 0xE1
#define KURO_OP_CHOOSE_TEAM_ORDER 0x22
#define KURO_FRAME_TEAM_PREVIEW 0x41
#define KURO_FRAME_REQUEST 0x42
#define KURO_REQUEST_FLAG_WAIT 8
#define KURO_REQUEST_FLAG_TEAM_PREVIEW 16

#define KURO_MAX_FORMATS 8
#define KURO_FORMAT_NAME_LENGTH 40
#define KURO_FORMAT_NEEDS_TEAM 1
#define KURO_MAX_ACCOUNTS 8
#define KURO_USERNAME_SIZE 20
#define KURO_MAX_AVATARS 72
#define KURO_ACCOUNT_FLAG_STORE 1

enum KuroLoginCode
{
    KURO_LOGIN_OK,
    KURO_LOGIN_WRONG_PASSWORD,
    KURO_LOGIN_NEEDS_PASSWORD,
    KURO_LOGIN_NAME_TAKEN,
    KURO_LOGIN_ERROR,
};

struct KuroRating
{
    u8 formatIndex;
    u16 elo;
};

enum KuroRelayStatus
{
    KURO_RELAY_OFFLINE,
    KURO_RELAY_CONNECTING,
    KURO_RELAY_GUEST,
    KURO_RELAY_LOGGED_IN,
    KURO_RELAY_LOGGED_OUT,
    KURO_RELAY_ERROR,
};

enum KuroSearchState
{
    KURO_SEARCH_IDLE,
    KURO_SEARCH_SEARCHING,
    KURO_SEARCH_MATCHED,
    KURO_SEARCH_REJECTED,
};

struct KuroFormat
{
    u8 index;
    u8 flags;
    u8 name[KURO_FORMAT_NAME_LENGTH];
};

struct KuroSession
{
    bool8 handshakeDone;
    bool8 mismatch;
    u8 status;
    u8 searchState;
    u8 formatCount;
    bool8 popupPending;
    bool8 teamPushed;
    bool8 exportDone;
    bool8 exportOk;
    bool8 resultPending;
    u8 resultKind;
    s16 ratingChange;
    u16 rating;
    u8 resultOpponent[20];
    u8 username[20];
    u8 detail[100];
    u8 popup[240];
    u8 relayBuild[40];
    u8 exportText[40];
    struct KuroFormat formats[KURO_MAX_FORMATS];
    bool8 accountListReady;
    bool8 accountStoreAvailable;
    bool8 loginResultPending;
    bool8 avatarListReady;
    u8 accountCount;
    u8 loginCode;
    u8 ratingCount;
    u8 avatarCount;
    u16 currentAvatar;
    u8 accountNames[KURO_MAX_ACCOUNTS][KURO_USERNAME_SIZE];
    u8 loginText[100];
    struct KuroRating ratings[KURO_MAX_FORMATS];
    u16 avatars[KURO_MAX_AVATARS];
};

extern struct KuroSession gKuroSession;

void KuroSession_Reset(void);
void KuroSession_HandleFrame(u8 opcode, const u8 *payload, u16 length);
const u8 *KuroSession_RomBuildString(void);
bool32 KuroSession_IsSessionOpcode(u32 opcode);

#endif // GUARD_KURO_SESSION_H
