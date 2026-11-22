/* st_logo.c - the intro picture: fade in, hold, fade out (the palette is scaled in 8 steps; the picture is on a screen that is already shown with a black palette) */
#include "st_logo.h"

extern const u16 logoPal[16];                         /* ST: 3 bits per channel */
extern const u16 logoPal4[16];                        /* the same colours, 4 bits per channel (plain 0RGB nibbles) for an STE */
extern const u16 logoImg[16000];

void LogoDraw(u8 *screen) {
	u16 *d = (u16 *)screen;
	int i;
	for (i = 0; i < 16000; i++) d[i] = logoImg[i];
}

static int ste = -1;                                   /* 1 = the 4 bit palette registers of an STE */

static u16 StePack(int v) { return (u16)(((v & 1) << 3) | (v >> 1)); }      /* a 4 bit value in the STE register format: bit 3 is the lowest bit */

static void Fade(int level) {                          /* 0 = black .. 8 = the palette */
	u16 p[16];
	int i;
	if (ste < 0) ste = StIsSTE();
	for (i = 0; i < 16; i++) {
		if (ste) {                                     // 4 bits per channel
			int r = (logoPal4[i] >> 8) & 15, g = (logoPal4[i] >> 4) & 15, b = logoPal4[i] & 15;
			p[i] = (u16)((StePack((r * level + 4) >> 3) << 8) | (StePack((g * level + 4) >> 3) << 4) | StePack((b * level + 4) >> 3));
		} else {                                       // 3 bits per channel
			int r = (logoPal[i] >> 8) & 7, g = (logoPal[i] >> 4) & 7, b = logoPal[i] & 7;
			p[i] = (u16)((((r * level + 4) >> 3) << 8) | (((g * level + 4) >> 3) << 4) | ((b * level + 4) >> 3));
		}
	}
	StSetPalette(p);
}

static int Wait(int vbls) {                            /* 1 = fire pressed */
	u16 t = stFrame;
	while ((u16)(stFrame - t) < (u16)vbls && !stQuit)
		if (StInput() & ST_FIRE) return 1;
	return 0;
}

void LogoIntro(u32 fadeOutAtPlay) {
	int level, skip = 0;
	u16 t0;
	for (level = 1; level <= 8 && !skip; level++) { Fade(level); skip = Wait(5); }
	t0 = stFrame;
	while (!skip && !stQuit && StSndhPlays() < fadeOutAtPlay && (u16)(stFrame - t0) < 600)
		if (StInput() & ST_FIRE) skip = 1;
	for (level = 7; level >= 0; level--) { Fade(level); Wait(5); }
}
