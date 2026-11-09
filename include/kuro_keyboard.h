#ifndef GUARD_KURO_KEYBOARD_H
#define GUARD_KURO_KEYBOARD_H

// The on-screen keyboard. Chat, PMs, login and names all open this one module.

#define KURO_UI_KEYBOARD 50
#define KURO_UI_SECURE_KEYBOARD 51 // Tools must not capture the screen while this is showing.
#define KURO_KEYBOARD_MAX_LENGTH 120
#define KURO_SECURE_MAX_LENGTH 64

#define KURO_SECURE_BACKSPACE 0xFE
#define KURO_SECURE_SUBMIT 0xFF
#define KURO_SECURE_CANCEL 0xFD

enum KuroKeyboardResult
{
    KURO_KEYBOARD_NONE,
    KURO_KEYBOARD_SENT,
    KURO_KEYBOARD_CLOSED,
};

struct KuroKeyboardOptions
{
    const u8 *title;
    u8 *text; // Starting text, replaced on send. Unused in secure mode.
    u8 limit; // Characters, not counting the terminator.
    bool8 secure; // Each key goes straight to the relay; only a count stays here.
    bool8 predictive;
    bool8 learn; // Remember the words sent, for the predictive row.
};

void KuroKeyboard_Open(const struct KuroKeyboardOptions *options);
void KuroKeyboard_Close(void);
bool32 KuroKeyboard_IsOpen(void);
void KuroKeyboard_Draw(u32 windowId);
// Call once a frame. Sets *dirty when the screen needs drawing.
enum KuroKeyboardResult KuroKeyboard_Update(bool32 *dirty);

#endif // GUARD_KURO_KEYBOARD_H
