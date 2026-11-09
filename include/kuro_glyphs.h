#ifndef GUARD_KURO_GLYPHS_H
#define GUARD_KURO_GLYPHS_H

// The printable ASCII the base font lacks. Bytes 0x3B to 0x48 have no Latin glyph in the base sheets.
#define KURO_EXTRA_GLYPHS TRUE

#define CHAR_KURO_QUOTE 0x3B
#define CHAR_KURO_HASH 0x3C
#define CHAR_KURO_DOLLAR 0x3D
#define CHAR_KURO_ASTERISK 0x3E
#define CHAR_KURO_AT 0x3F
#define CHAR_KURO_LBRACKET 0x40
#define CHAR_KURO_BACKSLASH 0x41
#define CHAR_KURO_RBRACKET 0x42
#define CHAR_KURO_CARET 0x43
#define CHAR_KURO_UNDERSCORE 0x44
#define CHAR_KURO_BACKTICK 0x45
#define CHAR_KURO_LBRACE 0x46
#define CHAR_KURO_PIPE 0x47
#define CHAR_KURO_RBRACE 0x48

// Fills gCurGlyph and returns TRUE when glyphId is one of these characters.
bool32 KuroGlyphs_Decompress(u16 glyphId, u8 fontId);
// Returns the glyph's width, or -1 when glyphId is not one of these characters.
s32 KuroGlyphs_Width(u16 glyphId, u8 fontId);

#endif // GUARD_KURO_GLYPHS_H
