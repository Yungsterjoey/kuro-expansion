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
    KURO_BOOT_CLIENT,
};

#define KURO_FIRST_BATTLE_OPCODE 0x40

// gKuroUiScreen values, read by the automated input in tools/regress.
enum KuroUiPhase
{
    KURO_UI_NONE,
    KURO_UI_ACTION,
    KURO_UI_MOVE,
    KURO_UI_PARTY,
    KURO_UI_FORFEIT,
    KURO_UI_WAITING,
    KURO_UI_MENU_BASE = 10,
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
extern volatile u8 gKuroUiScreen;
extern const volatile u8 gKuroBootMode;

void CB2_KuroBoot(void);
void KuroFreeBattleData(void);
void CB2_KuroMenuStart(void);
void KuroPlayback_StartClientBattle(u8 opcode, const u8 *payload, u16 length);
void KuroPlayback_StartClientResync(const u8 *battleStart, u16 length);
void KuroPlayback_Main(void);

#endif // GUARD_KURO_PLAYBACK_H
