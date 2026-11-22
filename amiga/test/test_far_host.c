/* test_far_host.c - checks flight_core.c (the latest iron port geometry: FAR + D80, 40 or 42 columns) against reference renders of the REAL 6502 build
   (testdata/far40_render.bin / far42_render.bin, made by tools/make_far_vectors.py through AltirraBridge): for every case
     - the ridge buffer ($2608) of all columns,
     - every ridge pixel of the 80 bitmap rows (the reference is taken at the entry of the game's sky fill, which blends 4 pixel bytes and
       would overwrite ridge pixels; the sky / ground colouring of the Amiga build is its own rule: ground = below the first ridge pixel of
       the column, checked here against the planar row-OR drip).
   Host build (from the project root):
     gcc -O2 -Isrc src/flight_core.c src/flight_data.c test/test_far_host.c                  40 columns, byte per pixel
     gcc -O2 -DPLOT_PLANAR -Isrc ...                                                           40 columns, planar plotting + the blitter's row-OR drip
     add -DFLIGHT_WIDE for the 42 column build */
#include <stdio.h>
#include <string.h>
#include "flight_core.h"

#ifdef PLOT_PLANAR
static unsigned char hostPlane[FSTRIDE * FROWS], hostGround[FSTRIDE * FROWS];
static int PIX(int r, int i) {
    unsigned char m = 0xC0 >> ((i & 3) * 2);
    int b = FPAD + (i >> 2), k, v = -1;
    for (k = 0; k < 3; k++) {                       /* the 3 rows of a picture row, both bits of a picture pixel */
        int bits = hostPlane[(r * 3 + k) * FSTRIDE + b] & m;
        int x = (bits == m) ? 1 : (bits == 0) ? 0 : 2;
        if (x == 2 || (v >= 0 && v != x)) return 2; /* 2 = inconsistent */
        v = x;
    }
    return v;
}
static int GROUND(int r, int i) { return (hostGround[(r * 3 + 1) * FSTRIDE + FPAD + (i >> 2)] & (0xC0 >> ((i & 3) * 2))) != 0; }
#else
#define PIX(r, i) scr[(r) * VIEW_W + (i)]
#endif

int main(void) {
    static unsigned char hm[256], lb[VIEW_W], bm[F_ROWS * (VIEW_W / 4)];
    unsigned short cam[8];
    unsigned char h[2];
    FILE *f;
    int ncase, c, i, r, bad = 0, ridgeTotal = 0, pixTotal = 0, classTotal = 0, v3 = 0;
    TFlight fl;

    f = fopen(F_NARROW ? "testdata/far40_render.bin" : "testdata/far42_render.bin", "rb");
    if (!f) { puts("reference file not found"); return 2; }
    fread(h, 1, 2, f); ncase = h[0] | h[1] << 8;
    for (c = 0; c < ncase; c++) {
        int ridgediff = 0, pixdiff = 0, classdiff = 0;
        if (fread(hm, 1, 256, f) != 256 || fread(cam, 1, 16, f) != 16 || fread(lb, 1, VIEW_W, f) != VIEW_W || fread(bm, 1, sizeof bm, f) != sizeof bm) { puts("short read"); return 2; }
        memcpy(heightmap, hm, 256);
        NewFlight(&fl);
        fl.posXLo = cam[0] & 255; fl.posXHi = cam[0] >> 8; fl.posZLo = cam[1] & 255; fl.posZHi = cam[1] >> 8;
        fl.altLo = cam[2] & 255; fl.altHi = cam[2] >> 8; fl.headLo = cam[3] & 255; fl.headHi = cam[3] >> 8;
        fl.pitchLo = cam[4] & 255; fl.pitchHi = cam[4] >> 8; fl.rollLo = cam[5] & 255; fl.rollHi = cam[5] >> 8;
        fl.bankLo = cam[6] & 255; fl.bankHi = cam[6] >> 8; fl.speedLo = cam[7] & 255; fl.speedHi = cam[7] >> 8;
#ifdef PLOT_PLANAR
        memset(hostPlane, 0, sizeof hostPlane); fPlane = hostPlane;
#endif
        RenderScene(&fl);

        for (i = 0; i < VIEW_W; i++)                                    /* the ridge buffer */
            if (fLineBuf[F_COL_LO + i] != lb[i]) { if (++ridgediff <= 3) printf("  case %d ridge buffer column %d mine $%02X game $%02X\n", c, F_COL_LO + i, fLineBuf[F_COL_LO + i], lb[i]); }

#ifdef PLOT_PLANAR
        {   /* the blitter's drip over all rows: ground(r) = line(r) OR ground(r-1) */
            int sr, b2;
            memset(hostGround, 0, sizeof hostGround);
            for (sr = 0; sr < FROWS; sr++)
                for (b2 = FPAD; b2 < FPAD + FINNER; b2++)
                    hostGround[sr * FSTRIDE + b2] = hostPlane[sr * FSTRIDE + b2] | (sr ? hostGround[(sr - 1) * FSTRIDE + b2] : 0);
        }
#endif
        for (i = 0; i < VIEW_W; i++) {
            int first = 999;
            for (r = 0; r < F_ROWS; r++) if (PIX(r, i) == 1) { first = r; break; }
            for (r = 0; r < F_ROWS; r++) {
                int v = (bm[r * (VIEW_W / 4) + (i >> 2)] >> (6 - 2 * (i & 3))) & 3;   /* 2 = ridge pixel, 0 = empty */
                int mine = PIX(r, i);
                if (v == 1 || v == 3) v3++;
                if ((v == 2) != (mine == 1)) { if (++pixdiff <= 3) printf("  case %d pixel row %d col %d mine %d game value %d\n", c, r, i, mine, v); }
#ifdef PLOT_PLANAR
                if (GROUND(r, i) != (r >= first)) classdiff++;   /* the drip: ground below the first ridge pixel of the column */
#endif
            }
        }
        ridgeTotal += ridgediff; pixTotal += pixdiff; classTotal += classdiff;
        if (ridgediff || pixdiff || classdiff) { bad++; printf("case %d: %d ridge columns, %d pixels, %d classes differ\n", c, ridgediff, pixdiff, classdiff); }
    }
    fclose(f);
    printf("%d columns, %d cases: bad %d  (ridge buffer diffs %d, ridge pixel diffs %d, drip diffs %d, other values seen %d times)\n", VIEW_W, ncase, bad, ridgeTotal, pixTotal, classTotal, v3);
    return bad ? 1 : 0;
}
