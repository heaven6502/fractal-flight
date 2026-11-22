/* flight_st_z.c (the depth band variant of flight_st.c: the ridge lines of the 5 cell rows go into 5 planes, FinishFrameZ fills every row band with its own ordered dither of two ground tones) - the fractal flight of Behind Jaggi Lines! / Rescue on Fractalus on the Atari ST (1040ST: 68000 @ 8 MHz, 1 MB, no blitter).
   flight_core.c is the verified flight model and terrain renderer of the Amiga port (FAR geometry: 160 x 80 pixels).  Ridge pixels are plotted
   into a small work plane (one bit-plane of 40 bytes per picture row, every pixel 2 bits wide), then FinishFrame (st_finish.s, CPU) drips the
   ridge pixels downwards into the ground, doubles every row to two screen lines and writes the interleaved low resolution screen.
   Colour index = 4 + (plane 1, plane 0):  sky, ground, ridge line.  Index 0 = border and cockpit bar.
   Joystick (IKBD port 1) or cursor keys: left / right = bank, up = dive, down = climb.  Autopilot until the first stick movement.  Esc / Space quit.
   The flight tick runs every second vertical blank (25 Hz) like the game; the picture rate is whatever the CPU manages. */
#include <stddef.h>
#include <string.h>
#include "flight_core.h"
#include "st_sys.h"
#include "st_scroll.h"
#include "st_logo.h"
#include "st_mod.h"

extern const u8 ymTune[], sndhTune[];
#ifndef SNDH_SUBTUNE
#define SNDH_SUBTUNE 0                     /* 0 = the tune's default sub tune */
#endif

#ifndef SHOW_FPS
#define SHOW_FPS 1                         /* the picture rate (x 10) in the top bar; make FPS=0 leaves it out */
#endif
#define SCR_BYTES  32000
#define PIC_LINES  150
#define PIC_LINE0  25                      /* first of the 150 picture lines (rows 1 .. 75, two lines each) */
#define WORK_BYTES (5 * FSTRIDE * F_ROWS)  /* 5 planes of 40 x 80 bytes: one per depth class */

static u8 scrMem[2 * SCR_BYTES + 256] __attribute__((aligned(4)));
static u8 *screens[2];
static u8 work[WORK_BYTES + 16] __attribute__((aligned(4)));

extern void ClearWorkZ(u8 *w);
extern void FinishFrameZ(const u8 *w, u8 *s, const u32 *masks);
static u32 zMasks[4][3];                       /* the 25 %, 50 %, 75 % Bayer patterns of the 4 screen line types (line & 3), the same 16 bit word in both halves */

static void InitMasks(void) {                  /* ordered dither at single screen pixel resolution, like the background */
	static const u8 bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
	int l, d, i;
	for (l = 0; l < 4; l++)
		for (d = 1; d <= 3; d++) {
			u32 w = 0;
			for (i = 0; i < 16; i++) w = (w << 1) | (bayer[l][i & 3] < d * 4);      /* screen pixel i of a 16 pixel word */
			zMasks[l][d - 1] = (w << 16) | w;
		}
}

/* ST palette: 0RGB, 3 bits per channel.  index = 4 * background + (plane 1, plane 0).  The background image (src/sky_data.c, planes 2 and 3 of the picture lines, 4 colours) is the sky behind
   the picture: 0 dark space (also the screen border), 1 blue, 2 butterscotch, 3 pale (sun, rings, moons).  The two bars above and below the picture are plane 0 only (colour 1, grey),
   the ridge pixels and the ground do not depend on the background: (plane 1, plane 0) = 10 ground, 11 ridge line (the number in the bar is drawn in colour 3), 00 sky = the background. */
static u16 palette[16] = {
	0x0001, 0x0421, 0x0654, 0x0100,       /* b0: dark space | far tone (and the bars): Mars brown | near ground tone: light | ridge line */
	0x0235, 0x0421, 0x0654, 0x0100,       /* b1: blue */
	0x0653, 0x0421, 0x0654, 0x0100,       /* b2: butterscotch */
	0x0776, 0x0421, 0x0654, 0x0100        /* b3: pale */
};
/* the mountain colours (planes 0 and 1), close together in brightness (0.3 R + 0.59 G + 0.11 B, of 7): line 2.2, far tone 3.2, near tone 4.2 (before: 0.3, 2.5, 5.2) */
#define LINE_TONE 0x0321                       /* the cell border lines */
#define FAR_TONE  0x0432                       /* far ground (colour class 01, also the bars) */
#define NEAR_TONE 0x0543                       /* near ground (colour class 10) */
extern const u16 skyBg[150][20][2];
extern const u16 skyPal[4];
extern const u16 skyGround;                   /* from sky_data.c: the mountain colour that stands out from the background */                   /* from sky_data.c: the 4 colours of the background image (0 = darkest) */

/* ---- flight state, advanced in the vertical blank ---- */
static TFlight flight;
static volatile u16 tickStart, tickEnd;
static volatile int autopilot = 1;

#define DRUMS_PLAY 380                                           /* PLAY calls of the tune before the picture is shown: the bass drum of Pajaro (hardware envelope, YM register 13 written once) is at call 390 = 7.8 s; the later digi drums are at call 721 */
/* flight ticks per 5 vertical blanks: 3 = 30 ticks per second at 50 Hz = the pace of the game on a 60 Hz (NTSC) machine; 2.5 (every second frame) would be the PAL pace of 25 ticks per second.
   Speed, turning, wind and the autopilot's timing all scale with it. */
#ifndef TICKS_PER_5
#define TICKS_PER_5 3
#endif
static u8 tickAcc;

static volatile u8 running;                                      /* 0 until the music runs and the first picture is shown: black screen, the flight stands still */
static const u16 blackPal[16];

static void VblHook(void) {
	MusicVbl();                                                 /* the MOD mixer of an STE: first thing in the vertical blank */
	if (!running) return;
	ScrollTick(StShown(), SCROLL_Y, 1);                         /* the scroller below the picture (first: the bar lines are drawn long before the beam gets there) */
	int porta = 0x0F;
	u8 j = StInput();
	if (j & ST_UP) porta &= ~F_UP;
	if (j & ST_DOWN) porta &= ~F_DOWN;
	if (j & ST_LEFT) porta &= ~F_LEFT;
	if (j & ST_RIGHT) porta &= ~F_RIGHT;
	if (porta != 0x0F) autopilot = 0;
	if (autopilot) porta = FAutopilot(&flight);          /* the demo flight of the Atari 800 port */
	tickAcc += TICKS_PER_5;
	if (tickAcc >= 5) {                                /* the game runs the flight tick every second frame (25 per second); here 30 per second, see TICKS_PER_5 */
		tickAcc -= 5;
		tickStart++;
		FlightTick(&flight, porta);
		GroundCollision(&flight);
		FWind(&flight);                                    /* wind from the side: the picture rolls and flutters a little */
		tickEnd++;
	}
}

static void Snapshot(TFlight *dst) {                   /* a consistent copy of the flight state (the interrupt may tick meanwhile) */
	u16 t;
	do {
		t = tickEnd;
		memcpy(dst, (const void *)&flight, sizeof(TFlight));
	} while (t != tickStart || t != tickEnd);
}

static void InitScreen(u8 *s) {
	int l, g;
	u16 *p = (u16 *)s;
	for (l = 0; l < 200; l++) {
		int pic = l >= PIC_LINE0 && l < PIC_LINE0 + PIC_LINES;
		for (g = 0; g < 20; g++) {
			*p++ = pic ? 0 : 0xFFFF;                                            /* plane 0: the bars are colour 1 */
			*p++ = 0;                                                           /* plane 1 */
			*p++ = pic ? skyBg[l - PIC_LINE0][g][0] : 0;                        /* planes 2 and 3: the background image */
			*p++ = pic ? skyBg[l - PIC_LINE0][g][1] : 0;
		}
	}
}

int main(void) {
	TFlight snap;
	u16 lastVbl = 0, renders = 0;
	int fps10 = 0, cur = 1;
	(void)lastVbl; (void)renders; (void)fps10;                                /* cur: the screen drawn next; the other one is on display */

	screens[0] = StAlign256(scrMem);
	screens[1] = screens[0] + SCR_BYTES;
	InitScreen(screens[0]); InitScreen(screens[1]);
#ifndef F_BENCH
	LogoDraw(screens[0]);                                       /* the intro picture on the first screen shown (black palette, then faded in) */
#endif

	InitMasks();
	ScrollInit(0);                                              /* builds the strip of the scroller text from the font (gfx/Antic.chr) */
	FInit();
	FSeed(20261106);
	InitGridMap();
	StartFlight(&flight);
	flight.speedCfg = FLIGHT_THRUST;
	fPlane = work;
	fBeforePlot = 0;

	#ifdef Z_DEBUG
	*(volatile u32 *)0x10000 = (u32)work; *(volatile u32 *)0x10004 = (u32)screens[0]; *(volatile u32 *)0x10008 = (u32)screens[1];
	#endif
	stVblHook = VblHook;
	{ int b; for (b = 0; b < 4; b++) { palette[b * 4] = skyPal[b]; palette[b * 4 + 1] = FAR_TONE; palette[b * 4 + 2] = NEAR_TONE; palette[b * 4 + 3] = LINE_TONE; } }          /* the sky colours of the background image; the far tone (colour class 01, also the bars) is the Mars brown, the near tone (class 10) the light tan */
#ifdef F_BENCH
	StInit(palette, screens[0]);
#else
	StInit(blackPal, screens[0]);                                  /* a black screen until the music runs */
#endif

#ifdef F_BENCH
	/* benchmark: F_BENCH frames of a fixed flight (the autopilot driven by the frame number), no music, no flip, no waiting for the vertical blank.  Pass 1 is timed: the number
	   of vertical blanks is drawn in the top bar.  Pass 2 flies the same frames again (the random numbers continue, so the pictures differ from pass 1 but are the same in every
	   build) and sums every picture: that checksum (mod 10000) is drawn in the bottom bar, so that two builds can be compared over all frames. */
	{
		int f, pass;
		u16 t0 = 0;
		u32 sum = 0;
		stVblHook = 0;
		for (pass = 0; pass < 2; pass++) {
			if (pass == 0) t0 = stFrame;
			for (f = 0; f < F_BENCH; f++) {
				int porta = 0x0F, phase = (f >> 4) & 3;
				signed char pitch = (signed char)flight.pitchHi;
				if (phase == 0) porta &= ~F_LEFT; else if (phase == 2) porta &= ~F_RIGHT;
				if (flight.m061D < 28 && pitch < 3) porta &= ~F_DOWN; else if (flight.m061D > 80 && pitch > -3) porta &= ~F_UP;
				FlightTick(&flight, porta); GroundCollision(&flight);
				RenderPrepare(&flight);
				ClearWorkZ(work);
				RenderPlot();
				FinishFrameZ(work, screens[0] + PIC_LINE0 * ST_LINE_BYTES, &zMasks[0][0]);
				if (pass == 1) {
					const u32 *p = (const u32 *)screens[0];
					int i;
					for (i = 0; i < 8000; i++) sum = (sum << 1 | sum >> 31) ^ p[i];
				}
			}
			if (pass == 0) fps10 = (int)(u16)(stFrame - t0);
		}
		StDrawNumberCol(screens[0], fps10, 8, 6, 3, 1);
		StDrawNumberCol(screens[0], (int)(sum % 10000), 8, 180, 3, 1);
		for (;;) ;
	}
#endif
	MusicStart(sndhTune, SNDH_SUBTUNE, ymTune);          /* the SNDH tune (music/tune.sndh), or the demo YM stream if that is not an SNDH */
#ifndef F_BENCH
	LogoIntro(MusicDrums() - 40);                                 /* fade in, hold, fade out (8 steps of 5 vertical blanks = 40) so that the screen is black at DRUMS_PLAY */
	InitScreen(screens[0]);                                     /* the flight's static planes again (bars, background) over the picture, while the palette is black */
#endif
	{                                                                      /* black until the drums kick in: the tune (Pajaro) has its bass drum at the PLAY call 390 (measured with the YM trace: the envelope shape register 13 is written once, the digi drums follow at call 721); the picture takes
	                                                                          about 10 PLAY calls to render, so it is shown at the call DRUMS_PLAY.  Fire skips the wait, the fallback after 30 seconds is for tunes that give no PLAY count. */
		u16 t0 = stFrame;
		while (!stQuit && (u16)(stFrame - t0) < 1500 && MusicPlays() < MusicDrums() && !(StInput() & ST_FIRE)) ;
	}

	while (!stQuit) {
		u8 *back;
		{ int k = StTakeNumKey(); if (k && !MusicIsMod()) StSndhStart(sndhTune, k); }   /* keys 1 .. 0 pick the SNDH sub tune */
		Snapshot(&snap);
		RenderPrepare(&snap);
		ClearWorkZ(work);
		RenderPlot();                                      /* into the work plane: the screens are not touched yet */
		StWaitPresented();                                 /* the previous picture is flipped in; the old screen is free one vertical blank later */
		back = screens[cur]; cur ^= 1;
		FinishFrameZ(work, back + PIC_LINE0 * ST_LINE_BYTES, &zMasks[0][0]);
#if SHOW_FPS
		renders++;
		if ((u16)(stFrame - lastVbl) >= 100) {             /* pictures per second x 10, drawn in the top bar */
#ifdef DEBUG_SNDH
			{ static u32 lp, lt; fps10 = (int)(StSndhPlays() - lp) * 100 + (int)(StSndhTicks() - lt) / 10; lp = StSndhPlays(); lt = StSndhTicks(); }
#else
			fps10 = renders * 5;
#endif
			renders = 0; lastVbl = stFrame;
		}
		StDrawNumberCol(back, fps10, 8, 6, 3, 1);
		if (MusicIsMod()) StDrawNumberCol(back, (int)(modArenaFree >> 10), 72, 6, 3, 1);       /* free KB of the MOD arena */
#endif
		StPresent(back);                                   /* shown from the next vertical blank on */
		if (!running) { StWaitPresented(); StSetPalette(palette); running = 1; }      /* the first picture is on display: show it, start the flight and the scroller */
	}

	MusicStop();
	StExit();
	return 0;
}
