/* flight_st.c - the fractal flight of Behind Jaggi Lines! / Rescue on Fractalus on the Atari ST (1040ST: 68000 @ 8 MHz, 1 MB, no blitter).
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
#include "st_descent.h"

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
#define WORK_BYTES (FSTRIDE * F_ROWS)      /* 40 x 80 */

static u8 scrMem[2 * SCR_BYTES + 256] __attribute__((aligned(4)));
static u8 *screens[2];
static u8 work[WORK_BYTES + 200 + 240 + 16] __attribute__((aligned(4)));
#ifdef UNDERWATER
#define WK (work + 200)                    /* four spare rows (zero) in front of the plane for the strip wave of st_finish_uw.s */
#else
#define WK work
#endif

extern void ClearWork(u8 *w);
extern void FinishFrame(const u8 *w, u8 *s);

/* ST palette: 0RGB, 3 bits per channel, no background image (the version with the image is flight_st_backdrop.c), the colours of that version.  Colour index = 4 * plane 2 + (plane 1, plane 0): the picture lines have plane 2 set
   (indices 4 .. 7: 4 sky, 6 ground, 7 ridge line), the bars above and below have plane 0 only (index 1, grey), the border and the number in the bar are indices 0 and 3. */
#ifdef UNDERWATER
/* the underwater version (flightuw.prg): the same picture in sea colours, a wobble of the vertices (st_vtab.s, UW) and a strip wave (st_finish_uw.s) */
static u16 palette[16] = {
	0x0013, 0x0123, 0x0012, 0x0668,       /* 0 border: deep blue | 1 bar | 2 | 3 the number in the bar */
	0x0001, 0x0233, 0x0000, 0x0012,       /* 4 .. 7: group 1 = outside the light, very dark: the water | the particles | the mountains | the ridge line.  Only the light cones show the full colours */
	0x0132, 0x0364, 0x0153, 0x0364,       /* 8 .. 11: group 2 = the edge of the light (dark green): the water a little brighter, the mountains much brighter than the water */
	0x0142, 0x0565, 0x0364, 0x0565        /* 12 .. 15: group 3 = the core of the light: the mountains bright, the ridge line brighter still */
};
/* the light shafts of the submarine: planes 2 and 3 are static and the CPU never touches them again.  They hold a mask (built once by BuildLightMask: two cones that widen away from the two lamps below the
   picture), a pixel is in group 1 (plane 2 / 3 = 1 0: outside the light), 2 (0 1: the edge of a shaft) or 3 (1 1: its core).  The picture itself only writes planes 0 and 1 (sky 00, ground 10, ridge 11), so the
   colour index is 4 * group + that, and the palette makes every group a brighter version of the same picture: the water, the sea floor and the ridge lines are lit where a shaft passes.  The vertical blank hook
   makes the lit colours of group 2 and 3 flicker (four palette registers every second vertical blank). */
#define FL_SLOW 2                                      /* vertical blanks between two flicker steps (25 per second) */
/* six brightness levels of the lit colours: the water (8, 12) and the mountains (10, 14) of group 2 (the edge) and group 3 (the core) */
static const u16 flW2[6] = { 0x0022, 0x0032, 0x0032, 0x0132, 0x0133, 0x0143 };
static const u16 flF2[6] = { 0x0132, 0x0133, 0x0143, 0x0153, 0x0254, 0x0264 };
static const u16 flW3[6] = { 0x0122, 0x0132, 0x0132, 0x0142, 0x0143, 0x0243 };
static const u16 flF3[6] = { 0x0143, 0x0154, 0x0264, 0x0364, 0x0365, 0x0465 };
static const u8 flMap[8] = { 0, 1, 2, 3, 3, 4, 5, 5 };
static u16 flRnd = 0xACE1;                             /* a 16 bit shift register for the random flicker */
static u8 flH = 3, flC = 3, flT;
static u16 lightMask[150][20][2];                      /* planes 2 and 3 of the 150 picture lines */
/* The light of the submarine: two lamps at the bottom of the picture, each throwing a soft beam ahead into the depth, volumetric like light in water: no edge, no cap.  Across a beam the brightness is
   a bell curve (gaussT: it fades out smoothly, 1.5 radii wide).  Like a flashlight pointing forward the beams get wider with the distance: a point at the lamp,
   wide at the far end, and they fade out.  There is a glare around each lamp.  The brightness (0 .. 255) becomes one of the three groups with an 8 x 8 ordered dither (bayer8): about 190
   apparent tones from three palette groups, so the beams have no visible edge.  Pixel coordinates: x = 0 .. 319, y = picture line * 1.2 (a line is 1.2 pixels high); the left beam starts at
   (LAMP_X, LAMP_Y) along an axis 6 degrees outwards, the right one is its mirror image (x -> 319 - x), the two add where they overlap. */
static const u8 gaussT[48] = { 255, 255, 253, 251, 247, 243, 238, 232, 225, 218, 210, 201, 192, 183, 174, 164, 155, 145, 135, 126, 117, 108, 99, 91, 83, 75, 68, 61, 55, 49, 44, 39, 35, 30, 27, 23, 20, 18, 15, 13, 11, 10, 8, 7, 6, 5, 4, 3 };      /* 255 * exp(-2 * (u / R)^2), u = 0 .. 1.5 R in 48 steps */
static const u8 bayer8[64] = { 2, 130, 34, 162, 10, 138, 42, 170, 194, 66, 226, 98, 202, 74, 234, 106, 50, 178, 18, 146, 58, 186, 26, 154, 242, 114, 210, 82, 250, 122, 218, 90, 14, 142, 46, 174, 6, 134, 38, 166, 206, 78, 238, 110, 198, 70, 230, 102, 62, 190, 30, 158, 54, 182, 22, 150, 254, 126, 222, 94, 246, 118, 214, 86 };      /* the 8 x 8 Bayer matrix * 4 + 2 */
#define LAMP_X 146                                     /* the left lamp (the right one is at 173) */
#define LAMP_Y 176                                     /* a little above the bottom of the picture (180) */
#define BEAM_L 95                                      /* the length of the beam on the screen: it ends near the middle of the picture (y = 176 - 95 = 81 of 180) */
#define BEAM_R0 3                                      /* the radius of the beam at the lamp (a point) ... */
#define BEAM_R1 80                                     /* ... and at the far end: like a flashlight, the beam gets wider with the distance */
#define AXIS_X (-44)                                   /* the unit vector of the axis * 256: up (-y) and 10 degrees outwards (-x) */
#define AXIS_Y (-252)
#define FADE_M 485                                     /* 180 * 256 / BEAM_L: the light fades to 75 / 255 at the end, which is then rounded off over the last BEAM_TAPER pixels */
#define BEAM_TAPER 14
#define GLARE_R 13                                     /* the glare around a lamp */
static short recT[96];                                 /* (32 << 11) / radius */
static void BuildLightMask(void) {
	int l, x, g, b, r;
	for (r = 1; r < 96; r++) recT[r] = (short)((32 << 11) / r);
	for (l = 0; l < 150; l++) {
		u16 acc[320];
		int dy = l * 6 / 5 - LAMP_Y, dx0 = 40 - LAMP_X;                          /* the loop starts at x = 40 */
		int tfp = dx0 * AXIS_X + dy * AXIS_Y, ufp = -dx0 * AXIS_Y + dy * AXIS_X;      /* along and across the axis, * 256 (they change by +AXIS_X and -AXIS_Y per pixel) */
		for (x = 0; x < 320; x++) acc[x] = 0;
		for (x = 40; x <= 200; x++, tfp += AXIS_X, ufp -= AXIS_Y) {
			int t = tfp >> 8, u = ufp >> 8, v = 0, au = u < 0 ? -u : u;
			if (t >= 0 && t < BEAM_L) {                                                                /* a: 255 at the lamp, 0 at the far end; the radius grows with the distance (the frustum of a flashlight), the light fades */
				int a = 255 - ((t * FADE_M) >> 8), R = BEAM_R0 + (((BEAM_R1 - BEAM_R0) * t) / BEAM_L), idx = ((short)au * recT[R]) >> 11;
				if (idx < 48) v = (gaussT[idx] * ((a * (a + 255)) >> 9)) >> 8;                                /* bell curve x the fade (between linear and square) */
				if (t > BEAM_L - BEAM_TAPER) v = (v * (BEAM_L - t) * 18) >> 8;                                /* the end of the beam is rounded off */
			}
			{
				int dx = x - LAMP_X, d2 = dx * dx + dy * dy;                                          /* the glare of the lamp */
				if (d2 < GLARE_R * GLARE_R) v += 255 - ((d2 * 255) / (GLARE_R * GLARE_R));
			}
			if (v) { acc[x] = (u16)(acc[x] + v); acc[319 - x] = (u16)(acc[319 - x] + v); }      /* the right beam is the mirror image */
		}
		for (g = 0; g < 20; g++) {
			u16 w2 = 0, w3 = 0;
			for (b = 0; b < 16; b++) {
				int xx = g * 16 + b, v = acc[xx] > 255 ? 255 : acc[xx];
				int lv = 1 + ((v * 2 + bayer8[((l & 7) << 3) | (xx & 7)]) >> 8);          /* 1 = outside the light, 3 = the core, in between dithered */
				if (lv > 3) lv = 3;
				w2 = (u16)(w2 << 1 | (lv & 1)); w3 = (u16)(w3 << 1 | (lv >> 1));
			}
			lightMask[l][g][0] = w2; lightMask[l][g][1] = w3;
		}
	}
}
short fColDisp[256], fColOffW[256];                     /* the water ripple of every pixel column (rows * 40 bytes) and the column byte offset with it added (the line routine reads it, st_vtab.s / flight_line.asm UW) */
extern u8 fColOff[256];
short fStripOff[10];                                   /* row offset (bytes) of each strip of 32 pixels */
int fWobPh;                                            /* phase of the vertex wobble */
const short fWobTab[64] = { 0, 39, 78, 116, 153, 189, 222, 254, 283, 309, 333, 353, 370, 383, 392, 398, 400, 398, 392, 383, 370, 353, 333, 309, 283, 254, 222, 189, 153, 116, 78, 39, 0, -39, -78, -116, -153, -189, -222, -254, -283, -309, -333, -353, -370, -383, -392, -398, -400, -398, -392, -383, -370, -353, -333, -309, -283, -254, -222, -189, -153, -116, -78, -39 };   /* 400 / 256 rows * sin */
static const signed char rippleT[64] = { 0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 1, 1, 0, 0, 0, -1, -1, -2, -2, -2, -3, -3, -3, -3, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -3, -3, -3, -3, -2, -2, -2, -1, -1, 0 };    /* +-4 rows: a column differs from the next by at most 1 row, so the ridge lines stay connected */
static const signed char stripE[64] = { 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, -1, -1, -1, 0, 0 };
#else
static u16 palette[16] = {
	0x0642, 0x0532, 0x0100, 0x0100,       /* 0 border: dark butterscotch (the Mars sky 0x0753 in the shade) | 1 the panel (scroller bar): dark rust | 2 | 3 the number in the bar */
	0x0753, 0x0765, 0x0432, 0x0100,       /* 4 sky: the butterscotch of the Mars sky (the green was 0x0122; a violet one: 0x0425) | 5 the halo of the sun | 6 ground (the brown of the backdrop version) | 7 ridge line */
	0x0753, 0x0773, 0x0432, 0x0100,       /* 8 .. 11: the group of the body of the sun: sky | the body of the sun (yellow) | ground | ridge line (the same colours as in the sky group: the mountains in front of the sun look as everywhere) */
	0x0753, 0x0777, 0x0432, 0x0100        /* 12 .. 15: the group of the core of the sun: sky | the core (white) | ground | ridge line */
};
#ifndef F_BENCH
/* The sun, four colours without dither: the sky and three solid rings, in the top right corner of the picture.  Planes 2 and 3 pick the palette group of a pixel (static): outside the sun group 1, the body group 2
   (plane 3 only), the core group 3 (both).  The picture bits (planes 0 and 1) of a sky pixel are 00; the pixels of the sun are drawn with the bits 01 after the picture (SunDraw), only where the picture has neither
   ground nor ridge line, so the mountains are in front of the sun: the colour 4 g + 1 is the halo (g = 1), the body (2) or the core (3).  A circle of 14 pixels (a line is 1.2 pixels high) at (304, line 17):
   it fits the last two groups of 16 pixels, columns 288 .. 319. */
#define SUN_PH 8                                       /* phases of the heat shimmer (the table is 13 KB: the program with the MOD buffer is close to the memory of a 1 MB ST) */
#define SUN_CY 17                                      /* the scanline of the centre of the sun */
#define SUN_T 6                                        /* the first and the last scanline of the circle (radius 14 pixels = 11.7 lines around SUN_CY) */
#define SUN_B 28
#define SUN_N (SUN_B - SUN_T + 1)
static const signed char sunS1[64] = { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0 };      /* 1.2 * sin and 0.8 * sin: the two sines that pick the scanline */
static const signed char sunS2[64] = { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0 };
static const short sunTan[72] = { 0, 2, 5, 7, 9, 12, 14, 17, 19, 21, 24, 26, 28, 31, 33, 36, 38, 41, 43, 46, 48, 51, 53, 56, 58, 61, 63, 66, 69, 71, 74, 77, 80, 82, 85, 88, 91, 94, 97, 100, 103, 106, 109, 112, 115, 118, 122, 125, 128, 132, 135, 139, 142, 146, 150, 154, 158, 162, 166, 170, 174, 178, 183, 187, 192, 197, 202, 207, 212, 217, 223, 228 };      /* 192 * tan(angle): the x offset in pixels of a far object at an angle of i * 32 / 16384 turns from the view direction (96 columns = 192 pixels per unit of tan) */
/* The sun, a far object: it stays at its azimuth in the world, so on the screen it moves sideways when the plane turns (x = 160 + 192 * tan(0.25 * (azimuth - heading)): the terrain's pan, slowed down because the
   sun is millions of km away) and leaves the picture when the turn is far enough.  Up and down: the pitch moves it a quarter of the terrain horizon (SunDraw dy).  The pixels of the sun at any x are prepared once: sunW[phase][shift of 0 .. 15 pixels][scanline][3 groups
   of 16 pixels x (all the sun, body + core, core)], so a picture only copies up to 3 words per scanline and plane.  The scanlines of the sun are picked by two sines (the heat shimmer, +-1 line and +-1 line at the peaks of the second one,
   wave lengths of 16 and 7 lines, 8 phases, a new one every 8 vertical blanks); a line outside the sun, or one that picks a line outside it, stays empty: the top and the bottom are never doubled. */
static u16 sunW[SUN_PH][16][SUN_N][9];                  /* 16 phases x 16 shifts x 23 scanlines x 9 words = 106 KB */
static void BuildSun(void) {
	static int dxx[40], ddy[SUN_N];
	int ph, sh, l, j, b, d;
	for (d = 0; d < 40; d++) dxx[d] = d * d;
	for (l = 0; l < SUN_N; l++) { int dy = SUN_T + l - SUN_CY; ddy[l] = (dy * dy * 36) / 25; }       /* (a line is 1.2 pixels high) */
	for (ph = 0; ph < SUN_PH; ph++)
		for (l = 0; l < SUN_N; l++) {                  /* the window of 48 pixels with the centre of the sun at pixel 16 */
			int L = SUN_T + l, src = L + sunS1[(L * 4 + ph * 4) & 63] + sunS2[(L * 9 - ph * 8) & 63], in = src >= SUN_T && src <= SUN_B, r2;
			for (j = 0; j < 3; j++) {
				u16 a = 0, bd = 0, c = 0;
				for (b = 0; b < 16; b++) {
					d = j * 16 + b - 16; if (d < 0) d = -d;
					r2 = in ? dxx[d] + ddy[src - SUN_T] : 9999;
					a = (u16)(a << 1 | (r2 <= 196)); bd = (u16)(bd << 1 | (r2 <= 100)); c = (u16)(c << 1 | (r2 <= 36));
				}
				sunW[ph][0][l][j] = a; sunW[ph][0][l][3 + j] = bd; sunW[ph][0][l][6 + j] = c;
			}
			for (sh = 1; sh < 16; sh++)                /* the pre shifted copies: the window shifted right by sh pixels, so that the sun can be put at any pixel */
				for (b = 0; b < 3; b++) {              /* b: all / body + core / core */
					const u16 *w = &sunW[ph][0][l][3 * b];
					u16 *o = &sunW[ph][sh][l][3 * b];
					o[0] = (u16)(w[0] >> sh);
					o[1] = (u16)(((u32)w[0] << (16 - sh) | w[1] >> sh) & 0xFFFF);
					o[2] = (u16)(((u32)w[1] << (16 - sh) | w[2] >> sh) & 0xFFFF);
				}
		}
}
#define SUN_AZ0 2239                                   /* the start azimuth left of the view direction: 1679 units (37 degrees = x 16, the top left corner) / the factor 0.75 */
static int sunAz, sunAzSet;                            /* the azimuth of the sun in heading units (a full turn = $4000), 6716 units to the left of the view direction at the start: the angle is scaled by 0.25 below (the sun is millions of km away: it pans slower than the terrain), so 6716 * 0.25 = 1679 units = 37 degrees = x 16, the top left corner */
static int SunPx(int head) {                           /* the x of the centre of the sun on the screen for this heading (a far object: 25 % of the pan of the terrain), 9999 = not on the screen */
	int ang, a, i, f, o;
	if (!sunAzSet) { sunAz = head - SUN_AZ0; sunAzSet = 1; }
	ang = ((short)((sunAz - head) << 2)) >> 2;         /* the angle from the view direction, 14 bit signed */
	ang -= ang >> 2;                                   /* * 0.75: a sun at infinity would turn exactly with the view (1.0); slightly slower looks better (the start azimuth SUN_AZ0 is 1679 / 0.75) */
	a = ang < 0 ? -ang : ang;
	i = a >> 5; f = a & 31;
	if (i >= 70) return 9999;
	o = sunTan[i] + (((sunTan[i + 1] - sunTan[i]) * f) >> 5);
	return 160 + (ang < 0 ? -o : o);
}
static short sunPrevG[2] = { -10, -10 };               /* the first group the sun covered in each of the two screens (to clear its planes 2 and 3 there) */
static short sunPrevDy[2];                             /* the vertical shift (scanlines) of the sun in each of the two screens */
static void SunDraw(u8 *scr, int ph, int px, int dy, int slot) {     /* scr = the first picture line of the finished picture */
	int l, j, g0 = 0, sh = 0, vis = px < 2000, pg = sunPrevG[slot], pdy = sunPrevDy[slot];
	ph &= SUN_PH - 1;
	if (vis) { int xq = px - 16; g0 = xq >> 4; sh = xq & 15; }     /* the left edge of the 32 pixel extent of the sun: the group and the pixel shift (a pre shifted copy of the sun for every pixel) */
	for (l = 0; l < SUN_N; l++) {                      /* first all of the old sun away (the old and the new lines overlap when the sun moves by a few lines: one pass would clear what was just drawn) */
		int ln = SUN_T + l + pdy;
		u8 *base = scr + ln * ST_LINE_BYTES;
		if (ln >= 0 && ln < PIC_LINES)
			for (j = 0; j < 3; j++) { int g = pg + j; if (g >= 0 && g < 20) { u16 *w = (u16 *)(base + g * 8); w[2] = 0xFFFF; w[3] = 0; } }      /* where the sun was: group 1 again */
	}
	if (vis)
		for (l = 0; l < SUN_N; l++) {
			int ln = SUN_T + l + dy;
			u8 *base = scr + ln * ST_LINE_BYTES;
			const u16 *t = sunW[ph][sh][l];
			if (ln >= 0 && ln < PIC_LINES)
				for (j = 0; j < 3; j++) {
					int g = g0 + j;
					if (g >= 0 && g < 20) {
						u16 *w = (u16 *)(base + g * 8);
						w[0] |= (u16)(t[j] & ~(w[0] | w[1]));                        /* plane 0 = 1 where the picture is sky: the bits 01 */
						w[2] = (u16)(~t[3 + j] | t[6 + j]);                          /* plane 2 / 3: the group: 2 = body (plane 3), 3 = core (both), 1 = the rest */
						w[3] = t[3 + j];
					}
				}
		}
	sunPrevG[slot] = (short)(vis ? g0 : -10);
	sunPrevDy[slot] = (short)dy;
}
#endif
#endif

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

#if !defined(UNDERWATER) && !defined(F_BENCH)
/* the entry into the atmosphere (measured in the Atari original): when the flight starts the whole window is the yellow of the dust (228, 158, 62) and every colour fades from that yellow into its
   final one within about 40 vertical blanks (sky orange, ground brown ...).  Here: the picture colours 4 .. 15 of the palette start all yellow and cross fade to the flight palette in 8 steps
   of 5 vertical blanks, in the vertical blank. */
#ifndef FADE_ON
#define FADE_ON 1                                      /* 0 = the first palette (all yellow) stays, to check it; 1 = the cross fade to the flight palette (make CFLAGS=-DFADE_ON=1) */
#endif
#define FADE_YELLOW 0x0753                             /* the light orange of the end of the descent (descPalette 7): no jump when the flight starts */
#define FADE_STEPS  16                                 /* 4 bit colours (STE, Amiga): 16 steps of 2.5 vertical blanks; the plain ST has 3 bits and shows every second step */
#ifndef FADE_MAX
#define FADE_MAX    16                                 /* the fade stops after this many steps (8 = all the way to the flight palette): 1 to look at the first step */
#endif
#define FADE_VBLS   5
#ifndef FADE_PICS
#define FADE_PICS   8                                  /* the fade starts when this many flight pictures have been rendered after the first one */
#endif
static u16 gFadeT0; static u8 gFade, gFadeStep, gFadeWait;
static u8 gFadeSte;
static void FadeSet(int k) {                           /* the colours 4 .. 15: start + (final - start) * k / 16, every channel in 4 bit levels (the palette holds 3 bit values: level = 2 * value) */
	int i, ch;
	for (i = 4; i < 16; i++) {
		int b = palette[i], r0 = ((FADE_YELLOW >> 8) & 7) * 2, g0 = ((FADE_YELLOW >> 4) & 7) * 2, b0 = (FADE_YELLOW & 7) * 2, v = 0;
		for (ch = 0; ch < 3; ch++) {
			int a4 = ch == 0 ? r0 : ch == 1 ? g0 : b0, f4 = ((b >> (8 - 4 * ch)) & 7) * 2, c = a4 + (((f4 - a4) * k) >> 4);
			int nib = gFadeSte ? (((c & 1) << 3) | (c >> 1)) : (c >> 1);              /* STE: bit 3 of the nibble is the least significant bit of the level; ST: 3 bits */
			v |= nib << (8 - 4 * ch);
		}
		*(volatile u16 *)(0xFF8240L + 2 * i) = (u16)v;
	}
}
#endif
static volatile u8 running;                                      /* 0 until the music runs and the first picture is shown: black screen, the flight stands still */
static const u16 blackPal[16];

static void VblHook(void) {
	MusicVbl();                                                 /* the MOD mixer of an STE: first thing in the vertical blank */
	if (!running) return;
#if !defined(UNDERWATER) && !defined(F_BENCH)
	if (gFade) {
		u16 n = (u16)(stFrame - gFadeT0);
		int k = (n * 13) >> 5;                                  /* n / 2.46: 16 steps in 40 vertical blanks */
		if (k > FADE_MAX) k = FADE_MAX;
		if (k != gFadeStep) { gFadeStep = (u8)k; FadeSet(k); if (k == FADE_MAX) gFade = 0; }
	}
#endif
#ifdef UNDERWATER
	if (++flT >= FL_SLOW) {                                   /* the lights flicker: a random brightness level for the edge and the core, smoothed with the last one */
		flT = 0;
		flRnd = (u16)((flRnd >> 1) ^ (-(flRnd & 1) & 0xB400));
		flH = (u8)((flH + flMap[flRnd & 7] + 1) >> 1);
		flC = (u8)((flC + flMap[(flRnd >> 4) & 7] + 1) >> 1);
		*(volatile u16 *)0xFF8250 = flW2[flH];                /* colours 8, 10: the edge of the light */
		*(volatile u16 *)0xFF8254 = flF2[flH];
		*(volatile u16 *)0xFF8258 = flW3[flC];                /* colours 12, 14: the core */
		*(volatile u16 *)0xFF825C = flF3[flC];
	}
#endif
	ScrollTick(StShown(), SCROLL_Y, 1);                         /* the scroller below the picture (first: the bar lines are drawn long before the beam gets there) */
#if !defined(UNDERWATER) && !defined(F_BENCH)
#endif
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

#if !defined(UNDERWATER) && !defined(F_BENCH)
static int sunPx = 9999, sunDy;
#endif
static void InitScreen(u8 *s) {
	int l, g;
	u16 *p = (u16 *)s;
	for (l = 0; l < 200; l++) {
		int pic = l >= PIC_LINE0 && l < PIC_LINE0 + PIC_LINES;
		for (g = 0; g < 20; g++) {
			*p++ = pic ? 0 : 0xFFFF;                                            /* plane 0: the bars are colour 1 */
			*p++ = 0;                                                           /* plane 1 */
#ifdef UNDERWATER
			*p++ = pic ? lightMask[l - PIC_LINE0][g][0] : 0;                        /* plane 2, 3: the light mask */
			*p++ = pic ? lightMask[l - PIC_LINE0][g][1] : 0;
#else
			*p++ = pic ? 0xFFFF : 0;                                            /* plane 2: the picture lines use the colours 4 .. 7 */
			*p++ = 0;                                                           /* plane 3 */
#endif
		}
	}
}

#ifdef UNDERWATER
static void UwFrame(int t) {                           /* the per frame work of the underwater effects: the ripple table of the columns and the phase of the vertex wobble */
	int i;
	for (i = F_COL_LO; i < F_COL_HI; i++) { short d = (short)(rippleT[(i * 2 + (t >> 2)) & 63] * 40); fColDisp[i] = d; fColOffW[i] = (short)(fColOff[i] + d); }   /* the water ripple: a sine of the column (wave length 32 columns), a cycle every 5 s */
	fWobPh = t >> 1;
}
static void ClearPlane(void) {                         /* the work plane with the rows around it that the ripple of the plot reaches (rows -4 .. 83) */
	u32 *q = (u32 *)(WK + 3040); int i;
	ClearWork(WK - 160);
	for (i = 0; i < 80; i++) q[i] = 0;
}
#else
#define ClearPlane() ClearWork(work)
#endif
#ifdef UNDERWATER

/* ---- the particles (marine snow): PT_N points in the camera space of the terrain (x right, y up, z ahead; the same units), one pixel each, drawn into plane 0 of the finished picture (planes 0 and 1 are
   rewritten completely for every picture, so nothing has to be erased).  They are carried by the flight like the terrain, but 1.25 times faster (PtMove): the camera moves by what the position of the flight
   changed since the last picture (so the speed does not depend on the picture rate), the turn of the heading rotates the cloud and the change of the altitude lifts it.  The picture bits 01 are used by
   nothing else, so a particle has the palette colours 5, 9, 13: dim in the dark water, bright in the light cones.  The projection is the one of the terrain (96 * x / z, the horizon of the pitch and roll
   of the flight, fPitchBase / fRollS), at the resolution of the screen: 320 x 150 picture lines.  A particle that leaves the view is born again far away. ---- */
#define PT_N     16
#define PT_ZMAX  30000                                 /* the flight moves about 830 z units per picture (1250 for the particles): a particle crosses the water in about 25 pictures */
#define PT_ZMIN  600
#define PT_RISE  8                                    /* the particles rise by themselves, like bubbles: y units per vertical blank (50 per second), independent of the flight */
extern int fPitchBase, fRollS, fCamS, fCamC;
static short ptX[PT_N], ptY[PT_N], ptZ[PT_N];
static u16 ptRnd = 0x1D2B;
static int PtRand(void) { ptRnd ^= (u16)(ptRnd << 7); ptRnd ^= (u16)(ptRnd >> 9); ptRnd ^= (u16)(ptRnd << 8); return ptRnd; }      /* xorshift, 16 bit */
static void PtSpawn(int i, int z) { ptX[i] = (short)((PtRand() & 0x7FFF) - 16384); ptY[i] = (short)((PtRand() & 0x1FFF) - 4096); ptZ[i] = (short)z; }
static void PtInit(void) { int i; for (i = 0; i < PT_N; i++) PtSpawn(i, PT_ZMIN + ((PtRand() & 0x7FFF) % (PT_ZMAX - PT_ZMIN))); }
static short ptLastX, ptLastZ, ptLastH, ptLastA;
static u16 ptLastV;
static u8 ptHave;
static void PtMove(const TFlight *f) {                 /* the flight moved: carry the particles (after RenderPrepare: fCamS / fCamC are the sine and cosine of the heading, 16 times the game's units) */
	short px = (short)(f->posXLo | f->posXHi << 8), pz = (short)(f->posZLo | f->posZHi << 8);
	short hd = (short)(f->headLo | f->headHi << 8), al = (short)(f->altLo | f->altHi << 8);
	int dpx = (short)(px - ptLastX), dpz = (short)(pz - ptLastZ), dh = ((short)((hd - ptLastH) << 2)) >> 2, dal = (short)(al - ptLastA);
	short s = (short)fCamS, c = (short)fCamC;
	int gx, gz, gy, i;
	int dv = (u16)(stFrame - ptLastV);                                           /* vertical blanks since the last picture */
	ptLastX = px; ptLastZ = pz; ptLastH = hd; ptLastA = al; ptLastV = stFrame;
	if (!ptHave) { ptHave = 1; return; }
	gx = (dpz * s - dpx * c) >> 12;                                              /* a fixed point of the water moves by this in the camera space ... */
	gz = -((dpz * c + dpx * s) >> 12);
	gy = -(dal << 2);
	gz += gz >> 2;                                                               /* ... 1.25 times faster than the terrain */
	gx >>= 3; gy >>= 3;                                                          /* the sideways drift (wind) and the climb are 1/8: the particles are much nearer than the terrain, so the parallax of these movements would throw them across the picture */
	gy += dv * PT_RISE;                                                          /* and up by themselves */
	for (i = 0; i < PT_N; i++) {
		int x = ptX[i] + gx, y = ptY[i] + gy, z = ptZ[i] + gz;
		if (dh) { int p = (short)z * dh; x -= (p + (p << 1)) >> 13; p = (short)x * dh; z += (p + (p << 1)) >> 13; }       /* the turn: dx = -z * angle, dz = x * angle (a full turn = $4000) */
		ptX[i] = (short)x; ptY[i] = (short)y; ptZ[i] = (short)z;
	}
}
static inline int Div16(int num, int den) { asm("divs.w %2,%0" : "+d"(num) : "d"(den) : "cc"); return (short)num; }       /* a 32 bit by 16 bit division, the quotient must fit 16 bits */
static void PtDraw(u8 *scr) {                          /* scr = the first picture line of the finished picture */
	int i;
	for (i = 0; i < PT_N; i++) {
		int z = ptZ[i], x = ptX[i], y = ptY[i], ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, xs, ys, px, line;
		if (z < PT_ZMIN || z > PT_ZMAX + 3000 || (ax << 5) >= (z << 5) - (z << 2) - z || (ay << 5) >= (z << 4) + (z << 2)) { PtSpawn(i, PT_ZMAX - (PtRand() & 0xFFF)); continue; }      /* gone: born again far away (the 68000 has no 32 bit multiply: shifts and adds) */
		xs = 0x8000 + Div16((x << 14) + (x << 13), z);                           /* 96 * 256 * x / z = 8.8 columns, + 128 columns */
		ys = 0x8000 + Div16((y << 14) + (y << 13), z) + fPitchBase + ((((short)(xs - 0x8000)) * (short)fRollS) >> 12);      /* + the horizon of this column */
		px = (xs - (F_COL_LO << 8)) >> 7;                                        /* 2 pixels per column */
		line = ((F_YTOP * 256 - ys) >> 7) - 2;                                   /* 2 lines per row, row 1 = the first line */
		if (px < 0 || px >= 320 || line < 0 || line >= 150) continue;
		*(u16 *)(scr + (line << 7) + (line << 5) + ((px >> 4) << 3)) |= (u16)(0x8000 >> (px & 15));
	}
}
#endif

int main(void) {
	TFlight snap;
	u16 lastVbl = 0, renders = 0;
	int fps10 = 0, cur = 1;
	(void)lastVbl; (void)renders; (void)fps10;                                /* cur: the screen drawn next; the other one is on display */

	screens[0] = StAlign256(scrMem);
	screens[1] = screens[0] + SCR_BYTES;
#ifdef UNDERWATER
	BuildLightMask();
#elif !defined(F_BENCH)
	BuildSun();
#endif
	InitScreen(screens[0]); InitScreen(screens[1]);
#ifdef UNDERWATER
	PtInit();
#endif
#ifndef F_BENCH
	LogoDraw(screens[0]);                                       /* the intro picture on the first screen shown (black palette, then faded in) */
#endif

	ScrollInit(0);                                              /* builds the strip of the scroller text from the font (gfx/Antic.chr) */
	FInit();
	FSeed(20261106);
	InitGridMap();
	StartFlight(&flight);
	flight.speedCfg = FLIGHT_THRUST;
	fPlane = WK;
	fBeforePlot = 0;

	stVblHook = VblHook;
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
#ifdef UNDERWATER
				UwFrame(f * 3);
#endif
				RenderPrepare(&flight);
#ifdef UNDERWATER
				PtMove(&flight);
#endif
				ClearPlane();
				RenderPlot();
				FinishFrame(WK, screens[0] + PIC_LINE0 * ST_LINE_BYTES);
#ifdef UNDERWATER
				PtDraw(screens[0] + PIC_LINE0 * ST_LINE_BYTES);
#endif
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
#ifndef UNDERWATER
	{                                                           /* the descent from orbit: space with stars, then the limb of the planet rises (st_descent.c), the last picture is the colour of the flight's sky */
		int dcur = 1;
		u16 t0;
		DescentInit(screens[0], screens[1]);
		StSetPalette(descPalette);
		t0 = stFrame;
		while (!stQuit && (u16)(stFrame - t0) < DESC_HOLD - 1 && !(StInput() & ST_FIRE)) ;
		for (;;) {
			int t;
			u8 *back;
			StWaitPresented();
			t = (u16)(stFrame - t0) + 1;                       /* the picture is on display from the next vertical blank */
			if (stQuit || t >= DESC_END || (StInput() & ST_FIRE)) break;
			back = screens[dcur]; dcur ^= 1;
			DescentFrame(back, back == screens[1], t);
			StPresent(back);
		}
		DescentEnd(screens[0], screens[1]);
		cur = dcur;                                             /* the flight draws into the screen that is not on display */
	}
#endif
#endif
	{                                                                      /* black until the drums kick in: the tune (Pajaro) has its bass drum at the PLAY call 390 (measured with the YM trace: the envelope shape register 13 is written once, the digi drums follow at call 721); the picture takes
	                                                                          about 10 PLAY calls to render, so it is shown at the call DRUMS_PLAY.  Fire skips the wait, the fallback after 30 seconds is for tunes that give no PLAY count. */
		u16 t0 = stFrame;
		while (!stQuit && (u16)(stFrame - t0) < 1500 && MusicPlays() < MusicDrums() && !(StInput() & ST_FIRE)) ;
	}

	while (!stQuit) {
		u8 *back;
		{ int k = StTakeNumKey(); if (k && !MusicIsMod()) StSndhStart(sndhTune, k); }   /* keys 1 .. 0 pick the SNDH sub tune */
#ifdef UNDERWATER
		UwFrame(stFrame);
#endif
		Snapshot(&snap);
		RenderPrepare(&snap);
#ifdef UNDERWATER
		PtMove(&snap);
#elif !defined(F_BENCH)
		sunPx = SunPx(snap.headLo | snap.headHi << 8);
		sunDy = ((short)(snap.pitchLo | snap.pitchHi << 8)) >> 7;     /* pitch (8.8): the terrain horizon moves 8 lines per pitch unit, the far sun a quarter of it: nose up = the sun goes down */
#endif
		ClearPlane();
		RenderPlot();                                      /* into the work plane: the screens are not touched yet */
		StWaitPresented();                                 /* the previous picture is flipped in; the old screen is free one vertical blank later */
		back = screens[cur]; cur ^= 1;
		FinishFrame(WK, back + PIC_LINE0 * ST_LINE_BYTES);
#ifdef UNDERWATER
		PtDraw(back + PIC_LINE0 * ST_LINE_BYTES);
#elif !defined(F_BENCH)
		SunDraw(back + PIC_LINE0 * ST_LINE_BYTES, stFrame >> 3, sunPx, sunDy, back == screens[1]);
#endif
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
#if !defined(UNDERWATER) && !defined(F_BENCH)
		if (!running) { gFadeSte = (u8)StIsSTE(); FadeSet(0); }                          /* before the picture is shown: all picture colours yellow (the descent palette would show the sky black) */
#endif
		StPresent(back);                                   /* shown from the next vertical blank on */
#if !defined(UNDERWATER) && !defined(F_BENCH)
		if (running && gFadeWait && ++gFadeWait > FADE_PICS) { gFadeT0 = stFrame; gFadeWait = 0; gFade = 1; }      /* a few pictures are on the screen: the fade (in the vertical blank) starts */
#endif
		if (!running) {
			StWaitPresented();
#if !defined(UNDERWATER) && !defined(F_BENCH)
			gFadeStep = 0; gFadeWait = FADE_ON;      /* the first flight picture is on display, all yellow: every colour fades to its final value (the colours 0 .. 3 are the same in both palettes) */
#else
			StSetPalette(palette);
#endif
			running = 1;
		}      /* the first picture is on display: show it, start the flight and the scroller */
	}

	MusicStop();
	StExit();
	return 0;
}
