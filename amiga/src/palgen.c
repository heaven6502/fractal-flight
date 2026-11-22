/* palgen.c - procedural palettes (see palgen.h).  A sky colour from a random hue, mountains in a related darker colour that fades towards the sky with the distance (the three bands), a border colour, a sun
   (halo / body / core) and three star shades.  Day, dusk and night skies, and a few colour schemes between the sky and the mountains (same hue, neighbours, opposite, a triad).  Integer only. */
#include "palgen.h"

static unsigned long rs;
static int Rnd(int n) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (int)((rs >> 8) % (unsigned long)n); }
static int Rng(int a, int b) { return a + Rnd(b - a + 1); }

static void Hsv(int h, int s, int v, int *r, int *g, int *b) {         /* h 0 .. 359, s, v 0 .. 255 -> r, g, b 0 .. 255 */
	int i, f, p, q, t;
	h %= 360; if (h < 0) h += 360;
	i = h / 60; f = (h % 60) * 255 / 60;
	p = v * (255 - s) / 255; q = v * (255 - s * f / 255) / 255; t = v * (255 - s * (255 - f) / 255) / 255;
	switch (i) {
	case 0: *r = v; *g = t; *b = p; break;
	case 1: *r = q; *g = v; *b = p; break;
	case 2: *r = p; *g = v; *b = t; break;
	case 3: *r = p; *g = q; *b = v; break;
	case 4: *r = t; *g = p; *b = v; break;
	default: *r = v; *g = p; *b = q; break;
	}
}
static unsigned short Rgb12(int r, int g, int b) {
	r = (r + 8) >> 4; g = (g + 8) >> 4; b = (b + 8) >> 4;
	if (r > 15) r = 15; if (g > 15) g = 15; if (b > 15) b = 15;
	return (unsigned short)((r << 8) | (g << 4) | b);
}
static unsigned short Hsv12(int h, int s, int v) { int r, g, b; Hsv(h, s, v, &r, &g, &b); return Rgb12(r, g, b); }
static unsigned short MixHsv(int h0, int s0, int v0, int h1, int s1, int v1, int w) {      /* between two colours in hue / saturation / value (the hue the short way round): the bands keep their colour instead of going grey */
	int d = ((h1 - h0 + 540) % 360) - 180;
	return Hsv12(h0 + d * w / 256, s0 + (s1 - s0) * w / 256, v0 + (v1 - v0) * w / 256);
}
static unsigned short Mix12(unsigned short a, unsigned short b, int w) {      /* a + (b - a) * w / 256, per 4 bit channel */
	int sh, v = 0;
	for (sh = 8; sh >= 0; sh -= 4) { int x = (a >> sh) & 15, y = (b >> sh) & 15; v |= (x + (((y - x) * w + 128) >> 8)) << sh; }
	return (unsigned short)v;
}

void PalGen(struct PalEnt *p, unsigned long seed) {
	int mode, hue, hb, hs, sSky, vSky, sBase, vBase, k;
	static const short dh[12] = { 20, -20, 35, -35, 50, -50, 75, -75, 100, -100, 130, -130 };       /* the mountains' hue away from the sky's: from a little (20) to far (130) but never the same hue and never opposite */
	rs = seed * 2654435761UL + 99991UL; if (!rs) rs = 1;
	Rnd(2); Rnd(2);
	mode = Rnd(10);                                 /* 0 .. 3 day, 4 .. 6 dusk, 7 .. 9 night */
	{ static const short anchor[14] = { 5, 20, 35, 140, 170, 190, 205, 220, 240, 260, 280, 300, 320, 340 };       /* the sky hues that look good: reds and ambers, a green, teal, blues, violets, pinks (no mud, no lime) */
	  hue = anchor[Rnd(14)] + Rng(-12, 12); }
	hb = hue + dh[Rnd(12)];
	if (mode < 4)      { sSky = Rng(70, 190); vSky = Rng(215, 255); vBase = Rng(40, 85);  sBase = Rng(130, 230); }
	else if (mode < 7) { sSky = Rng(130, 230); vSky = Rng(150, 215); vBase = Rng(25, 65);  sBase = Rng(150, 240); }
	else               { sSky = Rng(150, 240); vSky = Rng(60, 130);  vBase = Rng(4, 22);   sBase = Rng(120, 240); }
	if (hue % 360 > 50 && hue % 360 < 135) { sSky = sSky * 11 / 20; sBase = sBase * 11 / 20; }        /* greens and yellows: less saturated */
	p->sky = Hsv12(hue, sSky, vSky);
	p->nearC = Hsv12(hb, sBase, vBase);
	p->mid = MixHsv(hb, sBase, vBase, hue, sSky, vSky, 70);       /* the bands fade towards the sky with the distance, the hue too */
	p->farC = MixHsv(hb, sBase, vBase, hue, sSky, vSky, 135);
	p->bottom = Hsv12(hb, sBase, vBase / 2);
	p->bar = Mix12(p->mid, p->sky, 80);
	hs = Rnd(5) < 3 ? Rng(20, 60) : hue + 180;      /* the sun: warm, or the opposite of the sky */
	if (mode >= 7) {                                /* a night sky: a pale sun / moon */
		p->halo = Hsv12(hs, Rng(60, 110), 205); p->body = Hsv12(hs, Rng(30, 70), 235); p->core = Hsv12(hs, 10, 255);
	} else {
		p->halo = Hsv12(hs, Rng(150, 215), 255); p->body = Hsv12(hs, Rng(90, 150), 255); p->core = Hsv12(hs, Rng(15, 50), 255);
	}
	k = Rng(25, 70);
	p->star0 = Hsv12(hs, k + 20, 215); p->star1 = Hsv12(hs, k, 245); p->star2 = 0x0FFF;
}
