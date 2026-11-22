/* test_flight_host.c - checks flight_core.c against
     testdata/flight_render.bin  (55 reference renders of jaggie_3d.exe: line pixels of rows 0..42 + the topmost ridge pixel of every column)
     testdata/flight_flight.bin  (60 flight sequences of the Python model jaggie_flight.py, all 41 state bytes after every tick)
   Host build: gcc -O2 -DF_WIDE=0 -Isrc src/flight_core.c src/flight_data.c test/test_flight_host.c   (run from the project root; -DF_WIDE=0 = the game's 8 bit arithmetic, the vectors are from the game) */
#include <stdio.h>
#include <string.h>
#include "flight_core.h"

/* picture pixel access: byte per pixel (scr[]) or, with -DPLOT_PLANAR, the planar plane the Amiga build draws into */
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
static int GROUND(int sr, int i) { return (hostGround[sr * FSTRIDE + FPAD + (i >> 2)] & (0xC0 >> ((i & 3) * 2))) != 0; }
#else
#define PIX(r, i) scr[(r) * VIEW_W + (i)]
#endif

static unsigned char *rd(FILE *f, unsigned char *buf, int n) { return fread(buf, 1, n, f) == (size_t)n ? buf : 0; }

int main(void) {
    FILE *f;
    int ncase, c, bad = 0, i, r;
    unsigned char hdr[16], rows[43 * 21], ridge[168];
    TFlight fl;

    f = fopen("testdata/flight_render.bin", "rb");
    if (!f) { puts("testdata/flight_render.bin not found"); return 2; }
    rd(f, hdr, 2); ncase = hdr[0] | hdr[1] << 8;
    for (c = 0; c < ncase; c++) {
        unsigned short cam[8];
        int pixdiff = 0, ridgediff = 0;
        if (!rd(f, heightmap, 256) || !rd(f, (unsigned char *)cam, 16) || !rd(f, rows, sizeof rows) || !rd(f, ridge, 168)) { puts("short read"); return 2; }
        NewFlight(&fl);
        fl.posXLo = cam[0] & 255; fl.posXHi = cam[0] >> 8; fl.posZLo = cam[1] & 255; fl.posZHi = cam[1] >> 8;
        fl.altLo = cam[2] & 255; fl.altHi = cam[2] >> 8; fl.headLo = cam[3] & 255; fl.headHi = cam[3] >> 8;
        fl.pitchLo = cam[4] & 255; fl.pitchHi = cam[4] >> 8; fl.rollLo = cam[5] & 255; fl.rollHi = cam[5] >> 8;
        fl.bankLo = cam[6] & 255; fl.bankHi = cam[6] >> 8; fl.speedLo = cam[7] & 255; fl.speedHi = cam[7] >> 8;
#ifdef PLOT_PLANAR
        memset(hostPlane, 0, sizeof hostPlane); fPlane = hostPlane;
#endif
        RenderScene(&fl);
        for (r = 0; r < 43; r++)
            for (i = 0; i < 168; i++) {
                int want = (rows[r * 21 + i / 8] >> (7 - (i & 7))) & 1;
                if (PIX(r, i) != want) { if (++pixdiff <= 3) printf("  case %d pixel row %d col %d mine %d ref %d\n", c, r, i, PIX(r, i), want); }
            }
        for (i = 0; i < 168; i++) {                                    /* topmost ridge pixel per column, all 48 rows */
            int top = 0xFF;
            for (r = 0; r < 48; r++) if (PIX(r, i) == 1) { top = 0x97 - r; break; }
            if (top != ridge[i]) { if (++ridgediff <= 3) printf("  case %d ridge col %d mine $%02X ref $%02X\n", c, i, top, ridge[i]); }
        }
#ifdef PLOT_PLANAR
        {   /* the blitter drip: every screen row = plane row OR the ground row above it (rows 0..128), must equal "below the first ridge pixel" */
            int sr, b2, grounddiff = 0;
            memset(hostGround, 0, sizeof hostGround);
            for (sr = 0; sr < 129; sr++)
                for (b2 = FPAD; b2 < FPAD + 42; b2++)
                    hostGround[sr * FSTRIDE + b2] = hostPlane[sr * FSTRIDE + b2] | (sr ? hostGround[(sr - 1) * FSTRIDE + b2] : 0);
            for (i = 0; i < 168; i++) {
                int first = 99;
                for (r = 0; r < 48; r++) if (PIX(r, i) == 1) { first = r; break; }
                for (r = 0; r < 43; r++) {
                    int L = PIX(r, i) == 1, g = GROUND(r * 3 + 1, i), p0 = L || !g;           /* plane 0 after the sky pass */
                    int cls = g * 2 + p0;                                                       /* 1 sky, 2 ground, 3 line */
                    int want = (r < first) ? 1 : L ? 3 : 2;
                    if (g != (r >= first) || cls != want) grounddiff++;
                }
            }
            if (grounddiff) { ridgediff += grounddiff; printf("  case %d: %d ground (drip) differences\n", c, grounddiff); }
        }
#endif
        if (pixdiff || ridgediff) { bad++; printf("render case %d: %d pixels, %d ridge columns differ\n", c, pixdiff, ridgediff); }
    }
    fclose(f);
    printf("render: %d cases, bad %d\n", ncase, bad);

    /* flight model */
    {
        int nseq, s, nticks, t, badseq = 0, total = 0, hits = 0, fields = (int)sizeof(TFlight);
        unsigned char init[sizeof(TFlight)], want[sizeof(TFlight)], tk[3];
        unsigned char seed;
        if (fields != 41) { puts("TFlight is not 41 bytes"); return 2; }
        f = fopen("testdata/flight_flight.bin", "rb");
        if (!f) { puts("testdata/flight_flight.bin not found"); return 2; }
        rd(f, hdr, 2); nseq = hdr[0] | hdr[1] << 8;
        for (s = 0; s < nseq; s++) {
            int seqbad = 0;
            rd(f, hdr, 2); nticks = hdr[0] | hdr[1] << 8;
            rd(f, init, 41); rd(f, heightmap, 256); rd(f, &seed, 1);
            memcpy(&fl, init, 41);
            fUseLcg = 1; fRndState = seed;
            for (t = 0; t < nticks; t++) {
                int hit;
                rd(f, tk, 3); rd(f, want, 41);
                if (tk[0] == 1) KeyLand(&fl);
                else if (tk[0] == 2) KeyTakeoff(&fl);
                FlightTick(&fl, tk[1]);
                hit = GroundCollision(&fl);
                total++; hits += hit;
                if (memcmp(&fl, want, 41) != 0 || hit != tk[2]) {
                    if (!seqbad && badseq < 3) {
                        int k;
                        printf("sequence %d tick %d differs:", s, t);
                        for (k = 0; k < 41; k++) if (((unsigned char *)&fl)[k] != want[k]) printf(" [%d]=%02X/%02X", k, ((unsigned char *)&fl)[k], want[k]);
                        printf(" hit %d/%d\n", hit, tk[2]);
                    }
                    seqbad = 1;
                    break;                                              /* the rest of the sequence would only repeat the same error */
                }
            }
            if (seqbad) badseq++;
        }
        fclose(f);
        printf("flight: %d sequences, %d ticks compared, %d ground hits, bad %d\n", nseq, total, hits, badseq);
        return (bad || badseq) ? 1 : 0;
    }
}
