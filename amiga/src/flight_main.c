/* flight_main.c - the fractal flight of Behind Jaggi Lines! / Rescue on Fractalus on the Amiga 500 (OCS, 68000).
   flight_core.c is the verified flight model and terrain renderer in the geometry of the latest iron port (FAR: 55 vertices / 90 edges,
   D80: 80 rows, 40 byte columns = 160 pixels).  Its ridge pixels are plotted straight into bitplane 0 of the picture
   (PLOT_PLANAR: every pixel 2 bits wide and 3 rows tall = 320 x 240; row 0 is above the window, rows 76 .. 79 are the cockpit bar),
   the blitter clears that plane, then builds bitplane 1, the
   ground, by OR-ing every row with the ground row above it (a vertical drip of every ridge pixel downwards), then turns plane 0
   into "ridge line OR sky" (line OR NOT ground).  With (plane 1, plane 0):
     01 sky   10 ground   11 ridge line   00 cockpit bar and the border (colour 0, grey).
   The cockpit rows (the last F_ROWS - F_BAR0 picture rows) are cleared after plotting.
   The flight tick runs every second frame in the vertical blank interrupt, like the game.  Joystick port 2: left / right = bank,
   up = dive, down = climb.  Autopilot (a lazy S-curve that keeps clear of the ground) until the first stick movement.
   Left mouse button quits. */
#include "support/gcc8_c_support.h"
#include <proto/exec.h>
#include <graphics/gfxbase.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include "amiga_sys.h"
#include "blitq.h"
#include "flight_core.h"
#include "prof.h"
#include "logo_pal.h"
#include "descent.h"

/* ---- music: ThePlayer 6.1A (P61, Jarno Paananen), the no CIA variant: p61Music() is called once per vertical blank.  The module is a .p61 (a ProTracker module packed with P61con) in chip memory.
   Same wrappers as the Bartman template (main.c).  -DNO_MUSIC leaves it out.  The player needs INTF_EXTER (level 6) enabled. */
#ifndef NO_MUSIC
INCBIN(player, "music/player610.6.no_cia.bin")
INCBIN_CHIP(module, "music/testmod.p61")

static int p61Init(const void *mod) {                  // returns 0 if success, non-zero otherwise
	register volatile const void *_a0 ASM("a0") = mod;
	register volatile const void *_a1 ASM("a1") = NULL;
	register volatile const void *_a2 ASM("a2") = NULL;
	register volatile const void *_a3 ASM("a3") = player;
	register int _d0 ASM("d0");
	__asm volatile (
		"movem.l %%d1-%%d7/%%a4-%%a6,-(%%sp)\n"
		"jsr 0(%%a3)\n"
		"movem.l (%%sp)+,%%d1-%%d7/%%a4-%%a6"
	: "=r" (_d0), "+rf"(_a0), "+rf"(_a1), "+rf"(_a2), "+rf"(_a3)
	:
	: "cc", "memory");
	return _d0;
}

static void p61Music(void) {
	register volatile const void *_a3 ASM("a3") = player;
	register volatile const void *_a6 ASM("a6") = (void *)0xdff000;
	__asm volatile (
		"movem.l %%d0-%%d7/%%a0-%%a2/%%a4-%%a5,-(%%sp)\n"
		"jsr 4(%%a3)\n"
		"movem.l (%%sp)+,%%d0-%%d7/%%a0-%%a2/%%a4-%%a5"
	: "+rf"(_a3), "+rf"(_a6)
	:
	: "cc", "memory");
}

static void p61End(void) {
	register volatile const void *_a3 ASM("a3") = player;
	register volatile const void *_a6 ASM("a6") = (void *)0xdff000;
	__asm volatile (
		"movem.l %%d0-%%d1/%%a0-%%a1,-(%%sp)\n"
		"jsr 8(%%a3)\n"
		"movem.l (%%sp)+,%%d0-%%d1/%%a0-%%a1"
	: "+rf"(_a3), "+rf"(_a6)
	:
	: "cc", "memory");
}
#endif

#define P0_BYTES    (FSTRIDE * FROWS)            /* plane 0 */
#define P1_BYTES    (FSTRIDE * (FROWS + 1))      /* plane 1 has a zero row in front: the "ground above row 0" of the drip */
#define FRAME_BYTES (P0_BYTES + P1_BYTES)
#define DRIP_ROWS  (F_BAR0 * F_ROWREP)          /* the picture rows above the cockpit bar, F_ROWREP (2) screen rows each */
#define INNER_WORDS (FINNER / 2)                 /* picture bytes of a row, in words */
#define SHOW_ROW0   1                            /* the first row shown: row 0 (the top ridge value) is above the window */
#define SHOW_LINES  ((F_ROWS - SHOW_ROW0) * F_ROWREP)   /* 158 lines: 150 of picture (rows 1 .. 75) and the 8 lines of the cockpit bar, 2 x 2 screen pixels per picture pixel like the ST */
#ifndef SHOW_FPS
#define SHOW_FPS 1                               /* the picture rate in a top bar, like the ST: -DSHOW_FPS=0 for the final version */
#endif
#if SHOW_FPS
#define TOP_LINES   10                           /* lines of the top bar above the picture (grey, the number is drawn into plane 1) */
#else
#define TOP_LINES   0
#endif
#define TOTAL_LINES (TOP_LINES + SHOW_LINES)
#define VSTART      (44 + (256 - TOTAL_LINES) / 2)   /* PAL line of the first window line (the top bar): the window centred in the 256 lines (starting at line 44) */
#define PICSTART    (VSTART + TOP_LINES)         /* PAL line of the first picture line */

typedef struct { UBYTE *p0, *p1; } Frame;        /* p1 points at row 0 (after the zero row) */

/* three frames: one is shown, one may be finished and waiting for the next vertical blank (readyF), one may be in the blitter's finish chain
   (pendingF); the CPU draws into whichever is free.  So the picture rate is not tied to whole frames. */
static Frame fr[3];
static Frame *volatile shownF, *volatile readyF, *volatile pendingF, *volatile nextF;   // nextF: the frame whose clear is queued behind the finish chain
static void SetPlanes(const Frame *fp);                // copper list, below
static void SunWobble(int ph);                         // the heat shimmer of the sun, below
static void DescentPlanes(void);                       // the descent's buffer flip, below

/* ---- flight state, advanced in the vertical blank ---- */
static TFlight flight;
static volatile unsigned short frameCounter;
/* flight ticks per 5 vertical blanks: 3 = 30 ticks per second at 50 Hz = the pace of the game on a 60 Hz (NTSC) machine; 2.5 (every second frame) would be the PAL pace of 25 ticks per second.
   Speed, turning, wind and the autopilot's timing all scale with it. */
#ifndef TICKS_PER_5
#define TICKS_PER_5 3
#endif
static unsigned char tickAcc;


static volatile unsigned short tickStart, tickEnd;
static volatile int autopilot = 1;
static volatile int running;                           // 0 during the intro (the logo): the vertical blank handler runs the music only

/* sun state shared with the handler (the sun is a far object: see SunPx / SunDraw) */
static UBYTE *volatile sunCur, *volatile sunNext;      // the sun's plane set shown / the newest finished set
static volatile int sunCurDy, sunNextDy, sunDirty;
/* the entry fade (FadeSet below) */
#define FADE_MAX   16                                  /* the fade stops after this many steps (16 = all the way to the flight palette) */
#define FADE_PICS  8                                   /* the fade starts when this many flight pictures have been rendered after the first one */
static volatile int gFade, gFadeStep, gFadeWait;
static unsigned short gFadeT0;
static void FadeSet(int k);

static __attribute__((interrupt)) void irqHandler(void) {     // level 3: vertical blank and blitter finished
	UWORD j, porta = 0x0F, req = custom->intreqr;
	if (req & INTF_BLIT) {
		custom->intreq = INTF_BLIT; custom->intreq = INTF_BLIT;
		BlitNext();
	}
	if (!(req & INTF_VERTB)) return;
	custom->intreq = (1 << INTB_VERTB); custom->intreq = (1 << INTB_VERTB);   // twice for the A4000 bug
#ifndef NO_MUSIC
	p61Music();                                                               // first: the replay timing must not depend on what the rest of the handler does
#endif
	if (readyF) { SetPlanes(readyF); shownF = readyF; readyF = 0; }           // show the newest finished picture (the copper reads the pointers next frame)
	DescentPlanes();                                                          // the descent intro: show its newest picture
	if (sunCur != sunNext || sunCurDy != sunNextDy) { sunCur = sunNext; sunCurDy = sunNextDy; sunDirty = 1; }     // the sun drawn by the main loop (a new position)
	if (sunDirty || !(frameCounter & 3)) { SunWobble(frameCounter >> 2); sunDirty = 0; }                       // the heat shimmer of the sun: the lines of its planes (a few pointers in the copper list)
	if (gFade) {                                                              // the entry into the atmosphere: the colours cross fade from the yellow of the dust
		unsigned short n = (unsigned short)(frameCounter - gFadeT0);
		int k = (n * 13) >> 5;                                                // n / 2.46: 16 steps in 40 vertical blanks
		if (k > FADE_MAX) k = FADE_MAX;
		if (k != gFadeStep) { gFadeStep = k; FadeSet(k); if (k == FADE_MAX) gFade = 0; }
	}
	frameCounter++;
	if (!running) return;                                  // the intro is on: no flight yet
	j = custom->joy1dat;
	if ((j >> 1) & 1) porta &= ~F_RIGHT;
	if ((j >> 9) & 1) porta &= ~F_LEFT;
	if (((j >> 9) ^ (j >> 8)) & 1) porta &= ~F_UP;
	if (((j >> 1) ^ j) & 1) porta &= ~F_DOWN;
	if (porta != 0x0F) autopilot = 0;
	if (autopilot) porta = FAutopilot(&flight);            // the demo flight of the Atari 800 port (flight_core.c)
	tickAcc += TICKS_PER_5;
	if (tickAcc >= 5) {                                    // the game runs the flight tick every second frame (25 per second); here 30 per second, see TICKS_PER_5
		tickAcc -= 5;
		tickStart++;
		PROF(5, FlightTick(&flight, porta); GroundCollision(&flight); FWind(&flight));   // FWind: wind from the side, the picture rolls and flutters a little
		tickEnd++;
	}
}

static void Snapshot(TFlight *dst) {                   // a consistent copy of the flight state (the interrupt may tick meanwhile)
	unsigned short t;
	do {
		t = tickEnd;
		memcpy(dst, (const void *)&flight, sizeof(TFlight));
	} while (t != tickStart || t != tickEnd);
}

/* ---- blitter operations (the queue itself is in blitq.c) ---- */
#define MOD (FSTRIDE - 2 * INNER_WORDS)
#define SIZE_ALL ((DRIP_ROWS << 6) | INNER_WORDS)

static void OpClear(BlitOp *o, const Frame *f) {       // plane 0, the picture rows above the cockpit bar := 0
	o[0] = (BlitOp){ 0x0100, SIZE_ALL, MOD, 0, 0, f->p0 + FPAD };
}

static void OpsFinish(BlitOp *o, const Frame *f) {     // ground (plane 1) by the row-OR drip, cockpit rows cleared, then plane 0 := line OR sky
	o[0] = (BlitOp){ 0x0BFA, SIZE_ALL, MOD, f->p0 + FPAD, f->p1 + FPAD - FSTRIDE, f->p1 + FPAD };           // plane 1 row r := plane 0 row r OR plane 1 row r-1
	o[1] = (BlitOp){ 0x0100, ((FROWS - DRIP_ROWS) << 6) | INNER_WORDS, MOD, 0, 0, f->p0 + DRIP_ROWS * FSTRIDE + FPAD };   // ridge pixels plotted in the cockpit rows
	o[2] = (BlitOp){ 0x0BF5, SIZE_ALL, MOD, f->p0 + FPAD, f->p1 + FPAD, f->p0 + FPAD };                    // plane 0 := plane 0 OR NOT plane 1
}

static void QueueClear(const Frame *f) {
	blitMarkAt = 0;
	OpClear(bq, f);
	BlitRun(1);
}

static void MarkReady(void) {                          // interrupt: the finish part of the chain is done, the picture can be shown at the next vertical blank
	if (pendingF) { readyF = pendingF; pendingF = 0; }
}

static void QueueFinish(Frame *f, Frame *next) {       // the finish chain of f; behind it (if there is a free frame) the clear of the next frame, so that it overlaps with the CPU work
	OpsFinish(bq, f);
	pendingF = f;
	nextF = next;
	blitMarkFn = MarkReady;
	if (next) { OpClear(bq + 3, next); blitMarkAt = 3; BlitRun(4); }
	else { blitMarkAt = 0; blitChainDone = MarkReady; BlitRun(3); }
}

static Frame *FreeFrame(void) {                        // a frame that is neither shown, nor waiting, nor in the blitter chain, nor reserved
	for (;;)
		for (int i = 0; i < 3; i++)
			if (&fr[i] != shownF && &fr[i] != readyF && &fr[i] != pendingF && &fr[i] != nextF) return &fr[i];
}

static Frame *TryFreeFrame(const Frame *except) {      // the same, without waiting (0 if none)
	for (int i = 0; i < 3; i++)
		if (&fr[i] != except && &fr[i] != shownF && &fr[i] != readyF && &fr[i] != pendingF && &fr[i] != nextF) return &fr[i];
	return 0;
}

/* ---- picture rate in the cockpit bar (SHOW_FPS = 1, default; -DSHOW_FPS=0 for the final version): pictures per second x 10, 4 digits of a 3 x 5 font, every font pixel 2 screen pixels wide and 1 line tall,
   drawn into plane 1 of a top bar above the picture (the copper shows it first, then switches the planes to the picture frame): colour 2 = the ground colour on the grey bar ---- */
#ifndef SHOW_FPS
#define SHOW_FPS 1
#endif
#if SHOW_FPS
static const UBYTE font35[10][5] = {      /* 3 x 5 digits, bit 2 = left */
	{7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7}
};
#define FPS_X  8                                       /* first screen pixel (x) of the number */
#define FPS_ROW0 2                                     /* first line of the digits in the top bar */
static UBYTE *topBar0, *topBar1;                       /* the top bar: 2 planes of TOP_LINES lines (plane 0 stays zero) */
static void DrawFps(int value) {
	int d, r, c;
	for (r = 0; r < 5; r++) {                          // clear the 4 digits: 4 x 8 pixels = 4 bytes
		UBYTE *row = topBar1 + (FPS_ROW0 + r) * FSTRIDE + FPAD + (FPS_X >> 3);
		row[0] = row[1] = row[2] = row[3] = 0;
	}
	for (d = 0; d < 4; d++) {
		int div = d == 0 ? 1000 : d == 1 ? 100 : d == 2 ? 10 : 1, n = (value / div) % 10;
		if (value < div && d != 3) continue;           // blank leading zeros
		for (r = 0; r < 5; r++)
			for (c = 0; c < 3; c++)
				if ((font35[n][r] >> (2 - c)) & 1) {
					int x = FPS_X + d * 8 + c * 2;
					UBYTE *p = topBar1 + (FPS_ROW0 + r) * FSTRIDE + FPAD + (x >> 3);
					*p |= (UBYTE)(0xC0 >> (x & 7));
				}
	}
}
#endif

/* ---- the intro: the logo (320 x 200, 32 colours, 5 bitplanes, gfx/desire_logo32c.png) faded in and out before the flight, like on the Atari ST.  Its own copper list; the flight and its tick
   wait (running = 0) until the fade out is done. ---- */
INCBIN_CHIP(logoPlanes, "gfx/desire_logo32c.bpl")
#define LOGO_HOLD 300                                  /* vertical blanks at full brightness: 40 (fade in) + 300 + 40 (fade out) = 380 = the same as the Atari ST (the flight appears at PLAY call 380); the joystick fire skips the rest of the intro */
#define FIRE() (!(*(volatile UBYTE *)0xbfe001 & 0x80))
static UWORD *logoCop, *logoColors;                    // the logo copper list, and its first colour value

static void BuildLogoCopper(UWORD *p) {
	#define CMD(reg, val) do { *p++ = offsetof(struct Custom, reg); *p++ = (val); } while (0)
	const int vstart = 44 + (256 - 200) / 2;            // 200 lines centred in the 256 lines of a PAL screen
	CMD(diwstrt, (vstart << 8) | 0x81);
	CMD(diwstop, (((vstart + 200) & 0xff) << 8) | 0xc1);
	CMD(ddfstrt, 0x0038); CMD(ddfstop, 0x00d0);          // 20 words per row
	CMD(bplcon0, (5 << 12) | (1 << 9));                  // 5 bitplanes: 32 colours
	CMD(bplcon1, 0); CMD(bplcon2, 0);
	CMD(bpl1mod, 0); CMD(bpl2mod, 0);
	for (int k = 0; k < 5; k++) {
		ULONG a = (ULONG)logoPlanes + k * 8000;
		*p++ = offsetof(struct Custom, bplpt[k]);     *p++ = (UWORD)(a >> 16);
		*p++ = offsetof(struct Custom, bplpt[k]) + 2; *p++ = (UWORD)a;
	}
	logoColors = p + 1;                                  // the value of colour 0 (black until the fade in)
	for (int k = 0; k < 32; k++) { *p++ = offsetof(struct Custom, color[k]); *p++ = 0; }
	*p++ = 0xffff; *p++ = 0xfffe;
	#undef CMD
}

static void LogoFade(int level) {                      // 0 = black .. 8 = the palette (4 bit channels scaled in 8 steps)
	for (int k = 0; k < 32; k++) {
		int r = (logoPal[k] >> 8) & 15, g = (logoPal[k] >> 4) & 15, b = logoPal[k] & 15;
		logoColors[k * 2] = (UWORD)((((r * level + 4) >> 3) << 8) | (((g * level + 4) >> 3) << 4) | ((b * level + 4) >> 3));
	}
}

static int WaitFrames(int n) {                         // 1 = the fire button was pressed
	unsigned short t = frameCounter;
	while ((unsigned short)(frameCounter - t) < (unsigned short)n) if (FIRE()) return 1;
	return 0;
}

static void LogoIntro(void) {                          // fade in (8 steps of 5 vertical blanks), hold, fade out
	int level, skip = 0;
	unsigned short t;
	for (level = 1; level <= 8 && !skip; level++) { LogoFade(level); skip = WaitFrames(5); }
	t = frameCounter;
	while (!skip && (unsigned short)(frameCounter - t) < LOGO_HOLD && !MouseLeft()) if (FIRE()) skip = 1;
	for (level = 7; level >= 0; level--) { LogoFade(level); WaitFrames(5); }
}

/* ---- the sun: four colours without dither in the top right corner (the same as on the Atari ST): the sky and three solid rings (halo, body, core).  Two more bitplanes (planes 2 and 3) are switched on
   by the copper only for the lines of the sun: the picture bits (planes 0 and 1: ground 10, ridge line 11, sky 01) pick the colour inside a group of four, planes 2 and 3 the group: outside the sun group 0,
   the halo group 1 (plane 2), the body group 2 (plane 3), the core group 3 (both).  The sky colour of group g is the colour 4 g + 1: the sun's ring colours; the ground and ridge line entries of the groups are
   the same as in group 0, so the mountains are in front of the sun.
   The sun is a far object (as on the ST): it stays at its azimuth in the world, so it moves sideways when the plane turns (x = 160 + 192 * tan(0.75 * 0.25 * (azimuth - heading)), SunPx) and leaves the
   picture when the turn is far enough; the pitch moves it up and down (dy lines).  The planes hold the 23 scanlines of the circle at the current x (SunDraw redraws them when x changes, in one of two sets so that the beam
   never sees a half drawn one); the copper points every line of the band at the row of the circle that the heat shimmer picks (SunWobble), shifted by dy.  A circle of 14 pixels (a line is 1.2 pixels high). ---- */
#define SUN_L0 0                                     /* the first picture line with 4 bitplanes */
#define SUN_L1 46                                    /* ... and the line after the last: the sun's lines T .. B moved by dy (-6 .. 17) */
#define SUN_BAND (SUN_L1 - SUN_L0)
#define SUN_CY 17                                    /* the scanline of the centre of the sun */
#define SUN_T 6                                      /* the first and the last scanline of the circle (radius 14 pixels = 11.7 lines around SUN_CY) */
#define SUN_B 28
#define SUN_N (SUN_B - SUN_T + 1)
#define SUN_ROWS (SUN_N + 1)                         /* rows of a sun plane: the lines of the circle and one empty row at the end */
#define SUN_SET (2 * SUN_ROWS * 40)                  /* bytes of one set of planes 2 and 3 */
static UBYTE *sunPlanes;                             /* chip memory: two sets of plane 2 and plane 3, SUN_ROWS rows of 40 bytes each (the last row of each plane is empty) */
static UWORD *sunCop[SUN_BAND];                      /* in the copper list: the pointer moves of the planes 2 and 3 of every line of the band */
static const signed char sunS1[64] = { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0 };      /* 1.2 * sin and 0.8 * sin: the two sines that pick the scanline (the same as on the Atari ST) */
static const signed char sunS2[64] = { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0 };
static const short sunTan[72] = { 0, 2, 5, 7, 9, 12, 14, 17, 19, 21, 24, 26, 28, 31, 33, 36, 38, 41, 43, 46, 48, 51, 53, 56, 58, 61, 63, 66, 69, 71, 74, 77, 80, 82, 85, 88, 91, 94, 97, 100, 103, 106, 109, 112, 115, 118, 122, 125, 128, 132, 135, 139, 142, 146, 150, 154, 158, 162, 166, 170, 174, 178, 183, 187, 192, 197, 202, 207, 212, 217, 223, 228 };      /* 192 * tan(angle): the x offset in pixels of a far object at an angle of i * 32 / 16384 turns from the view direction (96 columns = 192 pixels per unit of tan) */
static signed char sunHw[3][SUN_N];                  /* per scanline of the circle: the half width of the halo + body + core (radius 14 / 10 / 6), -1 = none */
static int sunSetPx[2] = { 9999, 9999 };             /* the x each set is drawn for */

static void BuildSunTables(void) {
	static const int lim[3] = { 196, 100, 36 };
	for (int l = 0; l < SUN_N; l++) {
		int dy = SUN_T + l - SUN_CY, ddy = (dy * dy * 36) / 25;
		for (int k = 0; k < 3; k++) {
			int w = -1;
			for (int d = 0; d * d + ddy <= lim[k]; d++) w = d;
			sunHw[k][l] = (signed char)w;
		}
	}
}

#define SUN_AZ0 2239                                   /* the start azimuth left of the view direction: 1679 units (37 degrees = x 16, the top left corner) / the factor 0.75 */
static int sunAz, sunAzSet;                            /* the azimuth of the sun in heading units (a full turn = $4000): the angle is scaled by 0.75 below (the sun is millions of km away: it pans slower than the terrain) */
static int SunPx(int head) {                           /* the x of the centre of the sun on the screen for this heading, 9999 = not on the screen */
	int ang, a, i, f, o;
	if (!sunAzSet) { sunAz = head - SUN_AZ0; sunAzSet = 1; }
	ang = ((short)((sunAz - head) << 2)) >> 2;         /* the angle from the view direction, 14 bit signed */
	ang -= ang >> 2;                                   /* * 0.75: a sun at infinity would turn exactly with the view (1.0); slightly slower looks better */
	a = ang < 0 ? -ang : ang;
	i = a >> 5; f = a & 31;
	if (i >= 70) return 9999;
	o = sunTan[i] + (((sunTan[i + 1] - sunTan[i]) * f) >> 5);
	return 160 + (ang < 0 ? -o : o);
}

static void SunDraw(int px, int dy) {                  // main loop: draw the circle at x = px into the set that is not shown, then hand it to the vertical blank
	static int set;
	UBYTE *base;
	set ^= 1;
	base = sunPlanes + set * SUN_SET;
	if (dy < -6) dy = -6;
	if (dy > 17) dy = 17;
	if (sunSetPx[set] != px) {
		UBYTE *p2 = base, *p3 = base + SUN_ROWS * 40;
		memset(p2, 0, SUN_N * 40); memset(p3, 0, SUN_N * 40);          // (the last row of each plane stays empty)
		if (px < 2000)
			for (int l = 0; l < SUN_N; l++) {
				int wa = sunHw[0][l], wb = sunHw[1][l], wc = sunHw[2][l];
				for (int x = px - wa; wa >= 0 && x <= px + wa; x++) {
					int d = x > px ? x - px : px - x;
					if (x < 0 || x > 319) continue;
					if (d > wb || d <= wc) p2[l * 40 + (x >> 3)] |= 0x80 >> (x & 7);        // halo and core: plane 2
					if (d <= wb) p3[l * 40 + (x >> 3)] |= 0x80 >> (x & 7);                   // body and core: plane 3
				}
			}
		sunSetPx[set] = px;
	}
	sunNextDy = dy;
	sunNext = base;
}

/* The heat shimmer of the hot Mars (done by the copper, no pixels move): in every line of the band the copper loads the pointers of the planes 2 and 3; SunWobble points them to the row of the circle that the sum of
   two sines picks (+-1 and +-1 lines at the peaks of the second, wave lengths 16 and 7 lines, running with the phase, 16 phases), so that scanlines of the sun are skipped or doubled and it wobbles in y.  A line outside
   the sun, or one that picks a line outside it, points to the empty row: the top and the bottom of the sun are never doubled. */
static void SunWobble(int ph) {
	UBYTE *base = sunCur;
	int dy = sunCurDy;
	ph &= 15;
	for (int l = 0; l < SUN_BAND; l++) {
		int L = SUN_L0 + l - dy, src = L + sunS1[(L * 4 + ph * 4) & 63] + sunS2[(L * 9 - ph * 8) & 63];
		int row = (L >= SUN_T && L <= SUN_B && src >= SUN_T && src <= SUN_B) ? src - SUN_T : SUN_N;
		ULONG a2 = (ULONG)base + row * 40, a3 = (ULONG)base + SUN_ROWS * 40 + row * 40;
		UWORD *c = sunCop[l];
		c[1] = (UWORD)(a2 >> 16); c[3] = (UWORD)a2; c[5] = (UWORD)(a3 >> 16); c[7] = (UWORD)a3;
	}
}

/* ---- the descent from orbit (descent.c) between the logo and the flight: its own copper list, 4 bitplanes (16 colours: descPalette).  Planes 0 and 1 are the picture (two buffers, flipped in the vertical blank),
   plane 2 is set in the picture lines, plane 3 holds the stars.  The window is the same as the flight's (top bar, 150 picture lines, 8 lines of the cockpit bar, all grey = colour 0). ---- */
#define DESC_ROW0 TOP_LINES                            /* the first picture line in the descent's planes */
#define DESC_PLANE (TOTAL_LINES * 40)
static UBYTE *descMem, *descP0[2], *descP1[2];         /* the two buffers (plane 0 / plane 1, from the top bar) */
static UWORD *descCop, *descCopPl;
static volatile int descPend;                          /* 0, or the buffer to show + 1 (set by the main loop, taken in the vertical blank) */

static void DescentPlanes(void) {                      // vertical blank
	if (descPend) {
		ULONG a = (ULONG)descP0[descPend - 1], b = (ULONG)descP1[descPend - 1];
		descCopPl[1] = (UWORD)(a >> 16); descCopPl[3] = (UWORD)a;
		descCopPl[5] = (UWORD)(b >> 16); descCopPl[7] = (UWORD)b;
		descPend = 0;
	}
}

static void BuildDescentCopper(UWORD *p) {
	#define CMD(reg, val) do { *p++ = offsetof(struct Custom, reg); *p++ = (val); } while (0)
	CMD(diwstrt, (VSTART << 8) | 0x81);
	CMD(diwstop, (((VSTART + TOTAL_LINES) & 0xff) << 8) | 0xc1);
	CMD(ddfstrt, 0x0038); CMD(ddfstop, 0x00d0);
	CMD(bplcon0, (4 << 12) | (1 << 9));
	CMD(bplcon1, 0); CMD(bplcon2, 0);
	CMD(bpl1mod, 0); CMD(bpl2mod, 0);
	descCopPl = p;
	for (int k = 0; k < 2; k++) { *p++ = offsetof(struct Custom, bplpt[k]); *p++ = 0; *p++ = offsetof(struct Custom, bplpt[k]) + 2; *p++ = 0; }
	for (int k = 2; k < 4; k++) {
		ULONG a = (ULONG)descMem + 2 * 2 * DESC_PLANE + (k - 2) * DESC_PLANE;           // plane 2, plane 3 after the two buffers
		*p++ = offsetof(struct Custom, bplpt[k]);     *p++ = (UWORD)(a >> 16);
		*p++ = offsetof(struct Custom, bplpt[k]) + 2; *p++ = (UWORD)a;
	}
	for (int k = 0; k < 16; k++) { *p++ = offsetof(struct Custom, color[k]); *p++ = descPalette[k]; }
	*p++ = 0xffff; *p++ = 0xfffe;
	#undef CMD
}

/* ---- the entry into the atmosphere (measured in the Atari original): when the flight starts the whole window is the yellow of the dust (228, 158, 62) and every colour fades from that yellow into its final
   one within about 40 vertical blanks.  Here: the colours of the flight's copper list (all but the grey of the bar: 0, 4, 8, 12) start all yellow and cross fade to their final values in 16 steps
   (the handler: gFade, 40 vertical blanks); the yellow is the light orange of the end of the descent, so there is no jump when the flight starts. ---- */
#define FADE_YELLOW 0x0FB6
static UWORD *copColors;                               /* in the flight's copper list: color[0] (the values are at 2 k + 1) */
static UWORD finalPal[16];
static void FadeSet(int k) {
	for (int i = 1; i < 16; i++) {
		int v = 0;
		if (!(i & 3) || (i & 3) == 0) continue;
		for (int sh = 8; sh >= 0; sh -= 4) {
			int a = (FADE_YELLOW >> sh) & 15, f = (finalPal[i] >> sh) & 15;
			v |= (a + (((f - a) * k) >> 4)) << sh;
		}
		copColors[i * 2 + 1] = (UWORD)v;
	}
}

/* ---- copper list ---- */
static UWORD *copper;
static UWORD *copPlanes;

static void SetPlanes(const Frame *fp) {
	ULONG a = (ULONG)(fp->p0 + SHOW_ROW0 * F_ROWREP * FSTRIDE), b = (ULONG)(fp->p1 + SHOW_ROW0 * F_ROWREP * FSTRIDE);
	copPlanes[1] = (UWORD)(a >> 16); copPlanes[3] = (UWORD)a;
	copPlanes[5] = (UWORD)(b >> 16); copPlanes[7] = (UWORD)b;
}

static void BuildCopper(UWORD *p) {
	#define CMD(reg, val) do { *p++ = offsetof(struct Custom, reg); *p++ = (val); } while (0)
	CMD(diwstrt, (VSTART << 8) | 0x81);                  // 320 x (10 + 158) lowres
	CMD(diwstop, (((VSTART + TOTAL_LINES) & 0xff) << 8) | 0xc1);
	CMD(ddfstrt, 0x0038); CMD(ddfstop, 0x00d0);          // 20 words per row
	CMD(bplcon0, (2 << 12) | (1 << 9));
	CMD(bplcon1, 0); CMD(bplcon2, 0);
	CMD(bpl1mod, 0); CMD(bpl2mod, 0);
#if SHOW_FPS
	*p++ = offsetof(struct Custom, bplpt[0]);     *p++ = (UWORD)((ULONG)topBar0 >> 16);       // the top bar first: plane 0 = zero (grey), plane 1 = the number
	*p++ = offsetof(struct Custom, bplpt[0]) + 2; *p++ = (UWORD)(ULONG)topBar0;
	*p++ = offsetof(struct Custom, bplpt[1]);     *p++ = (UWORD)((ULONG)topBar1 >> 16);
	*p++ = offsetof(struct Custom, bplpt[1]) + 2; *p++ = (UWORD)(ULONG)topBar1;
#else
	copPlanes = p;
	*p++ = offsetof(struct Custom, bplpt[0]);     *p++ = 0;
	*p++ = offsetof(struct Custom, bplpt[0]) + 2; *p++ = 0;
	*p++ = offsetof(struct Custom, bplpt[1]);     *p++ = 0;
	*p++ = offsetof(struct Custom, bplpt[1]) + 2; *p++ = 0;
#endif
	copColors = p;
	CMD(color[0], 0x0666);                               // 00 cockpit bar, pads and the border: grey (the ST $0333)
	CMD(color[1], 0x0FB6);                               // 01 sky: the butterscotch of the Mars sky (the ST $0753; ST 3 bit value v -> Amiga 4 bit round(v * 15 / 7))
	CMD(color[2], 0x0964);                               // 10 ground: brown (the ST $0432)
	CMD(color[3], 0x0200);                               // 11 ridge line: dark brown (the ST $0100)
	CMD(color[4], 0x0666); CMD(color[5], 0x0FDB); CMD(color[6], 0x0964); CMD(color[7], 0x0200);         // group 1 (plane 2): the halo of the sun (the ST $0765), ground, ridge line as in group 0
	CMD(color[8], 0x0666); CMD(color[9], 0x0FF6); CMD(color[10], 0x0964); CMD(color[11], 0x0200);       // group 2 (plane 3): the body of the sun (yellow, the ST $0773)
	CMD(color[12], 0x0666); CMD(color[13], 0x0FFF); CMD(color[14], 0x0964); CMD(color[15], 0x0200);     // group 3 (both): the core (white, the ST $0777)
	for (int k = 0; k < 16; k++) finalPal[k] = copColors[k * 2 + 1];
#if SHOW_FPS
	*p++ = (UWORD)(((PICSTART - 1) << 8) | 0xDF); *p++ = 0xFFFE;   // end of the line before the picture: the planes of the picture frame
	copPlanes = p;
	*p++ = offsetof(struct Custom, bplpt[0]);     *p++ = 0;
	*p++ = offsetof(struct Custom, bplpt[0]) + 2; *p++ = 0;
	*p++ = offsetof(struct Custom, bplpt[1]);     *p++ = 0;
	*p++ = offsetof(struct Custom, bplpt[1]) + 2; *p++ = 0;
#endif
	for (int l = 0; l < SUN_BAND; l++) {                                          // the lines of the sun: at the end of the line before each one the pointers of the planes 2 and 3 (SunWobble sets them) ...
		*p++ = (UWORD)(((PICSTART + SUN_L0 + l - 1) << 8) | 0xDF); *p++ = 0xFFFE;
		if (l == 0) CMD(bplcon0, (4 << 12) | (1 << 9));                         // ... and 4 bitplanes in the first
		sunCop[l] = p;
		*p++ = offsetof(struct Custom, bplpt[2]);     *p++ = 0;
		*p++ = offsetof(struct Custom, bplpt[2]) + 2; *p++ = 0;
		*p++ = offsetof(struct Custom, bplpt[3]);     *p++ = 0;
		*p++ = offsetof(struct Custom, bplpt[3]) + 2; *p++ = 0;
	}
	*p++ = (UWORD)(((PICSTART + SUN_L1 - 1) << 8) | 0xDF); *p++ = 0xFFFE;       // after the last line 2 bitplanes again
	CMD(bplcon0, (2 << 12) | (1 << 9));
	*p++ = 0xffff; *p++ = 0xfffe;
	#undef CMD
}

int main(void) {
	TFlight snap;
	if (SysOpen()) return 20;

	UBYTE *mem = (UBYTE *)AllocMem(3 * FRAME_BYTES, MEMF_CHIP | MEMF_CLEAR);       // three frames, plane 0 + plane 1 each
	copper = (UWORD *)AllocMem(2048, MEMF_CHIP | MEMF_CLEAR);
	sunPlanes = (UBYTE *)AllocMem(2 * SUN_SET, MEMF_CHIP | MEMF_CLEAR);
	descMem = (UBYTE *)AllocMem(6 * DESC_PLANE, MEMF_CHIP | MEMF_CLEAR);        // two buffers of plane 0 + plane 1, plane 2, plane 3
	descCop = (UWORD *)AllocMem(256, MEMF_CHIP | MEMF_CLEAR);
	logoCop = (UWORD *)AllocMem(256, MEMF_CHIP | MEMF_CLEAR);
	if (!mem || !copper || !logoCop || !sunPlanes || !descMem || !descCop) return 20;
	BuildSunTables();
	sunCur = sunNext = sunPlanes;
	for (int i = 0; i < 3; i++) {
		fr[i].p0 = mem + i * FRAME_BYTES;
		fr[i].p1 = mem + i * FRAME_BYTES + P0_BYTES + FSTRIDE;
	}

	FInit();                                             // tables of the assembly line routine
	FSeed(20261106);
	InitGridMap();
	StartFlight(&flight);
	flight.speedCfg = FLIGHT_THRUST;                     // faster than the game's own start thrust of 30
#if SHOW_FPS
	topBar0 = (UBYTE *)AllocMem(2 * TOP_LINES * FSTRIDE, MEMF_CHIP | MEMF_CLEAR);
	if (!topBar0) return 20;
	topBar1 = topBar0 + TOP_LINES * FSTRIDE;
#endif
	BuildCopper(copper);
	SunWobble(0);                                        // the first pointers of the sun's planes
	BuildLogoCopper(logoCop);
	for (int i = 0; i < 2; i++) { descP0[i] = descMem + (2 * i) * DESC_PLANE; descP1[i] = descMem + (2 * i + 1) * DESC_PLANE; }
	for (int y = 0; y < DESC_LINES; y++) memset(descMem + 4 * DESC_PLANE + (DESC_ROW0 + y) * 40, 0xFF, 40);       // plane 2: the picture lines use the colours 4 .. 7
	DescentInit(descMem + 5 * DESC_PLANE + DESC_ROW0 * 40);
	BuildDescentCopper(descCop);
	shownF = &fr[0];
	SetPlanes(shownF);
	blitChainDone = 0;
	fBeforePlot = WaitBlt;                               // RenderPlot waits for the blitter clear before the first pixel

#ifndef NO_MUSIC
	if (p61Init(module) != 0) return 20;                  // before TakeSystem, like the template
#endif
	TakeSystem();
	custom->cop1lc = (ULONG)logoCop;                      // the intro first (black, then faded in)
	custom->dmacon = DMAF_BLITTER;
	custom->copjmp1 = 0x7fff;
	custom->dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER | DMAF_BLITTER;
	SetVblHandler(irqHandler);
	custom->intena = INTF_SETCLR | INTF_BLIT;
#ifndef NO_MUSIC
	custom->intena = INTF_SETCLR | INTF_EXTER;            // ThePlayer needs INTF_EXTER
#endif

	LogoIntro();                                          // the music plays meanwhile (the handler)
	WaitVbl();
	{                                                     // the descent from orbit: space with stars, then the limb of the planet rises, the last picture is the colour of the flight's sky
		int dcur = 0;
		unsigned short t0;
		descPend = 1;                                     // the first buffer (black picture)
		custom->cop1lc = (ULONG)descCop;
		custom->copjmp1 = 0x7fff;
		t0 = frameCounter;
		while (!MouseLeft() && (unsigned short)(frameCounter - t0) < DESC_HOLD - 1 && !FIRE()) ;
		for (;;) {
			int t;
			while (descPend) ;                            // the previous picture is flipped in
			t = (unsigned short)(frameCounter - t0) + 1;  // the picture is on display from the next vertical blank
			if (MouseLeft() || t >= DESC_END || FIRE()) break;
			DescentFrame(descP0[dcur] + DESC_ROW0 * 40, descP1[dcur] + DESC_ROW0 * 40, dcur, t);
			descPend = dcur + 1;
			dcur ^= 1;
		}
	}
	ProfInit();
#if SHOW_FPS
	unsigned short fpsLast = frameCounter; int fpsRenders = 0, fps10 = 0;
#endif
	while (!MouseLeft()) {
#ifdef PROFILE
		unsigned short f0 = frameCounter;
#endif
		Frame *f;
		if (nextF) { f = nextF; nextF = 0; }               // its clear was queued behind the previous finish chain
		else { f = FreeFrame(); QueueClear(f); }
		fPlane = f->p0;
#if SHOW_FPS
		fpsRenders++;
		if ((unsigned short)(frameCounter - fpsLast) >= 100) { fps10 = fpsRenders * 5; fpsRenders = 0; fpsLast = frameCounter; }   // pictures per 2 seconds x 5
		{ static int fpsShown = -1; if (fps10 != fpsShown) { DrawFps(fps10); fpsShown = fps10; } }
#endif
		PROF(4, Snapshot(&snap));
		PROF(6, RenderPrepare(&snap));                   // camera tables: the CPU is busy while the blitter finishes the previous frame
		SunDraw(SunPx(snap.headLo | snap.headHi << 8), ((short)(snap.pitchLo | snap.pitchHi << 8)) >> 7);   // the sun at its azimuth (pitch (8.8): the far sun moves a quarter of the terrain horizon: nose up = the sun goes down)
		PROF(3, WaitBlt());
		RenderPlot();                                    // ... RenderPlot waits for it through fBeforePlot, then plots
		QueueFinish(f, TryFreeFrame(f));                 // drip, cockpit, sky, then the clear of the next frame; the picture is shown by the next vertical blank
		if (!running) {                                  // the first flight picture: show it all yellow, then the flight starts and the colours fade in
			while (!readyF && !(shownF == f)) ;           // finished by the blitter ...
			while (readyF) ;                              // ... and taken by the next vertical blank (SetPlanes)
			FadeSet(0);
			custom->cop1lc = (ULONG)copper;
			custom->copjmp1 = 0x7fff;
			gFadeStep = 0; gFadeWait = 1;
			running = 1;
		}
		else if (gFadeWait && ++gFadeWait > FADE_PICS) { gFadeT0 = frameCounter; gFadeWait = 0; gFade = 1; }      // a few pictures are on the screen: the fade (in the vertical blank) starts
#ifdef PROFILE
		prof.frames += (unsigned short)(frameCounter - f0); prof.n++;
#endif
	}

	WaitBlt();
#ifndef NO_MUSIC
	p61End();
#endif
	FreeSystem();
	FreeMem(copper, 2048);
	FreeMem(sunPlanes, 2 * SUN_SET);
	FreeMem(descMem, 6 * DESC_PLANE);
	FreeMem(descCop, 256);
	FreeMem(logoCop, 256);
#if SHOW_FPS
	FreeMem(topBar0, 2 * TOP_LINES * FSTRIDE);
#endif
	FreeMem(mem, 3 * FRAME_BYTES);
	SysClose();
	return 0;
}
