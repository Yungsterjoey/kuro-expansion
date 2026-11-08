#ifndef GUARD_KURO_BUILDER_H
#define GUARD_KURO_BUILDER_H

// Team builder screens, drawn in the front-end menu window.

// gKuroUiScreen values for the builder start here.
#define KURO_UI_BUILDER_BASE 30

extern volatile u16 gKuroPickerValue;

bool32 KuroBuilder_IsOpen(void);
void KuroBuilder_Open(void);
void KuroBuilder_Draw(u32 windowId);
bool32 KuroBuilder_HandleInput(void);
bool32 KuroBuilder_Tick(void);
void KuroBuilder_ShowPopup(const u8 *text);

#endif // GUARD_KURO_BUILDER_H
