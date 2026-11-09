#ifndef GUARD_KURO_TEAMS_H
#define GUARD_KURO_TEAMS_H

// Teams saved in the ROM. The member layout is the wire layout of UPLOAD_TEAM (docs/PROTOCOL.md).

#define KURO_TEAM_SLOTS 6
#define KURO_TEAM_SIZE 6
#define KURO_MON_NICK_SIZE 12
#define KURO_TEAM_NAME_SIZE 11
#define KURO_MON_WIRE_SIZE 43
#define KURO_NO_TEAM_SLOT 0xFF

struct __attribute__((packed)) KuroTeamMon
{
    u16 species;
    u16 item;
    u16 ability;
    u16 moves[MAX_MON_MOVES];
    u8 nature;
    u8 evs[NUM_STATS];
    u8 ivs[NUM_STATS];
    u8 level;
    u8 gender;
    u8 shiny;
    u8 teraType;
    u8 nickname[KURO_MON_NICK_SIZE];
};

enum KuroTeamBadge
{
    KURO_BADGE_UNCHECKED,
    KURO_BADGE_PASSED,
    KURO_BADGE_FAILED,
};

struct KuroTeam
{
    u8 name[KURO_TEAM_NAME_SIZE];
    u8 formatTag;
    u8 badge;
    u8 count;
    struct KuroTeamMon mons[KURO_TEAM_SIZE];
};

#define KURO_FORMAT_TAG_COUNT 3
#define KURO_RECENT_WORDS 16
#define KURO_RECENT_WORD_SIZE 12

extern struct KuroTeam gKuroTeams[KURO_TEAM_SLOTS];
extern u8 gKuroRecentWords[KURO_RECENT_WORDS][KURO_RECENT_WORD_SIZE];

void KuroTeams_Load(void);
bool32 KuroTeams_Save(void);
const u8 *KuroTeams_FormatTagName(u8 tag);
void KuroTeams_InitMon(struct KuroTeamMon *mon, u16 species);
void KuroTeams_DefaultName(u8 slot, u8 *name);
bool32 KuroTeams_Upload(u8 slot);
void KuroTeams_ApplyPush(const u8 *payload, u16 length);

#endif // GUARD_KURO_TEAMS_H
