/* st_sys.c - see st_sys.h.  Runs in supervisor mode (st_crt.s). */
#include "st_sys.h"

#define REG8(a)  (*(volatile u8 *)(a))
#define REG16(a) (*(volatile u16 *)(a))

volatile u16 stFrame;
volatile int stQuit;
void (*stVblHook)(void);

static volatile u8 joyBits;                /* IKBD joystick: bit 0 up, 1 down, 2 left, 3 right, 7 fire */
static volatile u8 keyBits;
static volatile u8 numKey;                /* last number key (1 .. 9, 0 = 10) pressed, taken by StTakeNumKey */                /* cursor keys, same bits */
static u8 *volatile readyScr;              /* finished screen waiting for the next vertical blank */
static u8 *volatile shownScr;
static u8 *volatile dispScr;               /* the screen the hardware displays in this frame (the one shown before the last flip: the base register is loaded at the start of the frame) */

static u16 savedPal[16], savedRez;
static u8 *savedBase;
static u8 savedIerb, savedImrb;
static void *savedVbl;
static u8 savedIera, savedImra, savedTacr;
static void *savedTimerA;

/* ---- IKBD: joystick events, keyboard scan codes ---- */
static void AciaWrite(u8 b) {
	u16 n = 0;
	while (!(REG8(0xFFFC00) & 2) && ++n) ;          /* TDRE */
	REG8(0xFFFC02) = b;
}

static void KbdByte(u8 b) {
	static u8 pk[8], idx, need, hdr;
	if (need) {
		pk[idx++] = b;
		if (idx == need) {
			if (hdr == 0xFE || hdr == 0xFF) joyBits = pk[0] & 0x8F;
			else if (hdr == 0xFD) joyBits = (pk[0] | pk[1]) & 0x8F;
			need = 0;
		}
		return;
	}
	if (b >= 0xF8 && b <= 0xFB) { hdr = b; need = 2; idx = 0; }       /* mouse */
	else if (b == 0xFC) { hdr = b; need = 6; idx = 0; }               /* clock */
	else if (b == 0xFD) { hdr = b; need = 2; idx = 0; }               /* both joysticks */
	else if (b == 0xFE || b == 0xFF) { hdr = b; need = 1; idx = 0; }  /* joystick 0 / 1 */
	else if (b >= 0xF6) { hdr = b; need = 7; idx = 0; }               /* status, absolute mouse... */
	else {                                                            /* scan code */
		u8 code = b & 0x7F, down = !(b & 0x80), bit = 0;
		if (down && (code == 0x01 || code == 0x39)) stQuit = 1;     /* Esc, Space */
		if (down && code >= 0x02 && code <= 0x0B) numKey = (u8)(code - 1);          /* keys 1 .. 9, 0 */
		if (code == 0x48) bit = ST_UP; else if (code == 0x50) bit = ST_DOWN; else if (code == 0x4B) bit = ST_LEFT; else if (code == 0x4D) bit = ST_RIGHT;
		if (bit) { if (down) keyBits |= bit; else keyBits &= ~bit; }
	}
}

static void PollAcia(void) {
	u8 st;
	while ((st = REG8(0xFFFC00)) & 1) {                 /* RDRF */
		u8 b = REG8(0xFFFC02);
		if (!(st & 0x20)) KbdByte(b);                   /* skip it when overrun */
	}
}

u8 StInput(void) { return joyBits | keyBits; }
u8 StTakeNumKey(void) { u8 k = numKey; numKey = 0; return k; }
u8 *StAlign256(void *p) { return (u8 *)(((u32)p + 255) & ~255UL); }

static void SetScreen(const u8 *p) {
	u32 a = (u32)p;
	REG8(0xFF8201) = (u8)(a >> 16);
	REG8(0xFF8203) = (u8)(a >> 8);
}

static __attribute__((interrupt)) void vblHandler(void) {
	dispScr = shownScr;
	if (readyScr) { SetScreen(readyScr); shownScr = readyScr; readyScr = 0; }
	stFrame++;
	PollAcia();
	if (stVblHook) stVblHook();
}

void StPresent(u8 *screen) { readyScr = screen; }

/* the machine: the _MCH cookie of the cookie jar (pointer at $5A0, supervisor mode): high word 0 = ST, 1 = STE (also the Mega STE), 2 = TT, 3 = Falcon.  No jar (TOS 1.0 / 1.02) = ST. */
int StMachine(void) {
	const u32 *jar = *(const u32 *volatile *)0x5A0;
	if (!jar) return 0;
	for (; jar[0]; jar += 2)
		if (jar[0] == 0x5F4D4348UL) return (int)(jar[1] >> 16);          /* '_MCH' */
	return 0;
}
int StIsSTE(void) { return StMachine() >= 1; }
void StSetPalette(const u16 *palette) { int i; for (i = 0; i < 16; i++) REG16(0xFF8240 + 2 * i) = palette[i]; }
void StWaitPresented(void) {
	u16 f;
	while (readyScr && !stQuit) ;
	f = stFrame;
	while (stFrame == f && !stQuit) ;                  /* one more vertical blank: the old screen is surely not on display any more */
}
u8 *StShown(void) { return shownScr; }
u8 *StDisp(void) { return dispScr; }
u8 *StReady(void) { return readyScr; }

static u8 savedSync;
void StInit(const u16 *palette, u8 *first) {
	int i;
	for (i = 0; i < 16; i++) savedPal[i] = REG16(0xFF8240 + 2 * i);
	savedRez = REG8(0xFF8260) & 3;
	savedBase = (u8 *)(((u32)REG8(0xFF8201) << 16) | ((u32)REG8(0xFF8203) << 8));
	savedIerb = REG8(0xFFFA09); savedImrb = REG8(0xFFFA15);
	savedVbl = *(void *volatile *)0x70;
	savedSync = REG8(0xFF820A);

	__asm__ volatile("move.w #0x2700,%%sr" ::: "memory");
	REG8(0xFFFA09) = savedIerb & ~0x40; REG8(0xFFFA15) = savedImrb & ~0x40;     /* TOS keyboard handler off: we poll the ACIA */
	REG8(0xFF8260) = 0;
	REG8(0xFF820A) = 2;                                 /* 50 Hz (bit 1), like a PAL ST: an NTSC TOS / EmuTOS would start in 60 Hz and everything would run 20 % faster than on the Amiga */
	for (i = 0; i < 16; i++) REG16(0xFF8240 + 2 * i) = palette[i];
	shownScr = dispScr = first;
	SetScreen(first);
	*(void *volatile *)0x70 = (void *)vblHandler;
	while (REG8(0xFFFC00) & 1) (void)REG8(0xFFFC02);
	AciaWrite(0x12);                                   /* mouse off */
	AciaWrite(0x14);                                   /* joystick event reporting */
	__asm__ volatile("move.w #0x2300,%%sr" ::: "memory");   /* interrupts on: VBL (level 4) and the MFP (level 6) */
}

void StExit(void) {
	int i;
	StYmStop();
	StSndhStop();
	__asm__ volatile("move.w #0x2700,%%sr" ::: "memory");
	*(void *volatile *)0x70 = savedVbl;
	AciaWrite(0x08);                                   /* mouse relative reporting again */
	while (REG8(0xFFFC00) & 1) (void)REG8(0xFFFC02);
	REG8(0xFFFA09) = savedIerb; REG8(0xFFFA15) = savedImrb;
	for (i = 0; i < 16; i++) REG16(0xFF8240 + 2 * i) = savedPal[i];
	REG8(0xFF8260) = (u8)savedRez;
	REG8(0xFF820A) = savedSync;
	SetScreen(savedBase);
	__asm__ volatile("move.w #0x2300,%%sr" ::: "memory");
}

static const u8 font35[10][5] = {      /* 3 x 5 digits, bit 2 = left */
	{7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7}
};

void StDrawNumber(u8 *s, int value, int x0, int y0) { StDrawNumberCol(s, value, x0, y0, 2, 0); }

void StDrawNumberCol(u8 *s, int value, int x0, int y0, int col, int bg) {
	int d, n, r, c, k;
	u16 on1 = (col & 2) != 0, on0 = col & 1, bg1 = (bg & 2) != 0, bg0 = bg & 1;
	for (d = 0; d < 4; d++) {                           /* 4 digits, blank leading zeros */
		int div = d == 0 ? 1000 : d == 1 ? 100 : d == 2 ? 10 : 1;
		n = (value / div) % 10;
		for (r = 0; r < 5; r++)
			for (c = 0; c < 3; c++)
				for (k = 0; k < 4; k++) {                /* every font pixel is 2 x 2 screen pixels */
					int x = x0 + d * 8 + c * 2 + (k & 1), y = y0 + r * 2 + (k >> 1);
					u16 *w = (u16 *)(s + y * ST_LINE_BYTES + (x >> 4) * 8);   /* plane 0, plane 1 follows */
					u16 m = (u16)(0x8000 >> (x & 15));
					int on = (value >= div || d == 3) && ((font35[n][r] >> (2 - c)) & 1);
					if (on) { if (on1) w[1] |= m; else w[1] &= ~m; if (on0) w[0] |= m; else w[0] &= ~m; } else if (bg >= 0) { if (bg1) w[1] |= m; else w[1] &= ~m; if (bg0) w[0] |= m; else w[0] &= ~m; }      /* bg < 0: transparent, the picture under the digits stays */
				}
	}
}

/* ---- YM2149 player: .ymr stream, one frame per timer A interrupt (2.4576 MHz / 200 / 246 = 49.95 Hz) ---- */
u8 stYmRegs[16];
static const u8 *ymBase, *ymPos;
static u32 ymLoop;
static int ymOn;

static void YmWrite(u8 r, u8 v) {
	if (r == 7) v |= 0xC0;                                  /* ports A and B stay outputs (floppy select, Centronics) */
	stYmRegs[r] = v;
	REG8(0xFF8800) = r;
	REG8(0xFF8802) = v;
}

static void YmFrame(void) {
	u16 m = (u16)((ymPos[0] << 8) | ymPos[1]);
	u8 r;
	if (m == 0xFFFF) {                                      /* end: back to the loop frame */
		ymPos = ymBase + ymLoop;
		m = (u16)((ymPos[0] << 8) | ymPos[1]);
	}
	ymPos += 2;
	for (r = 0; r < 14; r++)
		if (m & (1u << r)) YmWrite(r, *ymPos++);
}

static __attribute__((interrupt)) void timerAHandler(void) {
	YmFrame();
	REG8(0xFFFA0F) = 0xDF;                                  /* end of interrupt (software EOI: ISRA bit 5) */
}

void StYmStart(const u8 *tune) {
	u8 r;
	if (ymOn) StYmStop();
	ymBase = tune + 4;
	ymLoop = ((u32)tune[0] << 24) | ((u32)tune[1] << 16) | ((u32)tune[2] << 8) | tune[3];
	ymPos = ymBase;
	for (r = 0; r < 14; r++) YmWrite(r, 0);
	YmWrite(7, 0x3F);
	savedIera = REG8(0xFFFA07); savedImra = REG8(0xFFFA13); savedTacr = REG8(0xFFFA19);
	savedTimerA = *(void *volatile *)0x134;
	__asm__ volatile("move.w #0x2700,%%sr" ::: "memory");
	REG8(0xFFFA19) = 0;                                     /* timer A stopped */
	*(void *volatile *)0x134 = (void *)timerAHandler;
	REG8(0xFFFA1F) = 246;
	REG8(0xFFFA0B) = 0xDF;                                  /* clear a pending timer A interrupt */
	REG8(0xFFFA07) = savedIera | 0x20; REG8(0xFFFA13) = savedImra | 0x20;
	REG8(0xFFFA19) = 7;                                     /* delay mode, divide by 200 */
	ymOn = 1;
	__asm__ volatile("move.w #0x2300,%%sr" ::: "memory");
}

void StYmStop(void) {
	u8 r;
	if (!ymOn) return;
	ymOn = 0;
	__asm__ volatile("move.w #0x2700,%%sr" ::: "memory");
	REG8(0xFFFA19) = 0;
	REG8(0xFFFA07) = savedIera; REG8(0xFFFA13) = savedImra;
	*(void *volatile *)0x134 = savedTimerA;
	REG8(0xFFFA0B) = 0xDF; REG8(0xFFFA0F) = 0xDF;
	for (r = 8; r <= 10; r++) YmWrite(r, 0);                /* silence */
	YmWrite(7, 0x3F);
	__asm__ volatile("move.w #0x2300,%%sr" ::: "memory");
}
