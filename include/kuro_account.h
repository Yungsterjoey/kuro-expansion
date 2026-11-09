#ifndef GUARD_KURO_ACCOUNT_H
#define GUARD_KURO_ACCOUNT_H

// Sign-in and the account menu, drawn in the front-end menu window.

#define KURO_UI_ACCOUNT_BASE 60

enum KuroAccountEntry
{
    KURO_ACCOUNT_SIGN_IN, // The list of accounts, with no way back.
    KURO_ACCOUNT_MENU,    // The menu for the account in use.
};

enum KuroAccountResult
{
    KURO_ACCOUNT_STAY,
    KURO_ACCOUNT_DONE,
};

void KuroAccount_Open(enum KuroAccountEntry entry);
void KuroAccount_Abort(void);
void KuroAccount_Draw(u32 windowId);
enum KuroAccountResult KuroAccount_Update(bool32 *dirty);
void KuroAccount_ShowPopup(const u8 *text);

#endif // GUARD_KURO_ACCOUNT_H
