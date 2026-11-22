/* flight_core.c - C translation of Atari\rof\jaggie_flight.pbi + jaggie_3d.pb, kept line by line (see flight_core.h). */
#include "flight_core.h"
#ifdef FLIGHT_AMIGA
#include "gcc8_c_support.h"
#include "prof.h"
#else
#include <string.h>
#define PROF(k, ...) __VA_ARGS__
#define PCOUNT(k) ((void)0)
#endif

/* F_WIDE (1 = on): the 16 / 32 bit extension of the 8 bit game arithmetic, from the PureBasic prototype jaggie_3d_16bit.pb (see C:/coding/Atari/rof/jaggie_16bit_analysis.md):
   - position inside the cell with a 12 bit fraction (the game: 8 bit),
   - camera heading: 14 bit angle, 16 bit sine table mirrored the right way (the game: 11 bit angle, mirrored one step = 1.4 degrees off in two quadrants, the view jumped when the heading crossed 90 / 180 / 270 degrees),
   - exact perspective (a 32 / 16 bit division per coordinate, result in 8.8 = sub pixel vertices) and the exact horizon ramp per column (the game: 8 bit quotient, 32 byte ramp table of 8 column steps),
   - velocity = speed 8.8 x 16 bit sine (the game: 8 bit speed x 8 bit sine, the ground track is up to 12 degrees off the heading), 8 more position bits.
   The camera space tables are kept in 16 times the game's units (vx16, vz16, vdy16); vx, vz, vdy stay in game units for everything else (autopilot, depth bands). */
#ifndef F_WIDE
#define F_WIDE 1
#endif
#if F_WIDE
#ifdef __mc68000__
static inline unsigned int DivU(unsigned int n, unsigned int d) { __asm__("divu.w %1,%0" : "+d"(n) : "d"(d) : "cc"); return n; }   /* low word = quotient, high word = remainder (the quotient must fit 16 bit) */
#else
static unsigned int DivU(unsigned int n, unsigned int d) { return ((n % d) << 16) | (n / d); }
#endif
#endif

#define Y_MIN F_YMIN
#define Y_MAX F_YTOP
#define X_MIN F_COL_LO
#define X_MAX (F_COL_HI - 1)

u8 heightmap[256];
u8 scr[VIEW_W * VIEW_H];
int fRndState = 1, fUseLcg = 0;
u8 *fPlane;
void (*fBeforePlot)(void);
#ifdef FLIGHT_BANDS
/* depth bands (flightz): the ridge pixels of every depth class go into their own plane, fClassOff bytes after fPlane (fClassW: the same as a word for the assembly plot) */
int fClassOff;
unsigned short fClassW;
#endif

static int carry;
static unsigned int fSeed = 1;
void FSeed(unsigned int seed) { fSeed = seed ? seed : 1; }
static int frand8(void) { fSeed = fSeed * 1103515245u + 12345u; return (int)((fSeed >> 16) & 255); }

static int iabs(int v) { return v < 0 ? -v : v; }

/* ---- 6502 helpers ---- */
static int Adc8(int a, int b, int c) { int t = a + b + c; carry = t >> 8; return t & 0xFF; }
static int Sbc8(int a, int b, int c) { int t = a - b - (1 - c); carry = (t >= 0) ? 1 : 0; return t & 0xFF; }

static int Rnd8(void) {
    fRndState = (fRndState * 5 + 17) & 0xFF;       /* same generator as the Python model (jaggie_flight.Lcg) */
    return fRndState;
}

static int S16(int lo, int hi) { int v = lo | (hi << 8); if (v & 0x8000) v -= 65536; return v; }
static int W16(int v) { v &= 0xFFFF; if (v & 0x8000) v -= 65536; return v; }

/* L8FF9: shift-and-add multiply, result ~ f0*f1/256 */
static int Mul8(int f0, int f1) {
    int a = 0, c;
    for (;;) {
        c = f0 >> 7;
        f0 = (f0 << 1) & 0xFF;
        if (c) {
            c = f1 & 1;
            f1 >>= 1;
            a = (a + f1 + c) & 0xFF;
        } else {
            f1 >>= 1;
            if (f1 == 0) return a;
        }
    }
}

#if F_WIDE
/* the sine table the right way round (65 entries, odd quadrants mirrored with 64 - y), 14 bit heading, linear interpolation with a 6 bit weight: +-65280 */
static int SinV16(int idx) {
    int q, y, v;
    idx &= 255;
    q = idx >> 6;
    y = idx & 63;
    if (q & 1) y = 64 - y;
    v = fSinHi[y] * 256 + fSinLo[y];
    return q >= 2 ? -v : v;
}
static int SinHead(int head) {
    int idx = (head >> 6) & 255, w = head & 63;
    return (SinV16(idx) * (64 - w) + SinV16(idx + 1) * w) >> 6;
}
/* speed 8.8 x sine / 65536 = position units * 256: added to the 24 bit position (posXHi:Lo:fr) */
static int posXfr, posZfr;                              /* 8 more fraction bits of the position (not in TFlight: the 41 byte state of the game stays as it is) */
static void Velocity16(TFlight *f) {
    int head = f->headLo + f->headHi * 256;
    int sp = f->speedHi * 256 + f->speedLo;
    int sX = SinHead(head), sZ = SinHead(head + 0x1000);
    int hi = sp >> 8, lo = sp & 0xFF;                    /* sp * s / 65536 in 32 bit arithmetic: hi * s < 2^24, lo * s < 2^24 */
    int vX = ((hi * sX) >> 8) + ((lo * sX) >> 16);
    int vZ = ((hi * sZ) >> 8) + ((lo * sZ) >> 16);
    int pX = ((f->posXHi << 16) | (f->posXLo << 8) | posXfr) + vX;
    int pZ = ((f->posZHi << 16) | (f->posZLo << 8) | posZfr) + vZ;
    f->posXHi = (u8)(pX >> 16); f->posXLo = (u8)(pX >> 8); posXfr = (u8)pX;
    f->posZHi = (u8)(pZ >> 16); f->posZLo = (u8)(pZ >> 8); posZfr = (u8)pZ;
}
#endif

/* L8F67: heading -> velocity vector */
static void Velocity(TFlight *f) {
    int idx, b7, b6, vxh, vzh, i6, f2, f3, a;
    idx = ((f->headHi << 2) | (f->headLo >> 6)) & 0xFF;
    b7 = idx >> 7;
    b6 = (idx >> 6) & 1;
    vxh = 0;
    if (b7) vxh = 0xFF;
    vzh = b6 ? 0xFF : 0;
    vzh ^= vxh;
    i6 = idx & 0x3F;
    f2 = i6;
    f3 = i6;
    if (b6) f3 = (i6 ^ 0x3F) + 1;
    else f2 = (i6 ^ 0x3F) + 1;
    f->f2 = f2;
    f->f3 = f3;

    a = Mul8(f->speedHi, fSinHi[f3]);
    f->velXLo = a;
    if (a == 0) vxh = 0;
    if (vxh) f->velXLo = (vxh - a - 1) & 0xFF;
    f->velXHi = vxh;

    a = Mul8(f->speedHi, fSinHi[f2]);
    f->velZLo = a;
    if (a == 0) vzh = 0;
    if (vzh) f->velZLo = (vzh - a - 1) & 0xFF;
    f->velZHi = vzh;
}

/* L8BDC: one tick of the landing descent */
static void LandStep(TFlight *f) {
    int hi = f->rollHi, lo = f->rollLo, v, alt;
    if (hi & 0x80) {
        lo += 0x14;
        if (lo > 255) hi = (hi + 1) & 0xFF;
        lo &= 0xFF;
    } else {
        v = lo - 0x14;
        if (v < 0) { v &= 0xFF; hi--; }
        if (hi < 0) { hi = 0; v = 0; }
        lo = v;
    }
    f->rollHi = hi; f->rollLo = lo;
    if (f->m061D) {
        alt = ((f->altHi << 8) | f->altLo) - 0x30;
        f->altLo = alt & 0xFF;
        f->altHi = (alt >> 8) & 0xFF;
    } else if (f->m28A4 == 0) {
        f->m28A4 = 1;
    }
}

int KeyLand(TFlight *f) {
    if (f->m0625 == 0 || f->m28A4 || f->m061C || f->m065F == 0) return 0;
    if (f->m061D >= 0x64) { f->m065F = 0xFF; return 0; }
    f->m065F = 0;
    f->speedCfg = 0;
    return 1;
}

int KeyTakeoff(TFlight *f) {
    if (f->m0625 == 0 || f->m28A4 == 0) return 0;
    if (f->m28A5 == 0) f->m28A4 = 0;
    f->bankLo = 0; f->bankHi = 0;
    f->speedCfg = 0x1E;
    f->m065F = 0xFF;
    return 1;
}

/* one flight tick (L8530), the game runs it every second frame.  porta: bit set = not pressed.  Returns 1 in a mode that is not ported. */
int FlightTick(TFlight *f, int porta) {
    int a, c, t, v, bc, bd, y, i, carryOut, lo, hi, acc, turn, sh, bank, half, bcv;

    /* L8530: stick -> turn_offset and pitch acceleration */
    if (f->m0626 == 2 && f->m28A5 == 0) {
        a = f->speedCfg;
        t = ((a >> 2) + 0x20 + ((a >> 1) & 1)) & 0xFF;
        if ((porta & F_LEFT) == 0) t ^= 0xFF;
        else if (porta & F_RIGHT) t = 0;
        f->turn = t;
        if (f->m293D == 0) v = 0xD0;
        else if ((porta & F_UP) == 0) {
            v = 0xD0;
            if (f->pitchHi == 0xF4 && f->pitchLo == 0) v = 0xFF;
        } else if ((porta & F_DOWN) == 0) {
            v = 0x30;
            if (f->pitchHi == 0x0B && f->pitchLo == 0xFF) v = 0x01;
        } else v = 0;
        f->pitchAcc = v;
    }

    /* L85A7 .. L8636: mode checks (these modes are not ported) */
    if (f->m061C || f->m065F == 2) return 1;
    if (f->m28A4) {
        /* L861D: landed, only graphics; the roll is kept */
    } else if (f->m065F == 0) {
        LandStep(f);
    } else {
        f->rollLo = f->bankLo;
        f->rollHi = f->bankHi;
    }
    if (f->m28A5) return 0;

    /* L8651: bank spring-back when the stick is centred */
    if (f->turn == 0) {
        bc = f->bankHi;
        a = f->bankLo;
        carryOut = 0;
        for (i = 1; i <= 5; i++) {
            c = a >> 7;
            a = (a << 1) & 0xFF;
            carryOut = bc >> 7;
            bc = ((bc << 1) | c) & 0xFF;
        }
        a = f->bankLo;
        if (carryOut) {
            a = Sbc8(a, bc, 1);
            f->bankLo = a;
            if (carry) f->bankHi = (f->bankHi + 1) & 0xFF;
        } else {
            a = Sbc8(a, bc, 0);
            if (carry == 0) {
                f->bankHi = (f->bankHi - 1) & 0xFF;
                if (f->bankHi & 0x80) { a = 0; f->bankHi = 0; }
            }
            f->bankLo = a;
        }
    }

    /* L868C: pitch spring-back */
    if (f->m28A4 == 0 && f->pitchAcc == 0) {
        bc = f->pitchHi;
        a = f->pitchLo;
        carryOut = 0;
        for (i = 1; i <= 2; i++) {
            c = a >> 7;
            a = (a << 1) & 0xFF;
            carryOut = bc >> 7;
            bc = ((bc << 1) | c) & 0xFF;
        }
        a = f->pitchLo;
        if (carryOut) {
            a = Sbc8(a, bc, 1);
            f->pitchLo = a;
            if (carry) f->pitchHi = (f->pitchHi + 1) & 0xFF;
        } else {
            a = Sbc8(a, bc, 0);
            if (carry == 0) {
                f->pitchHi = (f->pitchHi - 1) & 0xFF;
                if (f->pitchHi & 0x80) { a = 0; f->pitchHi = 0; }
            }
            f->pitchLo = a;
        }
    }

    /* L86CA: speed (thrust with random jitter, then drag) */
    y = 1;
    if (f->speedCfg == 0) {
        y = 3;
    } else {
        a = Sbc8(f->speedCfg, f->pitchHi, 1);
        a = Sbc8(a, f->pitchHi, carry);
        bcv = a;
        half = f->speedCfg >> 1;
        c = f->speedCfg & 1;
        a = (half | 7) & Rnd8();
        a = Adc8(a, bcv, c);
        if (carry) a = 0xFF;
        lo = Adc8(a, f->speedLo, 0);
        f->speedLo = lo;
        if (carry) f->speedHi = (f->speedHi + 1) & 0xFF;
    }
    if (f->m293D) {                                          /* L86FB: drag */
        t = f->speedHi + 2;
        bc = t & 0xFF;
        bd = t >> 8;
        a = f->speedLo;
        carryOut = 0;
        for (i = 1; i <= y; i++) {
            c = a >> 7;
            a = (a << 1) & 0xFF;
            v = bc >> 7;
            bc = ((bc << 1) | c) & 0xFF;
            carryOut = bd >> 7;
            bd = ((bd << 1) | v) & 0xFF;
        }
        a = Sbc8(f->speedLo, bc, carryOut);
        f->speedLo = a;
        a = Sbc8(f->speedHi, bd, carry);
        f->speedHi = a;
        if (carry == 0) { f->speedLo = 0; f->speedHi = 0; }
    }

    /* L873A: bank integration, clamped to -5 .. +4.FF */
    turn = f->turn;
    lo = f->bankLo + turn;
    c = lo >> 8;
    f->bankLo = lo & 0xFF;
    if (turn & 0x80) {
        if (c == 0) {
            f->bankHi = (f->bankHi - 1) & 0xFF;
            if ((f->bankHi & 0x80) && f->bankHi < 0xFB) { f->bankHi = (f->bankHi + 1) & 0xFF; f->bankLo = 0; }
        }
    } else if (c) {
        f->bankHi = (f->bankHi + 1) & 0xFF;
        if ((f->bankHi & 0x80) == 0 && f->bankHi >= 5) { f->bankHi = (f->bankHi - 1) & 0xFF; f->bankLo = 0xFF; }
    }

    /* L8778: pitch integration, clamped to -12 .. +11.FF */
    acc = f->pitchAcc;
    lo = f->pitchLo + acc;
    c = lo >> 8;
    f->pitchLo = lo & 0xFF;
    if (acc & 0x80) {
        if (c == 0) {
            f->pitchHi = (f->pitchHi - 1) & 0xFF;
            if ((f->pitchHi & 0x80) && f->pitchHi < 0xF4) { f->pitchHi = (f->pitchHi + 1) & 0xFF; f->pitchLo = 0; }
        }
    } else if (c) {
        f->pitchHi = (f->pitchHi + 1) & 0xFF;
        if ((f->pitchHi & 0x80) == 0 && f->pitchHi >= 0x0C) { f->pitchHi = (f->pitchHi - 1) & 0xFF; f->pitchLo = 0xFF; }
    }

    /* L87B6: climb = speed * pitch */
    hi = f->pitchHi;
    a = f->pitchLo;
    carryOut = 0;
    for (i = 1; i <= 3; i++) {
        c = a >> 7;
        a = (a << 1) & 0xFF;
        carryOut = hi >> 7;
        hi = ((hi << 1) | c) & 0xFF;
    }
    f->m2884 = hi;
    a = hi;
    if (carryOut) a = ((a ^ 0xFF) + 1) & 0xFF;
    f->f0 = a;
    f->f1 = f->speedHi;
    a = Mul8(f->f0, f->f1);
    y = 0;
    if (f->m2884 & 0x80) {
        y = 0xFF;
        a = (a ^ 0xFF) + 1;
        if (a > 255) y = 0;
        a &= 0xFF;
    }
    hi = y;
    for (i = 1; i <= 3; i++) {
        c = a >> 7;
        a = (a << 1) & 0xFF;
        hi = ((hi << 1) | c) & 0xFF;
    }
    f->climbHi = hi;
    f->climbLo = a;

    /* heading += bank / 16 (arithmetic shift of the 16 bit bank), wrap at $4000 */
    bank = S16(f->bankLo, f->bankHi);
    sh = bank >> 4;
    lo = Adc8(f->headLo, sh & 0xFF, 0);
    f->headLo = lo;
    hi = Adc8(f->headHi, (sh >> 8) & 0xFF, carry);
    f->headHi = hi & 0x3F;

#if F_WIDE
    Velocity16(f);                                       /* position += speed * (sin, cos of the heading), 24 bit */
#else
    Velocity(f);

    /* position += velocity */
    lo = Adc8(f->posXLo, f->velXLo, 0);
    f->posXLo = lo;
    f->posXHi = Adc8(f->posXHi, f->velXHi, carry);
    lo = Adc8(f->posZLo, f->velZLo, 0);
    f->posZLo = lo;
    f->posZHi = Adc8(f->posZHi, f->velZHi, carry);
#endif

    /* altitude += climb, clamped */
    lo = Adc8(f->altLo, f->climbLo, 0);
    f->altLo = lo;
    a = Adc8(f->altHi, f->climbHi, carry);
    if (a == 0xFF) a = 0;
    if (a >= 0x50) {
        if (f->m065F != 2) { f->altLo = 0xFF; a = 0x4F; }
        else if (a >= 0x60) f->m283F = 0xFF;
    }
    f->altHi = a;

    /* HUD markers (rounded) */
    f->markPitch = (f->pitchHi + 0x0C + (f->pitchLo >> 7)) & 0xFF;
    f->markRoll = (f->rollHi + 5 + (f->rollLo >> 7)) & 0xFF;
    return 0;
}

/* ---- ground collision: L99C2 (terrain height under the camera) and L97A9 (response) ---- */
#if defined(__mc68000__) && !defined(OLD_OPT)
/* the same in 68000 assembly: each step is the 6502's lsr / adc: the byte that is shifted adds its new value and the bit shifted out (X) to the accumulator (addx.b) */
static inline int Lerp8(int a, int b, int f) {
    int acc;
    __asm__(
        "moveq #0,%[acc]\n"
        ".rept 8\n"
        "add.b %[f],%[f]\n"
        "bcs.s 1f\n"
        "lsr.b #1,%[b]\n"
        "lsr.b #1,%[a]\n"
        "addx.b %[a],%[acc]\n"
        "bra.s 2f\n"
        "1:\n"
        "lsr.b #1,%[a]\n"
        "lsr.b #1,%[b]\n"
        "addx.b %[b],%[acc]\n"
        "2:\n"
        ".endr\n"
        : [acc]"=&d"(acc), [a]"+d"(a), [b]"+d"(b), [f]"+d"(f) : : "cc");
    return acc;
}
#else
static int Lerp8(int a, int b, int f) {
    int acc = 0, k, bit, c;
    for (k = 1; k <= 8; k++) {
        bit = (f >> 7) & 1;
        f = (f << 1) & 0xFF;
        if (bit == 0) {
            b >>= 1;
            c = a & 1;
            a >>= 1;
            acc = (acc + a + c) & 0xFF;
        } else {
            a >>= 1;
            c = b & 1;
            b >>= 1;
            acc = (acc + b + c) & 0xFF;
        }
    }
    return acc;
}
#endif

static int GroundHeight(TFlight *f) {
    int posX, posZ, frX, cX, frZ, cZ, row, row2, i00, i10, h00, h01, h10, h11, a0, y, t;
    posX = f->posXLo + f->posXHi * 256;
    posZ = f->posZLo + f->posZHi * 256;
    frX = (posX >> 4) & 0xFF; cX = (posX >> 12) & 0xFF;
    frZ = (posZ >> 4) & 0xFF; cZ = (posZ >> 12) & 0xFF;
    row = (cZ << 4) & 0xFF;
    i00 = (cX & 15) | row;
    h00 = heightmap[i00];
    h01 = heightmap[((i00 + 1) & 15) | row];
    row2 = (row + 16) & 0xFF;
    i10 = (cX & 15) | row2;
    h10 = heightmap[i10];
    h11 = heightmap[((i10 + 1) & 15) | row2];
    a0 = Lerp8(Lerp8(h00, h01, frX), Lerp8(h10, h11, frX), frZ);
    y = a0 >> 4;
    t = (a0 + y) & 0xFF;
    return (t + (y >> 1) + (y & 1)) & 0xFF;
}

int GroundCollision(TFlight *f) {
    int c, alt, g;
    c = (f->altHi >= 0x40) ? 0x3F : f->altHi;
    alt = ((c << 2) | (f->altLo >> 6)) & 0xFF;
    g = GroundHeight(f);
    if (alt >= g) { f->m061D = alt - g; return 0; }
    if (f->m065F != 0 && f->m0625 != 0 && f->speedHi != 0) f->speedHi = f->speedHi - 1;
    f->altHi = (f->altHi + 1) & 0xFF;
    f->altLo = 0;
    f->m061D = 0;
    if (f->pitchHi & 0x80) { f->pitchHi = (f->pitchHi + 1) & 0xFF; f->pitchLo = 0; }
    return 1;
}

void NewFlight(TFlight *f) {
    memset(f, 0, sizeof(TFlight));
#if F_WIDE
    posXfr = posZfr = 0;
#endif
    f->m0626 = 2;
    f->m065F = 0xFF;
    f->m293D = 0xFF;
    f->speedCfg = 30;
    f->speedLo = 64;
    f->speedHi = 18;
    f->altHi = 0x24;
    f->m0625 = 219;
}

void StartFlight(TFlight *f) {
    NewFlight(f);
    FAutopilotReset();
    f->posXHi = 0x80; f->posZHi = 0x80;
    f->altHi = 0x24;
}

/* ============================================================================================================
   renderer (jaggie_3d.pb)
   ============================================================================================================ */
/* the vertex tables are the members of one struct so that the assembly walk (st_vtab.s, ST_WALK_ASM) addresses them all from one register; the offsets are checked below */
struct VTab {
    int vx16[F_NV + 1], vz16[F_NV + 1];               /* camera space in 16 times the game's units */
    int vx[F_NV + 1], vz[F_NV + 1];                   /* the game's units */
    int vdy16[F_NV], vh[F_NV], vflag[F_NV];
    int vpx[F_NV], vpy[F_NV], vpdone[F_NV];           /* projected vertex (column, row), done flag */
};
struct VTab vt;
int fFlagLimMask = -1;                               /* -1: the frustum flags cut at 27/32 Z (the 160 column window); 0: at 7/8 Z (flight320: the 320 column view shows 160 / 183 = 0.874 Z, an edge that ends beyond 27/32 Z is still partly on screen) */
#if F_WIDE && F_NARROW && defined(FLIGHT_ASM) && F_NV == 55
#define ST_WALK_ASM 1
extern void UpdateWalkAsm(int s, int c, const unsigned char *walk, int b7, int xc, int alt);
_Static_assert(__builtin_offsetof(struct VTab, vz16) == 224 && __builtin_offsetof(struct VTab, vx) == 448 && __builtin_offsetof(struct VTab, vz) == 672 &&
               __builtin_offsetof(struct VTab, vdy16) == 896 && __builtin_offsetof(struct VTab, vh) == 1116 && __builtin_offsetof(struct VTab, vflag) == 1336 &&
               __builtin_offsetof(struct VTab, vpx) == 1556 && __builtin_offsetof(struct VTab, vpy) == 1776 && __builtin_offsetof(struct VTab, vpdone) == 1996, "st_vtab.s: offsets of struct VTab");
extern void ProjectVertexAsm(int i);
int fPitchBase, fRollS;                              /* for the assembly projection: (6 << 8) - 4 * pitch (rows * 256 of the horizon at the middle) and the roll (a short) */
#endif
#define vx16 vt.vx16
#define vz16 vt.vz16
#define vx vt.vx
#define vz vt.vz
#define vdy16 vt.vdy16
#define vh vt.vh
#define vflag vt.vflag
#define vpx vt.vpx
#define vpy vt.vpy
#define vpdone vt.vpdone
static int vcell[F_NV], vdy[F_NV];
static int ramp[32];
u8 fLineBuf[256];
u8 fZ[144];                                          /* the three byte stacks of the line routine, 48 entries each (one base register in the assembly) */
#define ZX (fZ)
#define ZY (fZ + 48)
#define ZH (fZ + 96)
short fSX[16], fSY[16], fSH[16];                    /* the 16 bit stack of edge end points (x, y, h), shared with the assembly DrawEdge */
#define sx fSX
#define sy fSY
#define sh_ fSH
int z82, z84, z86;                                  /* end 1 of the byte routine, shared with the assembly DrawEdge (slow path of SetPixelLine) */
static int e1x, e1y, e1h, mx, my, mh, cnt9F;

/* L9CCF: A (unsigned 8 bit) * m (signed 16 bit) >> 8, the 6502 shift-and-add loop */
static int MulS(int a, int m) {
    int mag = m, neg = 0, acc = 0, ad = a & 0xFF, c, lsb, k;
    if (m < 0) { mag = -m; neg = 1; }
    mag &= 0xFFFF;
    c = ad & 1;
    ad >>= 1;
    if (c) acc = mag;
    for (k = 1; k <= 7; k++) {
        lsb = acc & 1;
        acc >>= 1;
        c = ad & 1;
        ad = (ad >> 1) | (lsb << 7);
        if (c) acc = (acc + mag) & 0xFFFF;
    }
    acc >>= 1;
    if (neg) acc = (-acc) & 0xFFFF;
    return W16(acc);
}

static int SinV(int idx) {
    int q, y, v;
    idx &= 255;
    q = idx >> 6;
    y = idx & 63;
    if (q & 1) y ^= 0x3F;
    v = fSinHi[y] * 256 + fSinLo[y];
    if (q >= 2) v = -v;
    return v;
}

static int SinInterp(int idx, int w) { return (SinV(idx) * (8 - w) + SinV(idx + 1) * w) >> 8; }

/* ---- terrain generation: init_grid_map ($843C) ---- */
static int hm98, hm9C, hm9A, hm9B;
static int stk[256];

static int SetHigh(void) {
    int x, y;
    x = (hm9C + hm9A) & 0xFF;
    if ((heightmap[x] & 0x80) == 0) return 0;
    y = (x + hm9A) & 0xFF;
    if ((heightmap[y] & 0x80) == 0) return 0;
    x = (x + hm9B) & 0xFF;
    if (heightmap[x] & 0x80) return 1;
    return 0;
}
static void PushCell(void) { stk[hm98] = hm9C; hm98++; }
static void TryDirs(void) {
    hm9A = 0x01; hm9B = 0x10; if (SetHigh()) PushCell();
    hm9A = 0xFF; hm9B = 0xF0; if (SetHigh()) PushCell();
    hm9A = 0x10; hm9B = 0xFF; if (SetHigh()) PushCell();
    hm9A = 0xF0; hm9B = 0x01; if (SetHigh()) PushCell();
}

void InitGridMap(void) {
    int i, hm99, r, x, guard;
    for (i = 0; i <= 255; i++) heightmap[i] = (frand8() & 0x3F) | 0x80;
    hm98 = 1;
    hm9C = frand8();
    heightmap[hm9C] = frand8() & 0x3F;
    for (;;) {
        hm99 = hm98;
        TryDirs();
        if (hm98 == hm99) {
            hm98--;
            if (hm98 == 0) break;
            hm9C = stk[hm98];
        } else {
            guard = 0;
            do {
                r = frand8();
                if (r & 0x40) {
                    if (r & 0x80) { hm9A = 0xF0; hm9B = 0x01; } else { hm9A = 0x10; hm9B = 0xFF; }
                } else {
                    if (r & 0x80) { hm9A = 0xFF; hm9B = 0xF0; } else { hm9A = 0x01; hm9B = 0x10; }
                }
                guard++;
            } while (!SetHigh() && guard <= 500);
            x = (hm9C + hm9A) & 0xFF;
            heightmap[x] = frand8() & 0x3F;
            x = (x + hm9A) & 0xFF;
            heightmap[x] = frand8() & 0x3F;
            hm9C = x;
        }
    }
}

/* ---- camera and vertex grid: LAD6C, LA0DB, update_tables ---- */
static void BuildRamp(TFlight *f) {
    int a6, a4, a5, b6, x, t, c;
    a6 = ((f->pitchHi << 2) | (f->pitchLo >> 6)) & 0xFF;
    a6 = (6 - a6) & 0xFF;
    a4 = (f->rollHi >> 1) | (f->rollHi & 0x80);
    a5 = (f->rollLo >> 1) | ((f->rollHi & 1) << 7);
    ramp[16] = a6;
    b6 = 0;
    for (x = 0; x <= 14; x++) {
        t = b6 + a5;
        c = t >> 8;
        b6 = t & 0xFF;
        ramp[17 + x] = (ramp[16 + x] + a4 + c) & 0xFF;
    }
    b6 = 0;
    for (x = 15; x >= 0; x--) {
        t = b6 - a5;
        c = (t < 0) ? 1 : 0;
        b6 = t & 0xFF;
        ramp[x] = (ramp[x + 1] - a4 - c) & 0xFF;
    }
}

/* ---- demo flight: the autopilot of the Atari 800 port (made calmer: AP_DEAD / AP_PULSE below; -DAP_DEAD=1 -DAP_PULSE=1 gives the Atari's behaviour exactly, tools/ap/ap_check.py tests that)
   The Atari autopilot steers on every tick for any difference of the wall weights: in the C core it turned about 2.3 full turns a minute, mostly to the right, and reversed 120 times a minute.
   With the dead band 3 and a stick pulse every second tick it drifts about 17 degrees a minute to each side, with gentle tilts, and hits the ground less often (1 % of the seconds instead of 2.8 %).
   Original description: (flight800_far_pm_iron.asm, "autopilot"), once per flight tick (25 per second) ----
   Canyon following: the vertices of the fan in front of the ship (camera Z 1280 .. 10239, inside the view, terrain height >= AP_WALL = a wall) are counted left and right of the flight
   line (weight 2 within 1280 units of it, else 1; counted after every picture, like the Atari's autopilot reads the vertex tables of the last one) and it banks away from the side with
   more wall.  Altitude held low: climb below altHi $19, dive above $1E, climb (no dive) when the clearance m061D is below 20.  Speed: the phase table makes it run 250 ticks at the set
   thrust, 40 ticks with a '+' key every second tick (thrust +1 each, 30 -> 50), 250 ticks as set, 40 ticks with a '-' key every second tick, and so on.
   The index x of vflag / vh / vx / vz is used exactly as the Atari code does (vx[x], vz[x] are the camera coordinates of vertex x - 1). */
#define AP_WALL 110
#ifndef AP_DEAD
#define AP_DEAD 3                                   /* steer only when the wall weights differ by at least this much (the Atari: 1 = any difference) */
#endif
#ifndef AP_PULSE
#define AP_PULSE 2                                  /* press the stick on every AP_PULSE-th tick only (the Atari: 1 = every tick) */
#endif
static const u8 apSpeedMode[4] = { 0, 1, 0, 2 };         /* 0 fly, 1 '+' keys, 2 '-' keys */
static const u8 apLen[5] = { 250, 40, 250, 40, 0 };
static int apTick;
static int apPh, apT;                               /* phase (1 based once started), ticks left in it */
static u8 apL, apR;                                 /* wall weight left / right of the flight line (set after every picture) */

static void ApScan(void) {
    int x, l = 0, r = 0, zh, xh;
    for (x = F_NV - 1; x >= 0; x--) {
        if (vflag[x] & 0xE0) continue;                  /* outside the view or too near */
        zh = (vz[x] >> 8) & 0xFF;
        if (zh < 5 || zh >= 40) continue;
        if (vh[x] < AP_WALL) continue;
        xh = (vx[x] >> 8) & 0xFF;
        if (xh & 0x80) l += (xh >= 0xFB) ? 2 : 1;       /* left of the flight line */
        else r += (xh < 5) ? 2 : 1;
    }
    apL = (u8)l; apR = (u8)r;
}

static void ApSpeed(TFlight *f) {                     /* the speed phases of the Atari autopilot: '+' / '-' keys every second tick */
    int mode;
    if (apT == 0) {                                     /* next phase */
        int len = apLen[apPh];
        if (len == 0) { apPh = 0; len = apLen[0]; }
        apT = len; apPh++;
    }
    apT--;
    mode = apSpeedMode[apPh - 1];
    if (mode && (apT & 1) == 0) {                       /* a key every second tick */
        if (f->m065F == 0) { f->m065F = 0xFF; f->speedCfg = 0x1E; }   /* a throttle key cancels a landing */
        if (mode == 1) { if (f->speedCfg < 100) f->speedCfg++; }
        else if (f->speedCfg > 0) f->speedCfg--;
    }
}

int FAutopilotAtari(TFlight *f) {                        /* returns the stick (F_UP .. F_RIGHT, a set bit = not pressed); the '+' / '-' keys act on f->speedCfg directly */
    int porta = 0x0F;
    ApSpeed(f);
    apTick++;
    if (apL != apR && (apL > apR ? apL - apR : apR - apL) >= AP_DEAD && apTick % AP_PULSE == 0) porta &= (apL > apR) ? ~F_RIGHT : ~F_LEFT;   /* more wall on the left: bank right */
    if (f->altHi < 0x19) porta &= ~F_DOWN;              /* climb */
    if (f->altHi > 0x1E) porta &= ~F_UP;                /* dive */
    if (f->m061D < 20) { porta &= ~F_DOWN; porta |= F_UP; }   /* too near the ground: climb, no dive */
    return porta & 0x0F;
}
void FAutopilotState(int *ph, int *t, int *l, int *r) { *ph = apPh; *t = apT; *l = apL; *r = apR; }   /* tests */
#ifndef AP_LOOKC
#define AP_LOOKC 8                                   /* steps (half cells) the altitude looks ahead in canyon mode */
#endif
#ifndef AP_LOOKM
#define AP_LOOKM 12                                  /* ... and in mountain mode */
#endif
#ifndef AP_CLRC
#define AP_CLRC 60                                   /* altitude above the ground in canyon mode */
#endif
#ifndef AP_CLRM
#define AP_CLRM 68                                   /* ... and over a mountain */
#endif
#ifndef AP_MTN0
#define AP_MTN0 120                                  /* seconds between two mountain flights: AP_MTN0 .. AP_MTN0 + 20 */
#endif
#ifndef AP_BLOCK
#define AP_BLOCK 2                                   /* free run (half cells) below which no canyon is ahead */
#endif
#ifndef AP_PGAIN
#define AP_PGAIN 32                                  /* pitch units (1/256) wanted per unit of altitude error, 0 = hold the altitude band with full pitch bursts */
#endif
#ifndef AP_PMAX
#define AP_PMAX 5                                    /* largest wanted pitch */
#endif
#ifndef AP_PBAND
#define AP_PBAND 64                                  /* pitch dead band (1/256) */
#endif
#ifndef AP_BDIV
#define AP_BDIV 2                                    /* wanted bank (1/256) = heading error / AP_BDIV ... */
#endif
#ifndef AP_BMAX
#define AP_BMAX 500                                  /* ... at most this much (1.5 = about 13 degrees a second) */
#endif
#ifndef AP_RISE
#define AP_RISE 6                                    /* altitude target: units it may rise per tick ... */
#endif
#ifndef AP_FALL
#define AP_FALL 1                                    /* ... and sink per tick */
#endif
#ifndef AP_SIDE
#define AP_SIDE 0                                    /* lateral offset (1/4096 of a cell) of the side samples, 0 = off */
#endif
#ifndef AP_CLR0
#define AP_CLR0 22                                   /* altitude above the ground at the start of the flight (after the descent): low, so that the mountains fill the picture ... */
#endif
#ifndef AP_RAMP10
#define AP_RAMP10 10                                 /* ... and rises to AP_CLRC / AP_CLRM over 1 << AP_RAMP10 ticks (about 35 s) */
#endif
static int apAge;                                    /* flight ticks since the start */
#define AP_CELLS   6
static int apWant, apMode, apModeT, apMtnT, apRng, apTgt, apCnt, apHmax;
void FAutopilotReset(void) { apAge = 0; apPh = 0; apT = 0; apMode = 0; apModeT = 0; apMtnT = 25 * (AP_MTN0 - 10); apWant = 0; apTgt = 0; apCnt = 0; apHmax = 0; apRng = 12345; }

/* ---- the canyon autopilot (default FAutopilot): follows the canyons of the terrain and now and then flies over a mountain ----
   The terrain is a 16 x 16 maze of canyon cells (height < $80) and mountain cells ($80 .. $BF).  Rays are cast from the ship over the cells ahead (steps of half a cell, 6 cells): the
   heading with the longest free run through canyon cells is the one to follow (small turns preferred, the last choice is kept unless another is clearly better).  Stick: the bank
   follows the heading error (bank target proportional to it, left / right pulses), up / down hold the altitude: canyon mode flies 22 units above the highest ground of the next two
   cells, mountain mode 30 above the highest ground of the next four cells.  When no canyon is free ahead (free run < 1.5 cells), or every ~40 .. 60 s, the mode changes to "over the
   mountain": the heading is kept (or points at a mountain ahead) and the ship climbs over it, for 12 s.  The speed phases are the Atari's (ApSpeed). */
#define AP_STEPS   (AP_CELLS * 2)

static int ApRnd(void) { apRng = (apRng * 1103515245 + 12345) & 0x7FFFFFFF; return (apRng >> 16) & 0xFF; }
static int ApCellH(int x, int z) { return heightmap[((((z + 2048) >> 12) & 15) << 4) | (((x + 2048) >> 12) & 15)]; }   /* the nearest terrain lattice point (the terrain is interpolated between them) */
static int ApGround(int h) { return h + (h >> 4); }                  /* the ground height (altitude units) of a cell value, as GroundHeight() at a cell's centre */
static int ApAbs(int v) { return v < 0 ? -v : v; }

/* free run (half cell steps through canyon cells) along a heading; *peak = the highest cell value inside the free run, first `look` steps.  With AP_SIDE the samples to the left and right
   of the ray count too: only a ray that stays within about a cell of the canyon axis is free, so the ship keeps to the middle of the canyon. */
static int ApRay(int px, int pz, int head, int look, int *peak) {
    int idx = (head >> 6) & 255, sx = SinV(idx) >> 5, sz = SinV(idx + 64) >> 5;
    int x = px, z = pz, i, h, run = 0, mx = ApCellH(px, pz);
#if AP_SIDE
    int ox = sz * AP_SIDE / 2048, oz = -sx * AP_SIDE / 2048;
#endif
    for (i = 1; i <= AP_STEPS; i++) {
        x += sx; z += sz;
        h = ApCellH(x, z);
#if AP_SIDE
        if (ApCellH(x + ox, z + oz) >= 0x80 || ApCellH(x - ox, z - oz) >= 0x80) h = 0x80;
#endif
        if (h >= 0x80) break;
        run++;
        if (i <= look && h > mx) mx = h;
    }
    *peak = mx;
    return run;
}

int FAutopilot(TFlight *f) {
    int porta = 0x0F, px, pz, head, alt8, k, hd, run, peak, best = -1000, bestHead = 0, bestRun = 0, bestK = 0, score, e, bank, bt, tgt, hmax, h, i, c;
    ApSpeed(f);
    px = f->posXLo | f->posXHi << 8; pz = f->posZLo | f->posZHi << 8;
    head = f->headLo | f->headHi << 8;
    alt8 = (((f->altHi >= 0x40 ? 0x3F : f->altHi) << 2) | (f->altLo >> 6)) & 0xFF;
    if ((apCnt & 3) == 0) {                                            /* the rays are cast every 4th tick (6 times a second): they are the expensive part */
        /* the canyon to follow */
        for (k = -4; k <= 4; k++) {
            hd = (head + k * 0x400) & 0x3FFF;
            run = ApRay(px, pz, hd, 4, &peak);
            score = run * 3 - ApAbs(k) * 2;
            e = ((hd - apWant + 0x2000) & 0x3FFF) - 0x2000;
            if (ApAbs(e) < 0x200) score += 4;                              /* keep the last choice */
            if (score > best) { best = score; bestHead = hd; bestRun = run; bestK = k; }
        }
        if (apMode == 0) {
            apWant = bestHead;
            if (bestRun < AP_BLOCK) { apMode = 1; apModeT = 25 * 12; apWant = head; }          /* no canyon ahead: over the mountain */
            else if ((apMtnT -= 4) <= 0) {                                                   /* now and then: over a mountain */
                apMtnT = 25 * (AP_MTN0 + ApRnd() % 21);
                for (k = 0, c = 0; k <= 8; k++) {                                         /* a mountain 2 .. 5 cells ahead, the smallest turn first */
                    hd = (head + (((k + 1) >> 1) * ((k & 1) ? 1 : -1)) * 0x400) & 0x3FFF;
                    run = ApRay(px, pz, hd, 4, &peak);
                    if (run >= 4 && run <= 10) { apWant = hd; apMode = 1; apModeT = 25 * 12; c = 1; break; }
                }
                (void)c;
            }
        } else if ((apModeT -= 4) <= 0) apMode = 0;
    }
    /* altitude: above the highest ground of the next cells, along the heading and along the wanted heading (a turn must not lower the target) */
    if ((apCnt & 1) == 0) {
        ApRay(px, pz, head, apMode ? AP_LOOKM : AP_LOOKC, &peak);
        hmax = ApCellH(px, pz);
        if (peak > hmax) hmax = peak;
        if (apWant != head) {
            ApRay(px, pz, apWant, apMode ? AP_LOOKM : AP_LOOKC, &peak);
            if (peak > hmax) hmax = peak;
        }
        apHmax = hmax;
    }
    hmax = apHmax;
    apCnt++;
    {
        int clr = apMode ? AP_CLRM : AP_CLRC;
        if (apAge < (1 << AP_RAMP10)) clr = AP_CLR0 + (((clr - AP_CLR0) * apAge) >> AP_RAMP10);       /* low at the start, rising */
        apAge++;
        tgt = ApGround(hmax) + clr;
    }
    if (tgt > 245) tgt = 245;
    if (apTgt == 0) apTgt = tgt;                                       /* the target rises quickly and sinks slowly: no pumping of the nose */
    else if (tgt > apTgt) apTgt += (tgt - apTgt > AP_RISE) ? AP_RISE : tgt - apTgt;
    else apTgt -= (apTgt - tgt > AP_FALL) ? AP_FALL : apTgt - tgt;
    tgt = apTgt;
#if AP_PGAIN
    {                                                                  /* pitch proportional to the altitude error: smooth climbs and dives instead of full pitch bursts */
        int pitch = (short)(f->pitchLo | f->pitchHi << 8), pt = (tgt - alt8) * AP_PGAIN;
        if (pt > AP_PMAX * 256) pt = AP_PMAX * 256; else if (pt < -AP_PMAX * 256) pt = -AP_PMAX * 256;
        if (pitch < pt - AP_PBAND) porta &= ~F_DOWN;                   /* climb */
        else if (pitch > pt + AP_PBAND) porta &= ~F_UP;                /* dive */
    }
#else
    if (alt8 < tgt - 6) porta &= ~F_DOWN;                              /* climb */
    else if (alt8 > tgt + 8) porta &= ~F_UP;                           /* dive */
#endif
    if (f->m061D < 12) { porta &= ~F_DOWN; porta |= F_UP; }
    /* bank: proportional to the heading error */
    e = ((apWant - head + 0x2000) & 0x3FFF) - 0x2000;
    bank = (int)(short)(f->bankLo | f->bankHi << 8);
    bt = e / AP_BDIV;
    if (bt > AP_BMAX) bt = AP_BMAX; else if (bt < -AP_BMAX) bt = -AP_BMAX;
    if (bank < bt - 48) porta &= ~F_RIGHT;
    else if (bank > bt + 48) porta &= ~F_LEFT;
    (void)i; (void)h; (void)bestK;
    return porta & 0x0F;
}
void FAutopilotModeState(int *mode, int *want) { *mode = apMode; *want = apWant; }   /* tests */

/* ---- wind from the side: the picture always rolls a little and flutters.  FWind() is called once per flight tick after FlightTick(): it adds a slow random gust and a quick flutter
   to the displayed roll (rollLo / rollHi, which FlightTick sets from the bank at the start of every tick, so nothing accumulates and the heading is not touched). ---- */
#ifndef FW_GUST
#define FW_GUST 380                                 /* gust amplitude, 1/256 bank (1.0 = about 8 rows of tilt at the edges of the picture) */
#endif
#ifndef FW_FLUTTER
#define FW_FLUTTER 110                              /* flutter amplitude */
#endif
static unsigned wSeed = 0xACE1u;
static int wG, wGT, wGN, wF, wFT, wFN;
static int WRnd(void) { wSeed ^= wSeed << 7; wSeed &= 0xFFFF; wSeed ^= wSeed >> 9; wSeed ^= wSeed << 8; wSeed &= 0xFFFF; return (int)wSeed; }      /* xorshift, 16 bit: no multiplication */
void FWind(TFlight *f) {
    int roll;
    if (--wGN <= 0) { wGN = 20 + WRnd() % 40; wGT = (WRnd() % 2001 - 1000) * FW_GUST / 1000; }      /* a new gust every 0.8 .. 2.4 s */
    wG += (wGT - wG) / 6 + (wGT > wG) - (wGT < wG) / 1;
    if (--wFN <= 0) { wFN = 2 + WRnd() % 5; wFT = (WRnd() % 2001 - 1000) * FW_FLUTTER / 1000; }     /* a new flutter every 0.08 .. 0.28 s */
    wF += (wFT - wF) / 2;
    roll = (short)(f->rollLo | f->rollHi << 8) + wG + wF;
    if (roll > 5 * 256 - 1) roll = 5 * 256 - 1; else if (roll < -5 * 256) roll = -5 * 256;
    f->rollLo = (u8)roll; f->rollHi = (u8)(roll >> 8);
}

#if F_WIDE
static TFlight *fCur;                                       /* the flight state of the picture (pitch / roll for the exact horizon ramp) */
#endif
#ifdef UNDERWATER
int fCamS, fCamC;
#endif
#ifdef FLIGHT320
int fFarPhase;                                       /* the progress through the cell along both axes, weighted by the heading (continuous when turning), 0 .. 4095 (the fade of the far band) */
#endif
static void UpdateTables(TFlight *f) {
    int posX, posZ, alt16, frX, frZ, cellX, cellZ, c8B, c8C;
    int idx, w, s, c, quad, tbl, b7, xc, y, b, X, Z, r0, r1;
    int idxh, h, v, fl, e, va, vb, i;

    posX = f->posXLo + f->posXHi * 256;
    posZ = f->posZLo + f->posZHi * 256;
#if F_WIDE
    frX = posX & 0xFFF; cellX = (posX >> 12) & 0xFF;         /* 12 bit fraction of the cell */
    frZ = posZ & 0xFFF; cellZ = (posZ >> 12) & 0xFF;
#else
    frX = (posX >> 4) & 0xFF; cellX = (posX >> 12) & 0xFF;
    frZ = (posZ >> 4) & 0xFF; cellZ = (posZ >> 12) & 0xFF;
#endif
    if (f->altHi >= 0x40) {
        c8C = 0xFF; c8B = f->altLo;
    } else {
        alt16 = (f->altLo + f->altHi * 256) << 2;
        c8B = alt16 & 0xFF; c8C = (alt16 >> 8) & 0xFF;
    }

    idx = ((f->headHi << 2) | (f->headLo >> 6)) & 0xFF;
    w = (f->headLo >> 3) & 7;
    s = SinInterp(idx, w);
    c = SinInterp(idx + 64, w);
    quad = (f->headHi >> 4) & 3;

#if F_WIDE
    s = SinHead(f->headLo + f->headHi * 256) >> 1;           /* 14 bit heading, 16 bit table mirrored the right way: +-32640 = 16 times the game's +-2040 */
    c = SinHead(f->headLo + f->headHi * 256 + 0x1000) >> 1;
#ifdef FLIGHT320
    { int as = s < 0 ? -s : s, ac = c < 0 ? -c : c, pz = c >= 0 ? frZ : 4095 - frZ, px = s >= 0 ? frX : 4095 - frX;
      fFarPhase = (int)(((long)as * px + (long)ac * pz) / (as + ac + 1)); }
#endif
#ifdef UNDERWATER
    fCamS = s; fCamC = c;                                    /* for the particles of the underwater version (flight_st.c) */
#endif
    vx16[0] = (frZ * s - frX * c) >> 12;
    vz16[0] = -((frZ * c + frX * s) >> 12);
    vx[0] = W16(vx16[0] >> 4); vz[0] = W16(vz16[0] >> 4);
#else
    vx[0] = W16(MulS(frZ, s) - MulS(frX, c));
    vz[0] = W16(-(MulS(frZ, c) + MulS(frX, s)));
#endif

    switch (quad) {
    case 3: tbl = 0; break;
    case 2: tbl = F_NV; break;
    case 0: tbl = 2 * F_NV; break;
    default: tbl = 3 * F_NV; break;
    }
    b7 = (cellZ << 4) & 0xF0;
    xc = cellX & 0x0F;

#ifdef ST_WALK_ASM
    UpdateWalkAsm(s, c, fWalk + tbl, b7, xc, (c8C << 8) + c8B);        /* the 55 vertices in assembly (st_vtab.s) */
#ifdef BUNKERS
    {                                                               /* the map cell of every vertex (the assembly walk does not keep it): the same steps */
        int bb7 = b7, bxc = xc, yy;
        for (yy = 0; yy < F_NV; yy++) {
            int wb = fWalk[tbl + yy], q0, q1;
            if (wb & 0x80) { bb7 = (bb7 + 0xF0) & 0xFF; q0 = wb & 0x40; q1 = wb & 0x20; }
            else if (wb & 0x40) { bb7 = (bb7 + 0x10) & 0xFF; q0 = wb & 0x20; q1 = wb & 0x10; }
            else { q0 = wb & 0x20; q1 = wb & 0x10; }
            if (q0) bxc = (bxc - 1) & 0xFF; else if (q1) bxc = (bxc + 1) & 0xFF;
            vcell[yy] = bb7 | (bxc & 0x0F);
        }
    }
#endif
#else
    for (y = 0; y < F_NV; y++) {
        b = fWalk[tbl + y];
#if F_WIDE
        X = vx16[y]; Z = vz16[y];
#else
        X = vx[y]; Z = vz[y];
#endif
        if (b & 0x80) {
            X += s; Z -= c; b7 = (b7 + 0xF0) & 0xFF;
            r0 = b & 0x40; r1 = b & 0x20;
        } else if (b & 0x40) {
            X -= s; Z += c; b7 = (b7 + 0x10) & 0xFF;
            r0 = b & 0x20; r1 = b & 0x10;
        } else {
            r0 = b & 0x20; r1 = b & 0x10;
        }
        if (r0) {
            X -= c; Z -= s; xc = (xc - 1) & 0xFF;
        } else if (r1) {
            X += c; Z += s; xc = (xc + 1) & 0xFF;
        }
#if F_WIDE
        vx16[y + 1] = X; vz16[y + 1] = Z;
        vx[y + 1] = W16(X >> 4); vz[y + 1] = W16(Z >> 4);
#else
        vx[y + 1] = W16(X); vz[y + 1] = W16(Z);
#endif

        idxh = b7 | (xc & 0x0F);
        vcell[y] = idxh;
        h = heightmap[idxh];
        vh[y] = h;
        v = h * 256 - (c8C * 256 + c8B);
        vdy[y] = v >> 4;
#if F_WIDE
        vdy16[y] = v;
        Z = vz16[y + 1]; X = vx16[y + 1];
        if (Z < 6 * 16) fl = 0x80;
#else
        Z = vz[y + 1]; X = vx[y + 1];
        if (Z < 6) fl = 0x80;
#endif
        else {
            int lim = Z;                                     /* the visibility limit: Z, or with 160 columns 27/32 of Z = Z - Z/8 - Z/32 */
#if F_NARROW
            lim = Z - (Z >> 3) - ((Z >> 5) & fFlagLimMask);
#endif
            if (X < 0) { if (-X < lim) fl = 0; else fl = 0x40; }
            else { if (X < lim) fl = 0; else fl = 0x20; }
        }
        vflag[y] = fl;
    }

#endif

    for (e = 0; e <= 10; e += 2) {
        va = fEdges[e]; vb = fEdges[e + 1];
#if F_WIDE
        if (vflag[va] == 0x80 && vflag[vb] == 0) { vz[va + 1] = 6; vz16[va + 1] = 6 * 16; vflag[va] = 0; }
        else if (vflag[va] == 0 && vflag[vb] == 0x80) { vz[vb + 1] = 6; vz16[vb + 1] = 6 * 16; vflag[vb] = 0; }
#else
        if (vflag[va] == 0x80 && vflag[vb] == 0) { vz[va + 1] = 6; vflag[va] = 0; }
        else if (vflag[va] == 0 && vflag[vb] == 0x80) { vz[vb + 1] = 6; vflag[vb] = 0; }
#endif
    }
#if F_WIDE
    fCur = f;
    fPitchBase = (6 << 8) - (short)(f->pitchLo | f->pitchHi << 8) * 4;
    fRollS = (short)(f->rollLo | f->rollHi << 8);
#endif
    for (i = 0; i < F_NV; i++) vpdone[i] = 0;
#ifndef FLIGHT320
    BuildRamp(f);                                    /* (flight320 has its own horizon ramp: the exact one in F3Project) */
#endif
    ApScan();
}

/* ---- perspective: LA157 ---- */
static int ProjCoord(int num, int z, int neg) {
    int zz = z & 0xFFFF, n = 0, q, v, off;
    while (num >= zz) {
        zz = (zz << 1) & 0xFFFF;
        n++;
        if (n == 8) return neg ? -16384 : 16384;
    }
#ifdef FLIGHT_AMIGA
    q = divuw((unsigned int)(num << 8), (unsigned short)zz);        /* num < zz: the quotient is < 256 */
#else
    q = (num << 8) / zz;
#endif
    if (n == 0) off = (((q >> 1) + q + 1) >> 1) >> 1;
    else { v = q << n; off = (v + (v >> 1)) >> 2; }
    return neg ? (0x80 - off) : (0x80 + off);
}

#if F_WIDE
/* exact perspective: 96 * num / z in 8.8 (signed, sub pixel).  Two 16 bit divisions: the integer part and the 8 bit fraction.  |num| >= 128 z is the game's overflow (+-16384 pixels). */
static int Proj88(int num, int z) {
    int neg = 0, q1, fr;
    unsigned int n, d, t;
    if (num < 0) { num = -num; neg = 1; }
    if (z < 1) z = 1;
    if (num >= (z << 7)) return neg ? -(16384 << 8) : (16384 << 8);
    n = (unsigned int)num; d = (unsigned int)z;
    while (d > 32767) { d >>= 1; n >>= 1; }                 /* the divisor must fit 15 bits */
    t = DivU(n * 96, d);                                    /* n * 96 < 2^29, the quotient < 12400 */
    q1 = (int)(t & 0xFFFF);
    fr = (int)(DivU((t >> 16) << 8, d) & 0xFFFF);           /* remainder < d < 2^15: (r << 8) / d < 256 */
    q1 = (q1 << 8) + fr;
    return neg ? -q1 : q1;
}

#ifdef ST_WALK_ASM
#define ProjectVertex(i) ProjectVertexAsm(i)                    /* st_vtab.s */
#else
static void ProjectVertex(int i) {
    int X, Z, dy, xs, ys, r, xr;
    if (vpdone[i]) return;
    vpdone[i] = 1;
    X = vx16[i + 1]; Z = vz16[i + 1]; dy = vdy16[i];
    xs = Proj88(X, Z) + (128 << 8);
    ys = Proj88(dy, Z) + (128 << 8);
    xr = xs < 0 ? 0 : (xs > (255 << 8) ? (255 << 8) : xs);
    r = (6 << 8) - (short)(fCur->pitchLo | fCur->pitchHi << 8) * 4;      /* pitch: rows * 256 */
    r += ((xr - (128 << 8)) * (short)(fCur->rollLo | fCur->rollHi << 8)) >> 12;   /* roll: roll / 4096 rows per column, exact */
    vpx[i] = xs >> 8;
    vpy[i] = (ys + r) >> 8;
}
#endif
#else
static void ProjectVertex(int i) {
    int X, Z, dy, xs, ys, r;
    if (vpdone[i]) return;
    vpdone[i] = 1;
    X = vx[i + 1]; Z = vz[i + 1]; dy = vdy[i];
    xs = ProjCoord(iabs(X), Z, X < 0);
    ys = ProjCoord(iabs(dy), Z, dy < 0);
    if (xs < 0) r = ramp[0];
    else if (xs >= 256) r = ramp[31];
    else r = ramp[xs >> 3];
    if (r & 0x80) r -= 256;
    vpx[i] = xs;
    vpy[i] = ys + r;
}
#endif

/* ---- ridge buffer and pixel plotting ---- */
static void InitLineBuf(void) {
    int i;
    for (i = 0; i <= 255; i++) fLineBuf[i] = 0xFF;
    for (i = F_COL_LO; i <= 63; i++) fLineBuf[i] = F_FLOOR_E;
    for (i = 64; i <= 195; i++) fLineBuf[i] = F_FLOOR_M;
    for (i = 196; i < F_COL_HI; i++) fLineBuf[i] = F_FLOOR_E;
}

#ifdef UNDERWATER
extern short fColDisp[256];                               /* the water ripple of every pixel column: rows * stride in bytes (flight_st.c) */
#endif
static void PlotPixel(int x, int y) {
    if (x >= X_MIN && x <= X_MAX && y >= Y_MIN && y <= Y_MAX) {
#ifdef PLOT_PLANAR
        int p = x - X_MIN;
        u8 *d = fPlane + (Y_MAX - y) * F_ROWREP * FSTRIDE + FPAD + (p >> 2);
#ifdef FLIGHT_BANDS
        d += fClassOff;
#endif
#ifdef UNDERWATER
        d += fColDisp[x];
#endif
        u8 m = (u8)(0xC0 >> ((p & 3) * 2));
        { int k; for (k = 0; k < F_ROWREP; k++) d[k * FSTRIDE] |= m; }
#else
        scr[(Y_MAX - y) * VIEW_W + (x - X_MIN)] = 1;
#endif
    }
}

static void PlotCol(int x, int y) {
    PCOUNT(12);
    if (y <= fLineBuf[x]) return;
    PCOUNT(13);
    fLineBuf[x] = y;
    if (y >= F_YTOP) { fLineBuf[x] = 0xFF; y = F_YTOP; }
    PlotPixel(x, y);
}

#ifdef FLIGHT_ASM
/* tables and constants the assembly routine reads (flight_line.asm): column byte offset and pixel mask by pixel column, row address by
   ridge value (rebuilt for every picture: it holds the address of the frame being drawn), and a few constants */
struct FCfg { u8 colHi, yTop; unsigned short stride; u8 colLo, yClip; };
const struct FCfg fCfg = { F_COL_HI, F_YTOP, FSTRIDE, F_COL_LO, F_YCLIP };
u8 fColOff[256], fMask[256], fLbMap[256];           /* fLbMap[y] = the new ridge value of a plotted pixel: y, or $FF (the column is closed) when y >= yTop */
unsigned long fRowAddr[256];
int fUseAsm = 1;
extern void Line51CAsm(int X, int yi, int z84, int z86);
extern void DrawEdgeAsm(int x1, int y1, int h1, int x2, int y2, int h2);
void FInit(void) {
    int x, p;
    for (x = 0; x < 256; x++) { fColOff[x] = 0; fMask[x] = 0; }
    for (x = F_COL_LO; x < F_COL_HI; x++) { p = x - F_COL_LO; fColOff[x] = FPAD + (p >> 2); fMask[x] = (u8)(0xC0 >> ((p & 3) * 2)); }
    for (x = 0; x < 256; x++) fLbMap[x] = x < F_YTOP ? (u8)x : 0xFF;
}
#else
int fUseAsm = 0;
void FInit(void) {}
#endif

/* LB51C: the byte line routine */
#ifdef FLINE_STATS
unsigned long fLineSteps, fLineCulled, fLineScan;
#endif
static void Line51C(int X, int yi) {
    int a, t, c2, b6, cH, mid, guard = 0;
    PCOUNT(11);
    for (;;) {
        PCOUNT(15);
        guard++;
        if (guard > 4000) return;
        if (X >= F_COL_HI) return;
        z82 = X;
#ifdef FLINE_STATS
        fLineSteps++;
#endif
#ifdef FLINE_CULL
        {                                                  /* a piece of 4 or more columns whose pixels all lie below the ridge buffer: skip it (see docs) */
            int n = (ZX[yi] - X) & 0xFF;
            if (n >= FLINE_CULL) {
                int y0 = z84, y1 = ZY[yi], hi = y0 > y1 ? y0 : y1, lo = y0 < y1 ? y0 : y1, half = (n >> 1) + 3;
                if (lo >= half && hi + half <= 255) {
                    int bound = hi + half, c, e = X + n;
                    for (c = X; c < e; c++) {
#ifdef FLINE_STATS
                        fLineScan++;
#endif
                        if (fLineBuf[c] < bound) break;
                    }
                    if (c == e) {                          /* hidden: the state after the piece */
#ifdef FLINE_STATS
                        fLineCulled++;
#endif
                        z84 = ZY[yi];
                        z86 = ZH[yi];
                        X = ZX[yi];
                        yi--;
                        if (yi < 0) return;
                        continue;
                    }
                }
            }
        }
#endif
        a = (X - ZX[yi]) & 0xFF;
        if (a == 0xFE) {
            t = ZY[yi] + z84 + 1;
            PlotCol(X, (t >> 1) & 0xFF);
            X++;
            z84 = ZY[yi];
            PlotCol(X, ZY[yi]);
            yi--;
            if (yi < 0) return;
            X++;
            z86 = ZH[yi + 1];
        } else if (a == 0xFF) {
            z84 = ZY[yi];
            PlotCol(X, ZY[yi]);
            yi--;
            if (yi < 0) return;
            X++;
            z86 = ZH[yi + 1];
        } else {
            mid = ((X + ZX[yi]) >> 1) & 0xFF;
            ZX[yi + 1] = mid;
            t = z86 + ZH[yi] + 1;
            cH = t >> 8;
            ZH[yi + 1] = t & 0xFF;
            t = z84 + ZY[yi];
            c2 = t & 1;
            a = (t >> 1) & 0xFF;
            if (ZH[yi + 1] & 0x80) {
                if (cH == 0) {
                    b6 = (((mid - z82 - 1) & 0xFF) >> 1);
                    a = (a - b6 - (1 - c2)) & 0xFF;
                } else {
                    b6 = (((mid - z82) & 0xFF) >> 1);
                    t = a + b6 + c2;
                    a = (t > 255) ? 0xFF : t;
                }
            }
            ZY[yi + 1] = a;
            yi++;
        }
    }
}

/* set_pixel_line ($B418) */
void SetPixelLineC(void) {
    int a, mid, t, cH, b6, yi = 0, guard = 0;
    a = ZX[0];
    if (a < F_COL_LO + 1 || a < z82) return;
    if (a == z82) { PlotCol(ZX[0], ZY[0]); return; }
    for (;;) {
        guard++;
        if (guard > 4000) return;
        if (z82 >= F_COL_LO) {
#ifdef FLIGHT_ASM
            if (fUseAsm) { PROF(9, Line51CAsm(z82, yi, z84, z86)); return; }
#endif
            PROF(9, Line51C(z82, yi)); return;
        }
        mid = ((z82 + ZX[yi]) >> 1) & 0xFF;
        if (mid <= F_COL_LO) {
            z82 = mid;
            t = z86 + ZH[yi] + 1;
            cH = t >> 8;
            z86 = t & 0xFF;
            t = z84 + ZY[yi];
            a = (t >> 1) & 0xFF;
            if (z86 & 0x80) {
                b6 = (((ZX[yi] - z82) & 0xFF) >> 1);
                if (cH == 0) a = (a - b6) & 0xFF;
                else { t = a + b6; a = (t > 255) ? 0xFF : t; }
            }
            z84 = a;
        } else {
            ZX[yi + 1] = mid;
            t = z86 + ZH[yi] + 1;
            cH = t >> 8;
            ZH[yi + 1] = t & 0xFF;
            t = z84 + ZY[yi];
            a = (t >> 1) & 0xFF;
            if (ZH[yi + 1] & 0x80) {
                b6 = (((mid - z82) & 0xFF) >> 1);
                if (cH == 0) a = (a - b6) & 0xFF;
                else { t = a + b6; a = (t > 255) ? 0xFF : t; }
            }
            ZY[yi + 1] = a;
            yi++;
        }
    }
}

/* LB3A7: midpoint of end 1 (e1) and stack entry xi, with the fractal displacement */
static void Mid3A7(int xi) {
    int sum, d;
    PCOUNT(14);
    mx = (e1x + sx[xi] + 1) >> 1;
    my = (e1y + sy[xi] + 1) >> 1;
    sum = e1h + sh_[xi] + 1;
    mh = sum & 0xFF;
    if (mh & 0x80) {
        d = (mx - e1x) >> 1;
        if (sum > 255) my = W16(my + d);
        else my = W16(my - d);
    }
}

static int Clamp8(int y) { if (y < 0) return 0; if (y > 255) return 255; return y; }

/* cull_test of the 800XL port (draw.asm): 1 = no pixel of the edge (end 1 = e1x / e1y, end 2 = stack entry 0, x1 < x2) can be plotted, because the highest ridge
   value it can reach is not above the ridge buffer in any of its columns.  Exact (the picture does not change): the fractal displacement of a midpoint is
   at most half the width of the edge, so ytop = max(y1, y2) + width / 2 + 3; a pixel needs y > fLineBuf[column], so the edge is hidden if
   fLineBuf[c] >= ytop in every column c = max(x1, COL_LO) .. min(x2, COL_HI - 1).  A ytop above 255 or an edge outside the byte columns is left to the caller. */
static int CullTest(void) {
    int x1 = e1x, y1 = e1y, x2 = sx[0], y2 = sy[0];
    int c0, c1, ytop, c;
    if (x1 > 255) return 0;                          /* end 1 right of column 255 */
    c0 = (x1 < F_COL_LO) ? F_COL_LO : x1;            /* first column */
    if (x2 < 0) return 0;                            /* end 2 left of the view */
    c1 = (x2 >= F_COL_HI) ? F_COL_HI - 1 : x2;       /* last column */
    if (c1 < c0) return 0;
    ytop = (y1 > y2) ? y1 : y2;                      /* the higher end (signed) */
    if (ytop >= 256) return 0;
    ytop += ((x2 - x1) >> 1) + 3;
    if (ytop < 0) return 1;                          /* nothing is above the floor */
    if (ytop >= 256) return 0;
    if (fLineBuf[c0] < ytop || fLineBuf[c1] < ytop || fLineBuf[(c0 + c1) >> 1] < ytop) return 0;   /* the ends and the middle first */
    for (c = c0; c <= c1; c++) if (fLineBuf[c] < ytop) return 0;
    return 1;
}

/* LB24D: clip one projected edge, bisect it in 16 bits, hand the pieces to the byte routine */
static void DrawEdge(int x1, int y1, int h1, int x2, int y2, int h2) {
    int xi = 0, action, doSub, xw, b6, guard = 0;
    PCOUNT(10);
    e1x = x1; e1y = y1; e1h = h1;
    sx[0] = x2; sy[0] = y2; sh_[0] = h2;
    if (e1x >= sx[0]) return;
#ifndef FLIGHT_NOCULL
    if (CullTest()) return;
#endif
    cnt9F = 0x14;
    for (;;) {
        guard++;
        if (guard > 2000) return;
        if (e1x < 0) {
            cnt9F--;
            if (cnt9F < 0) return;
            Mid3A7(xi);
            if (mx < 0 || (mx < 256 && mx < 0x28)) {
                e1x = mx; e1y = my; e1h = mh;
            } else {
                xi++;
                sx[xi] = mx; sy[xi] = my; sh_[xi] = mh;
                if (xi >= 0x0F) return;
            }
        } else {
            if (e1x >= 256 || e1x >= 0xD8) return;
            for (;;) {
                doSub = 0;
                action = 0;                                   /* 0 draw, 1 skip, 2 subdivide */
                if ((sx[xi] >> 8) != 0) {
                    doSub = 1;
                } else {
                    xw = (sx[xi] - e1x) & 0xFF;
                    if (e1y < F_YCLIP) {
                        if (sy[xi] < F_YCLIP) action = 1;
                        else if (xw >= 0x14) {
                            b6 = xw >> 2;
                            if (e1y - b6 < 0) action = 2;
                        }
                    } else {
                        if (sy[xi] < F_YCLIP && xw >= 0x14) {
                            b6 = xw >> 2;
                            if (sy[xi] - b6 < 0) action = 2;
                        }
                    }
                    if (action == 2) doSub = 1;
                }
                if (doSub) {
                    cnt9F--;
                    if (cnt9F < 0) return;
                    Mid3A7(xi);
                    xi++;
                    sx[xi] = mx; sy[xi] = my; sh_[xi] = mh;
                    if (xi >= 0x0F) return;
                } else break;
            }
            if (action == 0) {
                z82 = e1x & 0xFF;
                z84 = Clamp8(e1y);
                z86 = e1h;
                ZX[0] = sx[xi] & 0xFF;
                ZY[0] = Clamp8(sy[xi]);
                ZH[0] = sh_[xi];
                SetPixelLineC();
            }
            if (xi == 0) return;
            e1x = sx[xi]; e1y = sy[xi]; e1h = sh_[xi];
            xi--;
        }
    }
}

/* one picture in two parts, so that the caller can overlap blitter work with the camera tables:
   RenderPrepare: ridge buffer, camera tables, (assembly build: the row address table of the plane in fPlane) - touches no picture memory
   RenderPlot:    the edges and lines, i.e. all pixels (waits for the caller's clear through fBeforePlot first) */
void RenderPrepare(TFlight *f) {
#ifdef PLOT_PLANAR
    PROF(0, InitLineBuf());                          /* the caller has cleared (or is clearing) the plane */
#ifdef FLIGHT_ASM
    { int y; for (y = F_YMIN; y <= F_YTOP; y++) fRowAddr[y] = (unsigned long)(fPlane + (F_YTOP - y) * F_ROWREP * FSTRIDE);
      for (y = F_YTOP + 1; y < 256; y++) fRowAddr[y] = fRowAddr[F_YTOP]; }       /* the assembly plot has no clamp: y >= yTop draws on the top row */
#endif
#else
    PROF(0, memset(scr, 0, VIEW_W * VIEW_H); InitLineBuf());
#endif
    PROF(1, UpdateTables(f));
}

#if defined(ST_WALK_ASM) && !defined(FLIGHT_BANDS) && !defined(FLIGHT_LEGACY)
extern void RenderPlotAsm(void);                          /* the edge loop in assembly (st_vtab.s) */
#endif
void RenderPlot(void) {
    int e, a, b;
    if (fBeforePlot) fBeforePlot();
#if defined(ST_WALK_ASM) && !defined(FLIGHT_BANDS) && !defined(FLIGHT_LEGACY)
    if (fUseAsm) { RenderPlotAsm(); return; }
#endif
    PROF(2,
    for (e = 0; e < 2 * F_NE; e += 2) {
        a = fEdges[e]; b = fEdges[e + 1];
        if ((vflag[a] & (0x80 | 0x20)) == 0 && (vflag[b] & (0x80 | 0x40)) == 0) {
            PROF(7, ProjectVertex(a); ProjectVertex(b));
            if (vpx[a] < vpx[b]) {
#ifdef FLIGHT_BANDS
                {                                                      /* the depth class of the edge: the cell row of its FARTHER end (5 rows, 2039 camera z units apart, class 0 = the nearest); an edge that runs
                                                                          into the screen belongs to the face between two rows, like the horizontal edge at its far end */
                    int za = vz[a + 1], zb = vz[b + 1], z = za > zb ? za : zb, k = z < 742 ? 0 : (z - 742) >> 11;
                    if (k > 4) k = 4;
                    fClassOff = k * (FSTRIDE * F_ROWS); fClassW = (unsigned short)fClassOff;
                }
#endif
#ifdef FLIGHT_ASM
                if (fUseAsm) { PROF(8, DrawEdgeAsm(vpx[a], vpy[a], vh[a], vpx[b], vpy[b], vh[b])); } else
#endif
                PROF(8, DrawEdge(vpx[a], vpy[a], vh[a], vpx[b], vpy[b], vh[b]));
            }
        }
    });
}

void RenderScene(TFlight *f) { RenderPrepare(f); RenderPlot(); }

#ifdef FLIGHT320
/* ============================================================================================================
   flight320 (Atari ST): the picture at 320 x F3_H single pixels (the fan of the flight: 55 vertices / 90 edges, or the game's 45 / 72 with -DFLIGHT_LEGACY), like the intro of Masterblazer (analysis: masterblazer_flight.md).
   The camera tables (UpdateTables) are the same; the projection, the ridge buffer and the fractal line work on word coordinates in screen pixels, one pixel per column.
   Rows are numbered like the ridge values: y = 0 is the bottom line, y = 199 the top line.  Three bitplanes of the interleaved low resolution screen are drawn: plane 0 = every ridge pixel (dripped down by the CPU = the ground),
   plane 1 = the ridge pixels of the LIT edges (dripped = the lit ground; the near ones, see F3_LITZ), plane 2 = the ridge line itself (not dripped).
   ============================================================================================================ */
#ifndef F3_SX
#define F3_SX 183                                   /* pixels per unit of x / z: the view of the game is +-0.875 (41 degrees) across its 168 columns, so 160 / 0.875 */
#endif
#ifndef F3_SY
#define F3_SY 183                                   /* ... and per unit of y / z (square pixels) */
#endif
#ifndef F3_H
#define F3_H 180                                    /* rows of the picture (the rest of the 200 lines is the bar of the scroller) */
#endif
#ifndef F3_Y0
#define F3_Y0 (F3_H * 56 / 100)                                   /* the row of the horizon at the middle column without pitch (the game: 62% of the window height) */
#endif
#ifndef F3_LITZ
#define F3_LITZ 5880                                /* an edge is drawn lit (planes 0, 1, 2) when both its ends are nearer than this (game units, z of the camera space; the rows of the fan are 2040 apart: 1800, 3840, 5880,
                                                       7920, 9960), farther edges unlit (planes 0, 2).  The 45 vertex fan of Masterblazer has the same split: its first 42 edges are the ones up to the row 5880 */
#endif
u8 f3LineBuf[320];                                  /* highest ridge row of every column, $FF = the column is closed (a ridge reached the top) */
u8 *f3Scr;                                          /* the screen: first line (the top line, y = F3_H - 1) of the 160 byte lines, interleaved planes: 4 words (planes 0 .. 3) per 16 pixels */
unsigned short f3RowOff[F3_H];                      /* the assembly line (flight_line320_st.asm): byte offset of the row y, (F3_H - 1 - y) * 160 */
unsigned short f3XTab[320][2];                      /* ... per column: the byte offset of its group of 16 pixels (x >> 4) * 8 and the pixel mask $8000 >> (x & 15) */
int f3UseAsm;                                       /* 1: the lines are drawn by F3SegAsm */
#ifdef F3_ASM
extern void F3SegAsm(int lit, int x1, int y1, int h1, int x2, int y2, int h2);
extern void F3ProjectAsm(int i);                    /* F3Project in assembly (flight_line320_st.asm, from the Amiga port), exact */
extern void F3EdgesAsm(const unsigned char *edges, int nEdges, int band1, int band2);     /* the edge loop of F3Plot320 (flight_line320_st.asm) */
#endif
static void F3SortEdges(void);
void F3Init(void) {
    int i;
    F3SortEdges();
    for (i = 0; i < F3_H; i++) f3RowOff[i] = (unsigned short)((F3_H - 1 - i) * 160);
    for (i = 0; i < 320; i++) { f3XTab[i][0] = (unsigned short)((i >> 4) * 8); f3XTab[i][1] = (unsigned short)(0x8000 >> (i & 15)); }
#ifdef F3_ASM
#ifdef BUNKERS
    f3UseAsm = 2;                                       /* the edge loop in C (it looks at the vertices for the bunkers) with the assembly lines */
#else
    f3UseAsm = 1;
#endif
#endif
    fFlagLimMask = 0;
}
static int f3Band;                                         /* the plane word (0, 1, 2) of the band of the edge being drawn */
int p3x[F_NV], p3y[F_NV];

/* The depth bands: the edges are sorted by ring around the camera cell (the game's order is only roughly near to far: 29 of the 90 edges come after an edge of a farther ring), rings 0 .. 2 = the
   near band (plane 0), ring 3 = the middle band (plane 1), ring 4 = the far band (plane 2).  Every band draws its ridge pixels into its own plane, the CPU ORs the planes down (F3DripRows), so the colour
   index of a pixel (8 + the bands it lies below: plane 3 is always 1) is the nearest band of the set.  Same code as the Amiga port (flight_core.c there). */
static u8 f3Edges[2 * F_NE];
int f3Band1, f3Band2;
static void F3SortEdges(void) {
    int pr[F_NV + 1], pc[F_NV + 1], rg[F_NE], R = 0, C = 0, y, e, r, n = 0;
    pr[0] = 0; pc[0] = 0;
    for (y = 0; y < F_NV; y++) {                        /* the grid position of every vertex: the first walk table (the others are rotations) */
        int b = fWalk[y], r0, r1;
        if (b & 0x80) { R++; r0 = b & 0x40; r1 = b & 0x20; }
        else if (b & 0x40) { R--; r0 = b & 0x20; r1 = b & 0x10; }
        else { r0 = b & 0x20; r1 = b & 0x10; }
        if (r0) C--; else if (r1) C++;
        pr[y + 1] = R; pc[y + 1] = C;
    }
    for (e = 0; e < F_NE; e++) {                        /* ring * 2 + 1 = 1, 3, 5 ... (the distance from the middle of the camera cell, in half cells) */
        int k, v, d, best = 0;
        for (k = 0; k < 2; k++) {
            v = fEdges[2 * e + k] + 1;
            d = 2 * pr[v] - 1; if (d < 0) d = -d; if (d > best) best = d;
            d = 2 * pc[v] - 1; if (d < 0) d = -d; if (d > best) best = d;
        }
        rg[e] = best;
    }
    f3Band1 = f3Band2 = 0;
    for (r = 1; r <= 2 * F_NV + 1; r += 2)             /* a stable sort by ring */
        for (e = 0; e < F_NE; e++)
            if (rg[e] == r) {
                f3Edges[2 * n] = fEdges[2 * e]; f3Edges[2 * n + 1] = fEdges[2 * e + 1]; n++;
                if (r <= 5) f3Band1 = n;                /* rings 0.5 .. 2.5 */
                if (r <= 7) f3Band2 = n;                /* ... 3.5 */
            }
}

/* the contour points: when a ridge pixel is raised the pixel it covers is remembered (x, y); the points of the near and middle band are drawn into the finished picture after the drip
   in the sky colour (F3DrawContoursAsm in st_f320.s).  The far band records into ptTrash.  ptReal = the end of the points to draw. */
unsigned short ptList[2 * 2048], ptTrash[2 * 2048];
unsigned short *ptPtr, *ptReal;

static int Proj88s(int num, int z, int scale) {        /* scale * num / z in 8.8 (signed, sub pixel): as Proj88 with another scale */
    int neg = 0, q1, fr;
    unsigned int n, d, t;
    if (num < 0) { num = -num; neg = 1; }
    if (z < 1) z = 1;
    if (num >= (z << 7)) return neg ? -(16384 << 8) : (16384 << 8);
    n = (unsigned int)num; d = (unsigned int)z;
    while (d > 32767) { d >>= 1; n >>= 1; }
    t = DivU(n * scale, d);
    q1 = (int)(t & 0xFFFF);
    fr = (int)(DivU((t >> 16) << 8, d) & 0xFFFF);
    q1 = (q1 << 8) + fr;
    return neg ? -q1 : q1;
}

int f3PitchR;                                           /* the pitch term of the horizon: rows * 256 (F3ProjectAsm) */
short f3RollS;                                          /* the roll of the picture (F3ProjectAsm) */
static inline int Mul16(int a, int b) { int r = a; __asm__("muls.w %1,%0" : "+d"(r) : "d"(b) : "cc"); return r; }       /* 16 x 16 -> 32 bit */

static void F3Project(int i) {
    int xs, ys, xr, r;
    if (vpdone[i]) return;
    vpdone[i] = 1;
    xs = Proj88s(vx16[i + 1], vz16[i + 1], F3_SX) + (160 << 8);
    ys = Proj88s(vdy16[i], vz16[i + 1], F3_SY) + (F3_Y0 << 8);
    xr = xs < 0 ? 0 : (xs > (319 << 8) ? (319 << 8) : xs);
    r = f3PitchR + (Mul16((xr - (160 << 8)) >> 1, (short)(fCur->rollLo | fCur->rollHi << 8)) >> 11);   /* roll: roll / 4096 rows per column, clamped at the edges of the picture */
    xs >>= 8; ys = (ys + r) >> 8;
    if (xs < -16000) xs = -16000; else if (xs > 16000) xs = 16000;                      /* the word routines add two coordinates: keep them in a word */
    if (ys < -16000) ys = -16000; else if (ys > 16000) ys = 16000;
    p3x[i] = xs;
    p3y[i] = ys;
}

static inline void F3Plot(int x, int y) {                /* PlotCol + PlotPixel: only above the ridge buffer; y >= 200 closes the column */
    int lb;
    if ((unsigned)x >= 320) return;
    lb = f3LineBuf[x];
    PCOUNT(11);
    if (lb == 0xFF || y <= lb) return;
    PCOUNT(12);
    if (y >= F3_H) { f3LineBuf[x] = 0xFF; y = F3_H - 1; } else f3LineBuf[x] = (u8)y;
    {
        unsigned short *w = (unsigned short *)(f3Scr + (F3_H - 1 - y) * 160 + (x >> 4) * 8);
        unsigned short m = (unsigned short)(0x8000 >> (x & 15));
        w[f3Band] |= m;                             /* the plane of the band of the edge (dripped down later) */
    }
}

/* the game's midpoint rule on word coordinates (Line51C / Mid3A7): hash h = h1 + h2 + 1 (8 bit, carry c); bit 7 set: the midpoint moves by (mid - x1) / 2, up when the sum had a carry;
   a piece (x1, x2) draws the columns x1 .. x2 - 1: a column gets the y of its right end, a piece of 2 columns the rounded up middle first */
static void F3Seg(int x1, int y1, int h1, int x2, int y2, int h2) {
    int w = x2 - x1, mid, t, hs, mh, a, c2, ym, b6;
    if (w < 1 || x2 <= 0 || x1 >= 320) return;
    PCOUNT(10);
    if (w == 1) { F3Plot(x1, y2); return; }
    if (w == 2) { F3Plot(x1, (y1 + y2 + 1) >> 1); F3Plot(x1 + 1, y2); return; }
    if (w >= 4) {                                      /* a piece whose pixels can not be above the ridge buffer in any column is skipped (exact: the displacements add up to less than w / 2 + 3) */
        int top = (y1 > y2 ? y1 : y2) + (w >> 1) + 3;
        if (top <= 255) {
            int c = x1 < 0 ? 0 : x1, e = x2 > 320 ? 320 : x2;
            while (c < e && f3LineBuf[c] >= top) { c++; PCOUNT(13); }
            if (c >= e) { PCOUNT(14); return; }
        }
    }
    mid = (x1 + x2) >> 1;
    hs = h1 + h2 + 1; mh = hs & 0xFF;
    t = y1 + y2; a = t >> 1; c2 = t & 1;
    ym = a;
    if (mh & 0x80) {
        if ((hs >> 8) == 0) { b6 = (mid - x1 - 1) >> 1; ym = a - b6 - (1 - c2); }
        else { b6 = (mid - x1) >> 1; ym = a + b6 + c2; }
    }
    F3Seg(x1, y1, h1, mid, ym, mh);
    F3Seg(mid, ym, mh, x2, y2, h2);
}

void F3Prepare(TFlight *f) {                              /* the camera tables; the ridge buffer is cleared by the caller (the blitter) */
    UpdateTables(f);
    f3RollS = (short)(f->rollLo | f->rollHi << 8);
    f3PitchR = -(((short)(f->pitchLo | f->pitchHi << 8) * 4 * F3_SY) / 96);              /* pitch: rows * 256 (the game 4 rows * 256 / 256 per unit, scaled to this view) */
}

#ifdef BUNKERS
/* ---- BUNK=1: the bunkers of the original (jaggie.asm: nodetype $64 on the tall mountain cells, drawn by LA8AB / LAC4F).  A cell with bit 7 set in the heightmap gets a bunker with the probability BUNK_DENSITY / 256
   (the game: RANDOM < $0617); the vertex of that cell is drawn when it is not hidden by a nearer ridge (its row is at least the ridge buffer of its column, the game tests the same).  Close ones (depth < 3328, the
   game: depth high byte < 13) are the 32 x 12 dome of the game ($A783), scaled by the distance with its top at the vertex and centred on it.  The vertices are recorded (bunkN, bunkX / Y / S); flight320_st.c draws them after the drip (HousesDraw). ---- */
#ifndef BUNK_PCT
#define BUNK_PCT 400                                       /* the size of the houses in percent of the game's scale times 183 / 96 for this view (the game's dome is only drawn closer than depth 3328, where the vertices are below the picture, so the game's size would show none) */
#endif
#ifndef BUNK_VALLEY
#define BUNK_VALLEY 1                                     /* 1: the bunkers stand in the valleys (cells without bit 7: the low resolved ground), 0: on the mountains like the game (bit 7 set) */
#endif
#if BUNK_VALLEY
#define BUNK_CELL(h) (((h) & 0x80) == 0)
#else
#define BUNK_CELL(h) (((h) & 0x80) != 0)
#endif
#ifndef BUNK_DENSITY
#ifdef LASER
#define BUNK_DENSITY 80                                    /* the laser build: more houses (they are the guns), 31 % of the valley cells */
#else
#define BUNK_DENSITY 32
#endif
#endif
static u8 bunkerCell[256];
static signed char bunkerOff[256];                      /* the sideways offset of the house from its vertex, in source pixels (-16 .. 16, one house width = 16), fixed per cell */
volatile unsigned long bkN, bkCells;                    /* bunker pixels of the last picture, bunker cells of the map (read through the debugger) */
void F3InitBunkers(void) {                               /* after InitGridMap */
    int i;
    bkCells = 0;
    for (i = 0; i < 256; i++) { bunkerCell[i] = (u8)(BUNK_CELL(heightmap[i]) && frand8() < BUNK_DENSITY); bkCells += bunkerCell[i]; bunkerOff[i] = (signed char)(frand8() % 33 - 16); }
}
#define BUNK_MAX 12
unsigned char bunkLb[BUNK_MAX][32];                       /* the ridge buffer of the columns x - 16 .. x + 15 at the moment the vertex was looked at (the nearer edges only): the per pixel hidden test of the game's set_pixel */
int bunkN, bunkX[BUNK_MAX], bunkY[BUNK_MAX], bunkS[BUNK_MAX];     /* the bunkers of this picture: the apex (column, ridge row) and the scale s8 (source pixels per screen pixel, 8.8); flight320_main.c blits them as bobs */
static unsigned char bkSeen[F_NV];                       /* the vertex has been looked at in this picture */
static void F3BunkerAt(int i, int far) {      /* far: the edge that uses it first is in the far band (the ring that fades in from the sky: a bunker can not fade, so there is none) */                          /* a vertex that was just projected, before the edge that uses it is drawn: the ridge buffer holds the nearer edges (what the game tests in LA690) */
    int x, y, z, s8, c;
    if (bkSeen[i]) return;
    bkSeen[i] = 1;
    c = vcell[i] & 255;
    if (far || !bunkerCell[c]) return;
    x = p3x[i]; y = p3y[i];
    if ((unsigned)x >= 320 || y < 0 || y >= F3_H) return;
    { int lb = f3LineBuf[x]; if (lb == 0xFF || y < lb) return; }
    z = vz16[i + 1] >> 4;                                 /* the depth in the game's units (the 16 bit tables are 16 times as big) */
    if (z < 200 || bunkN >= BUNK_MAX) return;
    s8 = (int)(((long)z * 135 * 100 / BUNK_PCT) >> 8);                      /* the game's scale, times 183 / 96 for this view */
    if (s8 < 128) s8 = 128;
    x += (bunkerOff[c] * 256) / s8;                        /* the house stands a little left or right of the vertex (the vertex was tested above) */
    if (x < -16 || x > 335) return;
    {                                                     /* only the columns the dome covers (half its width + a margin: the size of the bob is rounded to one of 10), the others stay 0 */
        int w2 = 2048 / s8 + 4, k, k0, k1;
        unsigned char *d = bunkLb[bunkN];
        if (w2 > 16) w2 = 16;
        k0 = 16 - w2; k1 = 16 + w2;
        for (k = 0; k < 32; k++) d[k] = 0;
        for (k = k0; k < k1; k++) { int c = x - 16 + k; d[k] = (unsigned char)((unsigned)c < 320 ? f3LineBuf[c] : 0); }
    }
    bunkX[bunkN] = x; bunkY[bunkN] = y; bunkS[bunkN] = s8; bunkN++;
}
static void F3BunkersDone(void) { bkN = (unsigned long)bunkN; }
#endif

void F3Plot320(void) {                                    /* all edges */
    int e, a, b;
#ifdef F3_ASM
    if (f3UseAsm == 1) { F3EdgesAsm(f3Edges, F_NE, f3Band1, f3Band2); return; }     /* (2 = the edge loop in C with the assembly lines: the check build) */     /* (it sets ptPtr / ptReal itself) */
#endif
    ptPtr = ptList; ptReal = 0;
#ifdef BUNKERS
    bunkN = 0; memset(bkSeen, 0, sizeof bkSeen);
#endif
    for (e = 0; e < 2 * F_NE; e += 2) {
        a = f3Edges[e]; b = f3Edges[e + 1];
        if ((vflag[a] & (0x80 | 0x20)) == 0 && (vflag[b] & (0x80 | 0x40)) == 0) {
            PROF(5, F3Project(a); F3Project(b));
#ifdef BUNKERS
            F3BunkerAt(a, (e >> 1) >= f3Band2); F3BunkerAt(b, (e >> 1) >= f3Band2);
#endif
            if (p3x[a] < p3x[b]) {
                f3Band = (e >> 1) < f3Band1 ? 0 : (e >> 1) < f3Band2 ? 1 : 2;       /* the band of the edge = its plane */
                if (f3Band == 2 && !ptReal) { ptReal = ptPtr; ptPtr = ptTrash; }
#ifdef F3_ASM
                if (f3UseAsm) { PROF(6, F3SegAsm(2 * f3Band, p3x[a], p3y[a], vh[a], p3x[b], p3y[b], vh[b])); } else
#endif
                PROF(6, F3Seg(p3x[a], p3y[a], vh[a], p3x[b], p3y[b], vh[b]));
            }
        }
    }
    if (!ptReal) ptReal = ptPtr;
#ifdef BUNKERS
    F3BunkersDone();
#endif
}
#endif
