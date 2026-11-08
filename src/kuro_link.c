// ROM side of the mailbox. The ROM produces the TX ring and consumes the RX ring.
#include "global.h"
#include "kuro_link.h"

#define FRAME_HEADER_SIZE 3
#define FRAME_CHECKSUM_SIZE 1

STATIC_ASSERT(sizeof(struct KuroLink) == 0x1000, KuroLinkIsExactly4KB);

volatile struct KuroLink gKuroLink __attribute__((section(".kurolink"), aligned(0x1000)));

static const u8 sMagic[8] = {'K', 'U', 'R', 'O', 'L', 'N', 'K', '1'};

static u16 RingUsed(u16 head, u16 tail, u16 size)
{
    return (head >= tail) ? (head - tail) : (size - tail + head);
}

static u8 RingPeek(const volatile u8 *ring, u16 tail, u16 size, u16 offset)
{
    return ring[(tail + offset) % size];
}

void KuroLink_Init(void)
{
    u32 i;
    volatile u8 *bytes = (volatile u8 *)&gKuroLink;

    for (i = 0; i < sizeof(struct KuroLink); i++)
        bytes[i] = 0;

    gKuroLink.protocolVersion = KURO_PROTOCOL_MAJOR;
    gKuroLink.romFlags = KURO_ROM_FLAG_BOOTED;

    // The magic goes in last, so the bridge never attaches to a half-written header.
    for (i = 0; i < ARRAY_COUNT(sMagic); i++)
        gKuroLink.magic[i] = sMagic[i];
}

void KuroLink_Tick(void)
{
    gKuroLink.heartbeat++;
}

void KuroLink_SetRomFlags(u8 flags)
{
    gKuroLink.romFlags = flags;
}

s32 KuroLink_PeekOpcode(void)
{
    u16 head = gKuroLink.rxHead;
    u16 tail = gKuroLink.rxTail;
    u16 used = RingUsed(head, tail, KURO_RX_RING_SIZE);
    u16 frameLength;

    if (used < FRAME_HEADER_SIZE)
        return -1;
    frameLength = RingPeek(gKuroLink.rx, tail, KURO_RX_RING_SIZE, 1)
                | (RingPeek(gKuroLink.rx, tail, KURO_RX_RING_SIZE, 2) << 8);
    if (used < FRAME_HEADER_SIZE + frameLength + FRAME_CHECKSUM_SIZE)
        return -1;
    return RingPeek(gKuroLink.rx, tail, KURO_RX_RING_SIZE, 0);
}

enum KuroLinkRead KuroLink_ReadFrame(u8 *opcode, u8 *payload, u16 *length)
{
    u16 head = gKuroLink.rxHead;
    u16 tail = gKuroLink.rxTail;
    u16 used = RingUsed(head, tail, KURO_RX_RING_SIZE);
    u16 frameLength, i;
    u8 checksum;

    if (used < FRAME_HEADER_SIZE)
        return KURO_LINK_READ_NONE;

    frameLength = RingPeek(gKuroLink.rx, tail, KURO_RX_RING_SIZE, 1)
                | (RingPeek(gKuroLink.rx, tail, KURO_RX_RING_SIZE, 2) << 8);

    if (frameLength > KURO_MAX_PAYLOAD)
    {
        gKuroLink.rxTail = head;
        return KURO_LINK_READ_CORRUPT;
    }

    if (used < FRAME_HEADER_SIZE + frameLength + FRAME_CHECKSUM_SIZE)
        return KURO_LINK_READ_NONE;

    checksum = 0;
    for (i = 0; i < FRAME_HEADER_SIZE; i++)
        checksum ^= RingPeek(gKuroLink.rx, tail, KURO_RX_RING_SIZE, i);
    for (i = 0; i < frameLength; i++)
    {
        payload[i] = RingPeek(gKuroLink.rx, tail, KURO_RX_RING_SIZE, FRAME_HEADER_SIZE + i);
        checksum ^= payload[i];
    }

    if (checksum != RingPeek(gKuroLink.rx, tail, KURO_RX_RING_SIZE, FRAME_HEADER_SIZE + frameLength))
    {
        gKuroLink.rxTail = head;
        return KURO_LINK_READ_CORRUPT;
    }

    *opcode = RingPeek(gKuroLink.rx, tail, KURO_RX_RING_SIZE, 0);
    *length = frameLength;
    gKuroLink.rxTail = (tail + FRAME_HEADER_SIZE + frameLength + FRAME_CHECKSUM_SIZE) % KURO_RX_RING_SIZE;
    return KURO_LINK_READ_OK;
}

bool32 KuroLink_SendFrame(u8 opcode, const u8 *payload, u16 length)
{
    u16 head = gKuroLink.txHead;
    u16 tail = gKuroLink.txTail;
    u16 used = RingUsed(head, tail, KURO_TX_RING_SIZE);
    u16 total = FRAME_HEADER_SIZE + length + FRAME_CHECKSUM_SIZE;
    u16 i, position = head;
    u8 checksum = opcode ^ (length & 0xFF) ^ (length >> 8);

#define PUT_BYTE(b) { gKuroLink.tx[position] = (b); position = (position + 1) % KURO_TX_RING_SIZE; }

    // One byte stays free so that head == tail always means empty.
    if (used + total >= KURO_TX_RING_SIZE)
        return FALSE;

    PUT_BYTE(opcode);
    PUT_BYTE(length & 0xFF);
    PUT_BYTE(length >> 8);
    for (i = 0; i < length; i++)
    {
        PUT_BYTE(payload[i]);
        checksum ^= payload[i];
    }
    PUT_BYTE(checksum);

#undef PUT_BYTE

    // Head advances last, so the bridge never reads a half-written frame.
    gKuroLink.txHead = position;
    return TRUE;
}
