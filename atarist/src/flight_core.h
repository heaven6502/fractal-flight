/* flight_core.h - flight model and terrain renderer of Behind Jaggi Lines! / Rescue on Fractalus (Atari 8 bit), C translation of
   Atari\rof\jaggie_flight.pbi and Atari\rof\jaggie_3d.pb (which are checked against the game).  Portable C, builds on the host for tests. */
#ifndef FLIGHT_CORE_H
#define FLIGHT_CORE_H

typedef unsigned char u8;

/* the game's flight variables, one byte each, 16 bit values as lo / hi pairs; same order as jaggie_flight.STATE_ADDRS */
typedef struct {
    u8 turn;        /* $2886 turn_offset */
    u8 speedCfg;    /* $2887 thrust value */
    u8 pitchAcc;    /* $2888 */
    u8 rollLo, rollHi;      /* $2889 displayed roll */
    u8 bankLo, bankHi;      /* $288B bank, 8.8 signed */
    u8 pitchLo, pitchHi;    /* $288D pitch, 8.8 signed */
    u8 m2884;
    u8 velXLo, velXHi, velZLo, velZHi;   /* $2891 */
    u8 climbLo, climbHi;    /* $2895 */
    u8 speedLo, speedHi;    /* $2897 */
    u8 headLo, headHi;      /* $2899 heading, a full turn = $4000 */
    u8 posXLo, posXHi, posZLo, posZHi;   /* $289B */
    u8 altLo, altHi;        /* $289F */
    u8 markRoll, markPitch; /* $2878 / $287A HUD markers */
    u8 f0, f1, f2, f3;      /* $28F0 */
    u8 m0626, m28A5, m061C, m065F, m28A4, m293D, m283F, m0625, m061D;
} TFlight;

#define F_UP    1
#define F_DOWN  2
#define F_LEFT  4
#define F_RIGHT 8      /* joystick bits as read from PORTA: a set bit = not pressed */

/* ---- geometry.  Default = the latest iron port (flight800_far_pm_iron.asm): FAR (55 vertices, 90 edges: the view reaches one cell further)
   and D80 (80 bitmap rows of ANTIC mode D, the original y scale), 40 byte columns (NARROW, pixel columns 48 .. 207) = 160 pixels wide.
   -DFLIGHT_WIDE   42 byte columns (columns 44 .. 211), otherwise the same
   -DFLIGHT_LEGACY the original game's view: 45 vertices, 72 edges, 48 rows, 42 columns (the PureBasic port, checked against the game)
   Rows are numbered from the top: row 0 = ridge value F_YTOP (closes the column), row F_ROWS-1 = F_YMIN. */
#ifdef FLIGHT_LEGACY
#define F_NV      45
#define F_NE      72
#define F_ROWS    48
#define F_YTOP    0x97
#define F_YMIN    0x68
#define F_YCLIP   0x6C          /* ridge values below this are not shown: the 5 rows F_BAR0 .. are the cockpit bar */
#define F_FLOOR_M 0x6B          /* start value of the ridge buffer in the middle columns / at the edge columns */
#define F_FLOOR_E 0x67
#define F_BAR0    43
#define F_COL_LO  44
#define F_COL_HI  212
#define F_NARROW  0
#else
#define F_NV      55
#define F_NE      90
#define F_ROWS    80
#define F_YTOP    0xA7
#define F_YMIN    0x58
#define F_YCLIP   0x5C
#define F_FLOOR_M 0x5B
#define F_FLOOR_E 0x57
#define F_BAR0    76
#ifdef FLIGHT_WIDE
#define F_COL_LO  44
#define F_COL_HI  212
#define F_NARROW  0
#else
#define F_COL_LO  48
#define F_COL_HI  208
#define F_NARROW  1             /* the left / right visibility limit is 27/32 of Z (81 columns instead of 96) */
#endif
#endif
#define VIEW_W (F_COL_HI - F_COL_LO)
#define VIEW_H F_ROWS

/* constant tables (flight_data.c, generated) */
extern const unsigned char fSinHi[65], fSinLo[65], fWalkLegacy[180], fEdgesLegacy[144], fWalkFar[220], fEdgesFar[180];
#ifdef FLIGHT_LEGACY
#define fWalk  fWalkLegacy
#define fEdges fEdgesLegacy
#else
#define fWalk  fWalkFar
#define fEdges fEdgesFar
#endif

extern u8 heightmap[256];            /* $0900 16 x 16 terrain, bit 7 = high mountain / unresolved */
extern u8 scr[VIEW_W * VIEW_H];      /* line pixels of the picture: 1 = ridge line, row 0 = top */
extern u8 fLineBuf[256];             /* the ridge buffer ($2608): highest ridge value plotted per pixel column, $FF = closed */
extern int fRndState, fUseLcg;       /* the thrust jitter: fUseLcg = same generator as the Python model (tests) */


/* Planar plotting (PLOT_PLANAR, the Amiga build): ridge pixels go straight into a bitplane of the final picture instead of scr[].
   Every picture pixel is 2 bits wide and 3 rows tall: a screen row is FSTRIDE bytes = FPAD pad bytes, FINNER bytes of picture, FPAD pad bytes.
   The caller clears the plane before RenderScene (fBeforePlot is called after the camera tables and before the first pixel is
   plotted: the place to wait for a blitter clear). */
#define FINNER  (VIEW_W / 4)     /* bytes of picture per screen row */
#if F_NARROW
#define FPAD    0                /* 160 pixels = 320 screen pixels = exactly one standard lowres row */
#else
#define FPAD    2                /* 2 pad bytes (one word) at each side: the screen row is a whole number of words */
#endif
#define FSTRIDE (FINNER + 2 * FPAD)
#ifndef F_ROWREP
#define F_ROWREP 3               /* screen rows per picture row (Amiga 3, Atari ST 1: the picture is doubled when it is copied to the screen) */
#endif
#define FROWS   (F_ROWS * F_ROWREP)
extern u8 *fPlane;                   /* plane 0 of the frame being drawn, row 0 first */
extern void (*fBeforePlot)(void);

/* FLIGHT_ASM (Amiga build): the byte line routine Line51C and its pixel plot are in 68000 assembly (flight_line.asm).  fUseAsm selects the
   C version for the self-test (flight_selftest.c); FInit must be called once. */
extern int fUseAsm;
void FInit(void);

void NewFlight(TFlight *f);
void StartFlight(TFlight *f);
int  FlightTick(TFlight *f, int porta);
int  FAutopilot(TFlight *f);          /* the canyon autopilot (default) */
int  FAutopilotAtari(TFlight *f);     /* the Atari 800 port's autopilot (flight800_far_pm_iron.asm), tests */
void FAutopilotModeState(int *mode, int *want);
void FWind(TFlight *f);               /* wind from the side: call once per flight tick after FlightTick() (only the displayed roll changes) */
#ifndef FLIGHT_THRUST
#define FLIGHT_THRUST 50                  /* the thrust the ports start with (the game's own start value is 30); the autopilot's speed phases go up to +20 from it */
#endif          /* demo flight (Atari 800 port): the stick for this flight tick; call once per tick while the player has not touched anything */
void FAutopilotReset(void);
void FAutopilotState(int *ph, int *t, int *l, int *r);
int  KeyLand(TFlight *f);
int  KeyTakeoff(TFlight *f);
int  GroundCollision(TFlight *f);
void FSeed(unsigned int seed);
void InitGridMap(void);              /* random terrain, the game's algorithm */
void RenderPrepare(TFlight *f);      /* the first part of RenderScene: tables, no picture memory touched */
void RenderPlot(void);               /* the second part: all pixels */
void RenderScene(TFlight *f);        /* one picture into scr[] (line pixels only; sky / ground are filled by the display code) */

#endif
