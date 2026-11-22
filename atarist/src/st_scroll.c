/* st_scroll.c - a tiny text scroller in a bar of the screen: 8 x 8 characters of the flight's own font (gfx/Antic.chr, ATASCII order), drawn into plane 1 only, prerotated (8 shifted copies of the strip), 1 pixel steps.
   The bar is plane 0 = $FFFF (colour 1); where a character pixel is set plane 1 is set too = colour 3 (dark), so the palette needs nothing new. */
#include "st_scroll.h"

#define SC_BLANK 41                                  /* blank characters in front of the text: a screen width (40 characters) and one more */

static const char defText[] =
#include "scrolltext.inc"
;

/* The text is only kept as it is (1 byte per character, any length).  The pre shifted pixels exist only for the visible part: a ring of SC_RING character columns, wsh[s][r][slot] = the 16 pixels of row r that start
   s pixels (0 .. 7) after the left edge of character column slot (a screen word is then a plain word copy).  Every column is stored twice (slot and slot + SC_RING) so that a window never wraps.  When the scroll
   crosses a character boundary the one new column is built from the font (8 shifts x 8 rows); that is all the work besides the copy. */
#define SC_RING 64
static u16 wsh[8][8][2 * SC_RING];
static const char *txt;                               /* the text (never copied) */
static int tlen;                                      /* characters of the text */
static u32 pos;                                       /* pixel position of the window in the endless strip: the blanks, the text, the blanks, the text ... */
static u32 filled;                                    /* the columns below this one are built */
#define SC_COLS 41                                    /* columns read by a window: 20 words of 16 pixels = 40 bytes, plus the byte after it */

extern const unsigned char fontAntic[64][8];          /* src/font_antic.c: the 8 x 8 font of the Atari 8 bit flight (ANTIC screen code order, 64 characters: no lower case) */

/* ASCII text -> the font index: the font is in screen code order = ATASCII 32 .. 95 (the index is the code minus 32); lower case letters show as capitals, everything else as a space */
static int FontIndex(int c) {
	if (c >= 32 && c <= 95) return c - 32;
	if (c >= 97 && c <= 122) return c - 64;
	return 0;
}

static u32 fillMod;                                   /* filled modulo the strip length (the blanks and the text), kept by counting: no division */
static int GlyphAtM(u32 m) {                          /* the character at the column m of the strip: SC_BLANK blanks, then the text */
	return m < SC_BLANK ? 0 : FontIndex((u8)txt[m - SC_BLANK]);
}

static void BuildColumn(void) {                       /* the 8 shifts x 8 rows of the column `filled` (its pixels and those of the next two characters), then the next column */
	u32 n = SC_BLANK + tlen, m1 = fillMod + 1, m2 = fillMod + 2;
	int g0 = GlyphAtM(fillMod), g1, g2, r, k;
	u32 slot = filled & (SC_RING - 1);
	if (m1 >= n) m1 -= n;
	if (m2 >= n) m2 -= n;
	g1 = GlyphAtM(m1); g2 = GlyphAtM(m2);
	for (r = 0; r < 8; r++) {
		u32 x = ((u32)fontAntic[g0][r] << 16) | ((u32)fontAntic[g1][r] << 8) | fontAntic[g2][r];
		for (k = 0; k < 8; k++) wsh[k][r][slot] = wsh[k][r][slot + SC_RING] = (u16)(x >> (8 - k));
	}
	filled++;
	if (++fillMod >= n) fillMod = 0;
}

void ScrollInit(const char *text) {
	if (!text) text = defText;
	for (tlen = 0; text[tlen]; tlen++) ;
	txt = text;
	pos = 0; filled = 0; fillMod = 0;
	while (filled < SC_COLS) BuildColumn();
}

#define W(g) d[4 * (g)] = q[2 * (g)];                /* word g of the line: plane 1 of the group g, 16 pixels from the byte 2 g on */
void ScrollTick(u8 *screen, int y0, int speed) {
	int r;
	u32 b = pos >> 3;
	const u16 (*sh)[2 * SC_RING] = wsh[pos & 7];
	u32 slot = b & (SC_RING - 1);
	while (filled < b + SC_COLS) BuildColumn();
	for (r = 0; r < 8; r++) {
		const u16 *q = &sh[r][slot];
		u16 *d = (u16 *)(screen + (y0 + r) * ST_LINE_BYTES) + 1;      /* the plane 1 word of every group */
		W(0) W(1) W(2) W(3) W(4) W(5) W(6) W(7) W(8) W(9) W(10) W(11) W(12) W(13) W(14) W(15) W(16) W(17) W(18) W(19)
	}
	pos += speed;
}

/* the same with the text in plane 3: the bar is plane 0 only (index 1, black in flight320), where a character pixel is plane 0 off and plane 3 on = index 8 (the sky colour of the picture's palette):
   no entry of the sun (2 .. 7) is used, so the planets cannot clash with the text. */
#define W3(g) d[4 * (g)] = (u16)~q[2 * (g)]; d[4 * (g) + 3] = q[2 * (g)];
void ScrollTickP3(u8 *screen, int y0, int speed) {
	int r;
	u32 b = pos >> 3;
	const u16 (*sh)[2 * SC_RING] = wsh[pos & 7];
	u32 slot = b & (SC_RING - 1);
	while (filled < b + SC_COLS) BuildColumn();
	for (r = 0; r < 8; r++) {
		const u16 *q = &sh[r][slot];
		u16 *d = (u16 *)(screen + (y0 + r) * ST_LINE_BYTES);
		W3(0) W3(1) W3(2) W3(3) W3(4) W3(5) W3(6) W3(7) W3(8) W3(9) W3(10) W3(11) W3(12) W3(13) W3(14) W3(15) W3(16) W3(17) W3(18) W3(19)
	}
	pos += speed;
}
