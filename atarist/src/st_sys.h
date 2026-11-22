/* st_sys.h - system takeover and small helpers shared by the Atari ST programs (1040ST, 68000, no libc, supervisor mode) */
#ifndef ST_SYS_H
#define ST_SYS_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

#define ST_UP    1
#define ST_DOWN  2
#define ST_LEFT  4
#define ST_RIGHT 8
#define ST_FIRE  0x80

#define ST_LINE_BYTES 160        /* low resolution: 20 groups of 4 words per line */

extern volatile u16 stFrame;     /* vertical blanks since StInit */
extern volatile int stQuit;      /* Esc or Space pressed */
extern void (*stVblHook)(void);  /* called at the end of every vertical blank (interrupt context); may be 0 */

/* Takes the machine over: low resolution, palette (16 words, 0RGB, 3 bits per channel), our own vertical blank handler, IKBD polled
   (joystick events on, mouse off, TOS keyboard interrupt masked).  The first screen shown is `first` (256 byte aligned). */
void StInit(const u16 *palette, u8 *first);
void StExit(void);                          /* gives everything back */
u8   StInput(void);                         /* joystick (IKBD) or cursor keys: ST_UP | ST_DOWN | ST_LEFT | ST_RIGHT | ST_FIRE */
u8   StTakeNumKey(void);                    /* number key pressed since the last call: 1 .. 9, 10 for the 0 key, else 0 */
void StPresent(u8 *screen);                 /* show `screen` from the next vertical blank on; returns at once */
int  StMachine(void);                      /* the high word of the _MCH cookie: 0 = ST, 1 = STE (also the Mega STE), 2 = TT, 3 = Falcon (no jar = ST) */
int  StIsSTE(void);                          /* 1 = an STE or better (4 bit per channel palette registers), 0 = ST */
void StSetPalette(const u16 *palette);       /* 16 words 0RGB, at once (fade in from a black first palette) */
void StWaitPresented(void);                 /* wait until the screen given to StPresent is on display */
u8  *StShown(void);                         /* the screen on display */
u8  *StAlign256(void *p);

/* YM2149 music: a .ymr stream (tools/ymtool.py) played at 50 Hz by MFP timer A, independent of the screen refresh rate.  StExit stops it. */
void StYmStart(const u8 *tune);
void StYmStop(void);
extern u8 stYmRegs[16];

/* SNDH music (src/st_sndh.c): the tune's own replay code, called at its rate from MFP timer C (200 Hz).  subtune 0 = default.  StExit stops it. */
int  StSndhStart(const u8 *sndh, int subtune);
void StSndhStop(void);
u32  StSndhPlays(void);                                    /* PLAY calls so far (diagnostics) */
u32  StSndhTicks(void);                                    /* timer C interrupts so far */
int  StSndhInfo(const u8 *sndh, int *rate, int *nsub);     /* 0 if not an SNDH; else the default sub tune */          /* what was last written to the YM registers */

/* a small 4 digit number (3 x 5 font, every pixel 2 x 2 screen pixels, so 8 x 10) at pixel x0, y0 in a low resolution screen: plane 1 set, plane 0 clear */
void StDrawNumber(u8 *screen, int value, int x0, int y0);
u8 *StDisp(void);                                   /* the screen on display in this frame */
u8 *StReady(void);                                  /* the screen waiting for the next vertical blank (0 = none) */
void StDrawNumberCol(u8 *screen, int value, int x0, int y0, int col, int bg);   /* the same with the colour indexes 0 .. 3 (planes 1, 0) of the digits and of their background (StDrawNumber: 2 and 0); bg = -1: no background, only the digit pixels are set */

#endif
