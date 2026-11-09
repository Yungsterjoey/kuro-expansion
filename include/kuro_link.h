#ifndef GUARD_KURO_LINK_H
#define GUARD_KURO_LINK_H

// The 4 KB EWRAM mailbox shared with the Lua bridge. See docs/PROTOCOL.md.

#define KURO_RX_RING_SIZE 3072
#define KURO_TX_RING_SIZE 992
#define KURO_MAX_PAYLOAD 255
#define KURO_PROTOCOL_MAJOR 2

#define KURO_ROM_FLAG_BOOTED (1 << 0)
#define KURO_ROM_FLAG_IN_BATTLE (1 << 1)
#define KURO_ROM_FLAG_AWAITING_CHOICE (1 << 2)

enum KuroLinkRead
{
    KURO_LINK_READ_NONE,
    KURO_LINK_READ_OK,
    KURO_LINK_READ_CORRUPT,
};

struct KuroLink
{
    u8 magic[8];
    u16 protocolVersion;
    u8 romFlags;
    u8 bridgeFlags;
    u16 rxHead;
    u16 rxTail;
    u16 txHead;
    u16 txTail;
    u32 heartbeat;
    u8 reserved[8];
    u8 rx[KURO_RX_RING_SIZE];
    u8 tx[KURO_TX_RING_SIZE];
};

extern volatile struct KuroLink gKuroLink;

void KuroLink_Init(void);
void KuroLink_Tick(void);
void KuroLink_SetRomFlags(u8 flags);
// Reads one whole frame. A corrupt frame empties the RX ring, since its boundary is lost.
enum KuroLinkRead KuroLink_ReadFrame(u8 *opcode, u8 *payload, u16 *length);
// Returns the opcode of the next whole frame, or -1 if none is ready.
s32 KuroLink_PeekOpcode(void);
// Returns FALSE when the TX ring is full; the caller retries next frame and must not drop it.
bool32 KuroLink_SendFrame(u8 opcode, const u8 *payload, u16 length);

#endif // GUARD_KURO_LINK_H
