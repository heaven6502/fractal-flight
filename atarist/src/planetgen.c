/* planetgen.c - procedural planets (see planetgen.h).  A lit sphere (light from the upper left, 4 x 4 ordered dither) in 8 colours with a surface from 3D value noise on the sphere: Earth-like (ocean, land,
   clouds, ice caps, the glow of the atmosphere), gas giant (warped bands, a storm), cratered rock, lava (glowing cracks), ice giant, and a banded planet with a ring.  The sphere has the geometry of the sun
   (PG_ASPECT: square pixels by default): 32 columns, 42 lines, radius 8 .. 15 columns.  Fixed point only. */
#include "planetgen.h"

unsigned short pgWords[PG_ROWS][PG_WPR];
unsigned short pgPal[PG_NC];

#ifdef PG_ST
static const unsigned short famPal[][6] = {                /* the Atari ST: 6 colours (the palette entries 2 .. 7) */
	{ 0x0026, 0x027C, 0x0241, 0x0573, 0x0EEF, 0x09CF },     /* 0  Earth: ocean dark / lit, land dark / lit, cloud, the glow of the atmosphere */
	{ 0x0210, 0x0642, 0x0863, 0x0A85, 0x0CA7, 0x0FEB },     /* 1  gas giant, brown and cream */
	{ 0x0112, 0x0446, 0x0568, 0x078A, 0x09AB, 0x0DEF },     /* 2  gas giant, blue grey */
	{ 0x0200, 0x0720, 0x0A42, 0x0C64, 0x0FA8, 0x0FDB },     /* 3  gas giant, red orange */
	{ 0x0111, 0x0444, 0x0777, 0x0999, 0x0BBB, 0x0EEE },     /* 4  rock, grey */
	{ 0x0110, 0x0432, 0x0654, 0x0876, 0x0CBA, 0x0EDC },     /* 5  rock, warm */
	{ 0x0100, 0x0533, 0x0900, 0x0E40, 0x0FC4, 0x0C30 },     /* 6  lava: crust dark / lit, cracks red, orange, yellow, the glow of the rim */
	{ 0x0012, 0x0258, 0x047B, 0x08BE, 0x0BDF, 0x0EFF }      /* 7  ice giant */
};
#else
static const unsigned short famPal[][8] = {
	{ 0x0012, 0x0036, 0x027C, 0x0131, 0x0562, 0x0EEF, 0x0779, 0x09CF },     /* 0  Earth: deep ocean / ocean / lit ocean, dark land / lit land, cloud / cloud in the shade, atmosphere */
	{ 0x0210, 0x0421, 0x0642, 0x0863, 0x0A85, 0x0CA7, 0x0EC9, 0x0FEB },     /* 1  gas giant, brown and cream */
	{ 0x0112, 0x0224, 0x0446, 0x0568, 0x078A, 0x09AB, 0x0BCD, 0x0DEF },     /* 2  gas giant, blue grey */
	{ 0x0200, 0x0410, 0x0720, 0x0A42, 0x0C64, 0x0E86, 0x0FA8, 0x0FDB },     /* 3  gas giant, red orange */
	{ 0x0111, 0x0222, 0x0444, 0x0666, 0x0888, 0x0AAA, 0x0CCC, 0x0EEE },     /* 4  rock, grey */
	{ 0x0110, 0x0221, 0x0432, 0x0654, 0x0876, 0x0A98, 0x0CBA, 0x0EDC },     /* 5  rock, warm */
	{ 0x0100, 0x0311, 0x0533, 0x0900, 0x0E40, 0x0FA2, 0x0FE8, 0x0C30 },     /* 6  lava: crust (night / dim / lit), cracks (red, orange, yellow, white yellow), the glow of the rim */
	{ 0x0012, 0x0135, 0x0258, 0x047B, 0x069D, 0x08BE, 0x0BDF, 0x0EFF }      /* 7  ice giant */
};
#endif

static inline int MS(int a, int b) { int r = a; __asm__("muls.w %1,%0" : "+d"(r) : "d"(b) : "cc"); return r; }              /* signed 16 x 16 -> 32 bit: the 68000 has no 32 bit multiply (a library call otherwise) */
static inline unsigned MU(unsigned a, unsigned b) { unsigned r = a; __asm__("mulu.w %1,%0" : "+d"(r) : "d"((unsigned short)b) : "cc"); return r; }   /* unsigned 16 x 16 -> 32 bit */
static unsigned short fadeT[256];                /* 3 t^2 - 2 t^3 over 0 .. 255 -> 0 .. 256 */
static unsigned char nz[4096];                    /* the noise lattice 16 x 16 x 16 */
static unsigned long rs;
static int Rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (int)((rs >> 8) & 255); }      /* xorshift: shifts only */
static int Sn(int x) { int t = x & 127, v = MS(t, 128 - t) >> 5; return (x & 128) ? -v : v; }      /* a sine, period 256, +-128 */
static int ISq(int v) {                           /* floor(sqrt(v)), v up to 65536 */
	int res = 0, bit = 1 << 16;
	while (bit > v) bit >>= 2;
	while (bit) { if (v >= res + bit) { v -= res + bit; res = (res >> 1) + bit; } else res >>= 1; bit >>= 2; }
	return res;
}
static int N3(int x, int y, int z) {              /* value noise, 8.8 coordinates, wraps every 16 cells: 0 .. 255 */
	int xi = (x >> 8) & 15, yi = (y >> 8) & 15, zi = (z >> 8) & 15, xj = (xi + 1) & 15, yj = (yi + 1) & 15, zj = (zi + 1) & 15;
	int tx = x & 255, ty = y & 255, tz = z & 255;
	int fx = fadeT[tx], fy = fadeT[ty], fz = fadeT[tz];
	int a000 = nz[(xi << 8) | (yi << 4) | zi], a100 = nz[(xj << 8) | (yi << 4) | zi], a010 = nz[(xi << 8) | (yj << 4) | zi], a110 = nz[(xj << 8) | (yj << 4) | zi];
	int a001 = nz[(xi << 8) | (yi << 4) | zj], a101 = nz[(xj << 8) | (yi << 4) | zj], a011 = nz[(xi << 8) | (yj << 4) | zj], a111 = nz[(xj << 8) | (yj << 4) | zj];
	int x00 = a000 + (MS(a100 - a000, fx) >> 8), x10 = a010 + (MS(a110 - a010, fx) >> 8);
	int x01 = a001 + (MS(a101 - a001, fx) >> 8), x11 = a011 + (MS(a111 - a011, fx) >> 8);
	int y0 = x00 + (MS(x10 - x00, fy) >> 8), y1 = x01 + (MS(x11 - x01, fy) >> 8);
	return y0 + (MS(y1 - y0, fz) >> 8);
}
static int Fbm(int x, int y, int z, int oct) {    /* 0 .. 255 */
	static const int kk[5] = { 0, 512, 341, 292, 273 };      /* 65536 / the sum of the weights 128, 64, 32, 16 */
	int s = 0;
	for (int o = 0; o < oct; o++) { s += N3(x, y, z) << (7 - o); x = (x << 1) + 0x137; y = (y << 1) + 0x2B5; z = (z << 1) + 0x4D1; }
	return (int)(MU((unsigned)s, (unsigned)kk[oct]) >> 16);
}
static int Lv(int lum, int K, int th) {           /* the shade 0 .. K of the brightness lum (0 .. 255), dithered with the threshold th (0 .. 255) */
	int q = MS(lum, K), s = (q >> 8) + ((q & 255) > th ? 1 : 0);
	return s > K ? K : s;
}

#ifndef PG_ASPECT
#ifdef PG_ST
#define PG_ASPECT 120                              /* the ST: 320 x 200 on a 4:3 monitor, a line is 1.2 columns high */
#else
#define PG_ASPECT 100                              /* the height of a screen line in columns (x 100): 100 = square pixels (a circle is as many lines as columns); the PAL screen on a 4:3 monitor has 94, the earlier 6/5 factor of the sun (120) made planets flat */
#endif
#endif
static short nxT[32], nyT[PG_ROWS], dvT[PG_ROWS];

static unsigned short newWords[PG_ROWS][PG_WPR];       /* the planet being made (the one on the screen stays in pgWords until PlanetGenCommit) */
static unsigned short newPal[PG_NC];
static int family, R, ang, ox, oy, oz, p1, p2, sx, sy, cx[12], cy[12], cr[12], nc, row;
static int rcos, rsin, rk1, rk2;                  /* the ring: its tilt (cos / sin, +-128) and the reciprocals of its half axes squared */

void PlanetGenBegin(int fam, unsigned long seed) {
	int pr = 0, i, l, col;
	family = fam; R = 14; p1 = p2 = sx = sy = 0; nc = 0; row = 0;
	rs = seed * 2654435761UL + 12345UL;
	if (!rs) rs = 1;
	for (i = 0; i < 256; i++) { int t = i; fadeT[i] = (unsigned short)(((long)t * t * (768 - 2 * t)) >> 16); }
	for (i = 0; i < 4096; i++) nz[i] = (unsigned char)Rnd();
	ang = Rnd(); ox = Rnd() * 16; oy = Rnd() * 16; oz = Rnd() * 16;
	R = family == 5 ? 10 : 14;                               /* the radius of the planet in columns: always the maximum (the ringed ones 10: the ring needs the room); set R = 8 + Rnd() % 8 (ringed: 7 + Rnd() % 5) for random sizes */
	if (family == 5) {                                       /* the ring: flat (mostly), tilted along the horizon, upright, or diagonal */
		int m = Rnd() % 3, a, major, minor;
		a = m == 0 ? Rnd() % 33 - 16 : m == 1 ? 64 + Rnd() % 33 - 16 : (Rnd() & 1 ? 32 : -32) + Rnd() % 17 - 8;
		rcos = Sn(a + 64); rsin = Sn(a);
		major = R * 16 / 10; if (major > 15) major = 15;
		minor = major * (25 + Rnd() % 20) / 100; if (minor < 3) minor = 3;
		rk1 = 65536 / (major * major); rk2 = 65536 / (minor * minor);
	}
	for (i = 0; i < PG_NC; i++) newPal[i] = 0;
	switch (family) {
	case 0: for (i = 0; i < PG_NC; i++) newPal[i] = famPal[0][i]; p1 = 122 + Rnd() % 26; p2 = 150 + Rnd() % 30; break;        /* the land threshold, the cloud threshold */
	case 1: pr = 1 + Rnd() % 3; for (i = 0; i < PG_NC; i++) newPal[i] = famPal[pr][i]; p1 = Rnd() % 6 + 7; sx = (Rnd() - 128) / 2; sy = (Rnd() - 128) / 3; break;   /* the ramp, the band frequency, the storm */
	case 2: pr = 4 + Rnd() % 2; for (i = 0; i < PG_NC; i++) newPal[i] = famPal[pr][i];
		nc = 7 + Rnd() % 4;
		for (i = 0; i < nc; i++) { cx[i] = (Rnd() - 128) * 3 / 2; cy[i] = (Rnd() - 128) * 3 / 2; cr[i] = 26 + Rnd() % 46; } break;
	case 3: for (i = 0; i < PG_NC; i++) newPal[i] = famPal[6][i]; p1 = 214 + Rnd() % 8; break;                                  /* the crack threshold */
	case 4: for (i = 0; i < PG_NC; i++) newPal[i] = famPal[7][i]; p1 = Rnd() % 3 + 9; break;
#ifdef PG_ST
	case 5: pr = 1 + Rnd() % 3; newPal[0] = famPal[pr][0]; newPal[1] = famPal[pr][2]; newPal[2] = famPal[pr][3]; newPal[3] = famPal[pr][5];
		newPal[4] = 0x0874; newPal[5] = 0x0DCA; p1 = Rnd() % 4 + 8; break;
#else
	case 5: pr = 1 + Rnd() % 3; newPal[0] = famPal[pr][0]; newPal[1] = famPal[pr][1]; newPal[2] = famPal[pr][3]; newPal[3] = famPal[pr][4]; newPal[4] = famPal[pr][6]; newPal[5] = famPal[pr][7];
		newPal[6] = 0x0874; newPal[7] = 0x0DCA; p1 = Rnd() % 4 + 8; break;
#endif
	}
	for (col = 0; col < 32; col++) nxT[col] = (short)((2 * col - 31) * 128 / R);
	for (l = 0; l < PG_ROWS; l++) { nyT[l] = (short)(((2 * l - 41) * 128 * PG_ASPECT / 100) / R); dvT[l] = (short)((2 * l - 41) * 128 * PG_ASPECT / 100); }
	for (l = 0; l < PG_ROWS; l++) for (i = 0; i < PG_WPR; i++) newWords[l][i] = 0;
}

int PlanetGenRows(int count) {                    /* the next count lines of the planet (the sprite words go to the new buffer); 1 when it is complete */
	static const unsigned char bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
	int l, col;
	for (l = row; l < PG_ROWS && l < row + count; l++) {
		int ny = nyT[l];
		for (col = 0; col < 32; col++) {
			int nx = nxT[col], r2 = MS(nx, nx) + MS(ny, ny), code = 0, nzv, I, lum, th, c, s, px, py, pz, ux, uy, uz, in = r2 <= 65536;
			th = bayer[(l & 3) * 4 + (col & 3)] * 16 + 8;
			if (in) {
				nzv = ISq(65536 - r2);
				I = (MS(nx, -141) + MS(ny, -115) + MS(nzv, 179)) >> 8;
				if (I < 0) I = 0;
				lum = 22 + (MS(I, 234) >> 8); if (lum > 255) lum = 255;
				c = Sn(ang + 64); s = Sn(ang);
				px = (MS(nx, c) + MS(nzv, s)) >> 7; pz = (MS(nzv, c) - MS(nx, s)) >> 7; py = -ny;
				ux = ox + (MS(px, 26) >> 4); uy = oy + (MS(py, 26) >> 4); uz = oz + (MS(pz, 26) >> 4);
				switch (family) {
				case 0: {
					int h = Fbm(ux, uy, uz, 3), cn = Fbm(oz + (MS(px, 38) >> 4) + 900, oy + (MS(py, 80) >> 4) + 500, ox + (MS(pz, 38) >> 4) + 300, 3);
					int white = cn > p2 || (py > 238 || py < -238);
#ifdef PG_ST
					if (white) code = 5;
					else if (h > p1) code = 3 + Lv(lum, 1, th);
					else code = 1 + Lv(lum, 1, th);
					if (!white && r2 > 56000 && I > 40) code = 6;
#else
					if (white) code = Lv(lum, 1, th) ? 6 : 7;
					else if (h > p1) code = 4 + Lv(lum, 1, th);
					else code = 1 + Lv(lum, 2, th);
					if (!white && r2 > 56000 && I > 40) code = 8;
#endif
					break; }
				case 1: case 5: {
					int w = Fbm(ux + 700, uy, uz, 3), phase = (MS(py, p1 * 3) >> 4) + (w - 128), band = Sn(phase) + 128, v;
					int K = family == 1 ? PG_NC - 1 : PG_NC - 3;
					band = 64 + (MS(band, 3) >> 2);
					if (family == 1) { int dxs = px - 70 - sx, dys = (py + 60 - sy) * 2, dd = MS(dxs, dxs) + MS(dys, dys); if (dd < 5184) band = dd < 1600 ? 236 : 205; }
					v = MS(band, lum) >> 7; if (v > 255) v = 255;
					code = 1 + Lv(v, K, th);
					break; }
				case 2: {
					int alb = 96 + (Fbm(ux, uy, uz, 3) >> 2), v, k;
					for (k = 0; k < nc; k++) {
						int dx = nx - cx[k], dy = ny - cy[k], rr = cr[k] + (cr[k] >> 1), dd, rr2;
						if (dx > rr || dx < -rr || dy > rr || dy < -rr) continue;
						dd = MS(dx, dx) + MS(dy, dy); rr2 = MS(cr[k], cr[k]);
						if (dd < rr2) alb -= (int)((MU((unsigned)(rr2 - dd), 70) << 0) / (unsigned)rr2);
						else if (dd < rr2 + (rr2 >> 1) + (rr2 >> 3)) alb += 28;
					}
					if (alb < 8) alb = 8;
					v = MS(alb, lum) >> 7; if (v > 255) v = 255;
					code = 1 + Lv(v, PG_NC - 1, th);
					break; }
				case 3: {
					int n2 = Fbm(ux, uy, uz, 4), ridge = 255 - (n2 > 127 ? (n2 - 127) * 2 : (127 - n2) * 2);
#ifdef PG_ST
					if (ridge > p1) { int gl = (ridge - p1) >> 3; code = 3 + (gl > 2 ? 2 : gl); }
					else code = 1 + Lv(lum, 1, th);
					if (code < 3 && r2 > 56000) code = 6;
#else
					if (ridge > p1) { int gl = (ridge - p1) >> 3; code = 4 + (gl > 3 ? 3 : gl); }
					else code = 1 + Lv(lum, 2, th);
					if (code < 4 && r2 > 56000) code = 8;
#endif
					break; }
				case 4: {
					int w = Fbm(ux + 700, uy, uz, 2), phase = (MS(py, p1 * 3) >> 4) + (w - 128), v0 = 150 + (Sn(phase) >> 2), v;
					if (Fbm(MS(px, 6) >> 4, (MS(py, 80) >> 4) + oy, (MS(pz, 6) >> 4) + oz, 3) > 158) v0 -= 40;
					if (py > 205) v0 = 255; else if (py > 180) v0 -= 40;
					v = MS(v0, lum) >> 7; if (v > 255) v = 255;
					code = 1 + Lv(v, PG_NC - 1, th);
					break; }
				}
			}
			if (family == 5) {                      /* the ring: an ellipse (15.8 x 5.6 pixels) tilted a little, two bands and a gap; the near half is in front of the planet, the far half behind it */
				int du8 = (2 * col - 31) * 128, dv8 = dvT[l];
				int u8 = (MS(du8, rcos) + MS(dv8, rsin)) >> 7, v8 = (MS(dv8, rcos) - MS(du8, rsin)) >> 7;
				unsigned tu = (unsigned)(MS(u8, u8) >> 8), tv = (unsigned)(MS(v8, v8) >> 8);
				int e;
				if (tu > 65535) tu = 65535;
				if (tv > 65535) tv = 65535;
				e = (int)(MU(tu, (unsigned)rk1) >> 16) + (int)(MU(tv, (unsigned)rk2) >> 16);
				if (e < 256 && e > 102 && !(e > 159 && e < 179)) {
					int rc = e < 159 ? PG_NC - 1 : PG_NC;
					if (v8 > 0 || !in) code = rc;
				}
			}
			if (code) {
				int sp = col >> 4, b = 0x8000 >> (col & 15);
#ifdef PG_ST
				code++;                                  /* the palette entries 2 .. 7 (the pixel value = the entry; planes 0 .. 2: the words [0 1] [2 3] [4 5]) */
				if (code & 1) newWords[l][sp] |= (unsigned short)b; if (code & 2) newWords[l][2 + sp] |= (unsigned short)b; if (code & 4) newWords[l][4 + sp] |= (unsigned short)b;
#else
				if (code & 1) newWords[l][4 * sp] |= (unsigned short)b; if (code & 2) newWords[l][4 * sp + 1] |= (unsigned short)b; if (code & 4) newWords[l][4 * sp + 2] |= (unsigned short)b; if (code & 8) newWords[l][4 * sp + 3] |= (unsigned short)b;
#endif
			}
		}
	}
	row = l;
	return row >= PG_ROWS;
}

void PlanetGenCommit(void) {                      /* the new planet becomes the one shown */
	int l, i;
	for (l = 0; l < PG_ROWS; l++) for (i = 0; i < PG_WPR; i++) pgWords[l][i] = newWords[l][i];
	for (i = 0; i < PG_NC; i++) pgPal[i] = newPal[i];
}

void PlanetGen(int fam, unsigned long seed) { PlanetGenBegin(fam, seed); PlanetGenRows(PG_ROWS); PlanetGenCommit(); }
