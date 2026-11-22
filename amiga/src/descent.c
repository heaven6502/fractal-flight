/* descent.c - the descent from orbit (see descent.h), the Amiga version of st_descent.c: the same model and drawing, planar (plane 0 and plane 1 are separate bitplanes).

   The model of the game (jaggie.asm L5FB5 / L5FD8, checked against a save state of the original): 22 column heights h (16 bit, 1/256 rows, mirrored left / right: a dome), a step every two
   vertical blanks:  acc += $18,  h = max(0, h - 4 * (h >> 8) - (acc >> 8)).  The limb is drawn above the height: the light orange starts at row h >> 8, above it acc >> 8 rows of mid orange
   and one row of rust.  The heights are interpolated (Catmull Rom) to one height per pixel column, and between the steps in time.  The 43 rows of the game window are the 150 picture lines.
   Per picture line the three edges of the dome are three distances from the middle (the heights grow monotonically towards the edge), found by a sweep; the line is built from at most 7 spans.
   Only the lines of the limb are drawn: above it the screen is still black, below it a line that is light orange in the whole width is written once per buffer (fullFrom). */
#include "descent.h"

typedef UBYTE u8; typedef UWORD u16; typedef ULONG u32;
#define PIC_LINES DESC_LINES
#define NCOL   22
#define STEPS  206                                   /* the game stops at 190 with a strip of the limb still in the top lines; the steps to 206 take every height to 0: the window is light orange everywhere */
#define LS8    893                                   /* 150 / 43 lines per row, * 256 */

const u16 descPalette[16] = {                        /* the ST palette with 4 bit channels (v * 15 / 7) */
	0x0666, 0x0666, 0x0200, 0x0200,
	0x0000, 0x0920, 0x0D64, 0x0FB6,                  /* 4 sky = space | 5 rust | 6 mid orange | 7 light orange (the sky colour of the flight) */
	0x0000, 0x0920, 0x0D64, 0x0FB6,
	0x0DDD, 0x0920, 0x0D64, 0x0FB6                   /* 12 a star (plane 3) | 13 .. 15 the planet in front of it */
};

static const short startH[NCOL] = { 17022, 16643, 16264, 15923, 15581, 15278, 14975, 14710, 14445, 14217, 13990, 13800, 13611, 13459, 13308, 13194, 13080, 13004, 12929, 12891, 12853, 12853 };
static short stepH[STEPS + 1][NCOL];                 /* the heights after each step: 8.4 KB */
static short accAfter[STEPS + 1];
static const short crW[8][4] = {                     /* Catmull Rom weights * 1024 at t = (k + 0.5) / 8 */
	{ -28, 1014, 40, -2 }, { -63, 944, 158, -15 }, { -76, 821, 313, -34 }, { -71, 663, 487, -55 },
	{ -55, 487, 663, -71 }, { -34, 313, 821, -76 }, { -15, 158, 944, -63 }, { -2, 40, 1014, -28 }
};
#define NSTAR 18
static u8 starL[NSTAR], starG[NSTAR]; static u16 starB[NSTAR];
static short fullFrom[2] = { PIC_LINES, PIC_LINES };  /* per screen: the lines from here on are light orange all across */

void DescentInit(u8 *starPlane) {
	int s, c, i;
	u32 r = 0x2F6E2B1UL;
	short h[NCOL];
	int acc = 0;
	for (c = 0; c < NCOL; c++) { h[c] = startH[c]; stepH[0][c] = startH[c]; }
	accAfter[0] = 0;
	for (s = 1; s <= STEPS; s++) {
		acc += 0x18;
		for (c = 0; c < NCOL; c++) {
			int v = h[c] - 4 * (h[c] >> 8) - (acc >> 8);
			h[c] = (short)(v < 0 ? 0 : v);
			stepH[s][c] = h[c];
		}
		accAfter[s] = (short)acc;
	}
	fullFrom[0] = fullFrom[1] = PIC_LINES;
	for (i = 0; i < NSTAR; i++) {                    /* the stars: single pixels in the space part of the window */
		int x, y;
		r = r * 1664525UL + 1013904223UL; x = 8 + (int)((r >> 16) % 304);
		r = r * 1664525UL + 1013904223UL; y = 4 + (int)((r >> 16) % 100);
		starL[i] = (u8)y; starG[i] = (u8)(x >> 4); starB[i] = (u16)(0x8000 >> (x & 15));
		*(u16 *)(starPlane + y * DESC_STRIDE + (x >> 4) * 2) |= starB[i];
	}
}

static int yL8[164];                                 /* the first line of the light orange in 1/256 lines (a 16 bit value overflows: 175 lines * 256) */
static short yL[164], yT[164], yM[164];              /* per distance d from the middle (pixel d .. d + 1): the first line of the light orange, of the rust, of the mid orange */
static u32 lineBuf[20];

static void Span(int from, int to, u32 col) {        /* colour col (plane 0 in the high word, plane 1 in the low word) for the pixels from .. to - 1 of the line */
	int a, b;
	u32 ma, mb;
	if (from >= to) return;
	a = from >> 4; b = (to - 1) >> 4;
	ma = 0xFFFFu >> (from & 15);
	mb = (0xFFFFu << (15 - ((to - 1) & 15))) & 0xFFFFu;
	if (a == b) { u32 m = ma & mb; lineBuf[a] |= col & ((m << 16) | m); return; }
	lineBuf[a] |= col & ((ma << 16) | ma);
	for (a++; a < b; a++) lineBuf[a] = col;
	lineBuf[b] |= col & ((mb << 16) | mb);
}

void DescentFrame(u8 *p0, u8 *p1, int slot, int t) {
	int s8, s, f, c, k, d, y, accRows8, koff, dA = 0, dB = 0, dC = 0, y0, y1, full;
	short kn[NCOL + 2];
	if (t < DESC_HOLD) return;                       /* black space: nothing to draw */
	s8 = (t - DESC_HOLD) << 7;                       /* the step in 1/256: one step per two vertical blanks */
	if (s8 > STEPS * 256) s8 = STEPS * 256;
	s = s8 >> 8; f = s8 & 255;
	if (s >= STEPS) { s = STEPS - 1; f = 256; }
	for (c = 0; c < NCOL; c++) kn[c] = (short)(((short)stepH[s][c] * (256 - f) + (short)stepH[s + 1][c] * f) >> 8);
	kn[NCOL] = kn[NCOL - 1]; kn[NCOL + 1] = kn[NCOL - 1];         /* the neighbours beyond the middle: the same height (flat) */
	for (d = 0; d < 4; d++) yL8[d] = ((int)kn[NCOL - 1] * LS8) >> 8;
	for (c = 20; c >= 1; c--) {
		short p0 = kn[c - 1], p1 = kn[c], p2 = kn[c + 1], p3 = kn[c + 2];
		for (k = 0; k < 8; k++) {
			int v = ((p0 * crW[k][0] + p1 * crW[k][1] + p2 * crW[k][2] + p3 * crW[k][3]) >> 10);
			d = 171 - 8 * c - k;
			if (v < 0) v = 0;
			if (d < 164) yL8[d] = (v * LS8) >> 8;                   /* 1/256 lines */
		}
	}
	accRows8 = (s8 * 24) >> 8;                       /* acc >> 8 rows in 1/256, smooth in time */
	koff = ((accRows8 + 256) * LS8) >> 8;            /* the limb's top is that far (+ the row of rust) above the light orange, in 1/256 lines */
	for (d = 0; d < 160; d++) {
		int l8 = yL8[d], t8 = l8 - koff, ly = (l8 + 128) >> 8, ty = (t8 + 128) >> 8, my;
		if (d && ly < yL[d - 1]) ly = yL[d - 1];                       /* monotone: the dome only grows towards the edge (yL holds lines from here on) */
		if (d && ty < yT[d - 1]) ty = yT[d - 1];
		my = ty + 3; if (my > ly) my = ly;
		yL[d] = (short)ly; yT[d] = (short)ty; yM[d] = (short)my;
	}
	/* the lines to draw: from the first line the limb reaches to the last line that is not yet light orange on this screen */
	y0 = yT[0] < 0 ? 0 : yT[0];
	y1 = fullFrom[slot];
	for (y = y0; y < y1; y++) {
		u16 *d0, *d1;
		int xa, xb, xc, g;
		while (dA < 160 && yT[dA] <= y) dA++;
		while (dB < 160 && yM[dB] <= y) dB++;
		while (dC < 160 && yL[dC] <= y) dC++;
		d0 = (u16 *)(p0 + y * DESC_STRIDE); d1 = (u16 *)(p1 + y * DESC_STRIDE);
		if (dC >= 160) { for (g = 0; g < 20; g++) d0[g] = d1[g] = 0xFFFF; continue; }          /* light orange all across (the long write at +g*8: planes 0 and 1) */
		for (g = 0; g < 20; g++) lineBuf[g] = 0;
		xa = 160 - dA; xb = 160 - dB; xc = 160 - dC;
		Span(xa, xb, 0xFFFF0000UL);                  /* rust */
		Span(xb, xc, 0x0000FFFFUL);                  /* mid orange */
		Span(xc, 320 - xc, 0xFFFFFFFFUL);            /* light orange */
		Span(320 - xc, 320 - xb, 0x0000FFFFUL);
		Span(320 - xb, 320 - xa, 0xFFFF0000UL);
		for (g = 0; g < 20; g++) { d0[g] = (u16)(lineBuf[g] >> 16); d1[g] = (u16)lineBuf[g]; }
	}
	/* where does the light orange start on all of the width (the first line with dC = 160)? the lines from there on are done for this screen */
	full = yL[159];
	if (full < 0) full = 0;
	if (full > PIC_LINES) full = PIC_LINES;
	fullFrom[slot] = (short)full;
}
