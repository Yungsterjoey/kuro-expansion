#ifndef GUARD_KURO_PLAYBACK_H
#define GUARD_KURO_PLAYBACK_H

// KURO: BATTLE_TYPE_30 is unused upstream, so puppet mode takes that flag.
#define BATTLE_TYPE_KURO_REMOTE BATTLE_TYPE_30

// M0 boots straight into the hardcoded fixture instead of the intro.
#define KURO_M0_PLAYBACK TRUE

enum KuroPlaybackStatus
{
    KURO_STATUS_RUNNING,
    KURO_STATUS_ENDED,
    KURO_STATUS_ERROR,
};

// Read by the headless harness in tools/regress.
extern volatile u16 gKuroAckSeq;
extern volatile u8 gKuroStatus;

void CB2_KuroM0Start(void);
void KuroPlayback_Main(void);

#endif // GUARD_KURO_PLAYBACK_H
