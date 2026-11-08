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

#define KURO_OP_HELLO_ACK 0x01
#define KURO_OP_SEARCH 0x10
#define KURO_OP_CANCEL_SEARCH 0x11
#define KURO_OP_UPLOAD_TEAM 0x12
#define KURO_OP_TEAM_IMPORT_READY 0x13
#define KURO_OP_TEAM_EXPORT 0x14
#define KURO_OP_SAVE_REPLAY 0x15
#define KURO_OP_ERR_VERSION 0xE1

#define KURO_MAX_FORMATS 8
#define KURO_FORMAT_NAME_LENGTH 40
#define KURO_FORMAT_NEEDS_TEAM 1

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
    u8 username[20];
    u8 detail[100];
    u8 popup[120];
    u8 relayBuild[40];
    u8 exportText[40];
    struct KuroFormat formats[KURO_MAX_FORMATS];
};

extern struct KuroSession gKuroSession;

void KuroSession_Reset(void);
void KuroSession_HandleFrame(u8 opcode, const u8 *payload, u16 length);
const u8 *KuroSession_RomBuildString(void);

#endif // GUARD_KURO_SESSION_H
