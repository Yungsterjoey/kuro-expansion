#ifndef GUARD_KURO_PLAYBACK_H
#define GUARD_KURO_PLAYBACK_H

// KURO: BATTLE_TYPE_30 is unused upstream, so puppet mode takes that flag.
#define BATTLE_TYPE_KURO_REMOTE BATTLE_TYPE_30

// M0 boots straight into the hardcoded fixture instead of the intro.
#define KURO_M0_PLAYBACK TRUE

// What the ROM runs at boot. tools/regress patches gKuroBootMode in a copy of the ROM.
enum KuroBootMode
{
    KURO_BOOT_FIXTURE_0,
    KURO_BOOT_FIXTURE_1,
    KURO_BOOT_WIRE_PLAYBACK,
    KURO_BOOT_LOOPBACK,
};

enum KuroPlaybackStatus
{
    KURO_STATUS_RUNNING,
    KURO_STATUS_ENDED,
    KURO_STATUS_ERROR,
};

// Read by the headless harness in tools/regress.
extern volatile u16 gKuroAckSeq;
extern volatile u8 gKuroStatus;
extern const volatile u8 gKuroBootMode;

void CB2_KuroBoot(void);
void KuroPlayback_Main(void);

#endif // GUARD_KURO_PLAYBACK_H
