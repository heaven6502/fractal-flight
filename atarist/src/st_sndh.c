/* st_sndh.c - SNDH music player (Atari ST YM2149 tunes with their own 68000 replay code), see st_sndh.s.  Runs in supervisor mode.
   The tune is used in place (SNDH code is position independent).  Compressed (ICE!) files must be unpacked first. */
#include "st_sys.h"

#define REG8(a) (*(volatile u8 *)(a))

extern void sndhTimerC(void);
extern void sndhInit(const u8 *base, int subtune);
extern void sndhExit(const u8 *base);
extern const u8 *sndhBase;
extern u16 sndhRate, sndhAcc;
extern u32 sndhPlays, sndhTicks;
u32 StSndhPlays(void) { return sndhPlays; }
u32 StSndhTicks(void) { return sndhTicks; }

static int sndhOn;
static u8 savedTcdr, savedTcdcr;
static void *savedTimerC;
static const u8 *base;

/* tag parser: sets the replay rate and the number of sub tunes; returns the default sub tune */
static int Digits(const u8 *p, int n) {
	int v = 0;
	while (n-- > 0 && *p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
	return v;
}

int StSndhInfo(const u8 *s, int *rate, int *nsub) {
	const u8 *p = s + 16;
	int def = 1;
	*rate = 50; *nsub = 1;
	if (s[12] != 'S' || s[13] != 'N' || s[14] != 'D' || s[15] != 'H') return 0;
	while (p < s + 4096) {
		if (p[0] == 'H' && p[1] == 'D' && p[2] == 'N' && p[3] == 'S') break;
		if ((p[0] == 'T' && (p[1] == 'C' || p[1] == 'A' || p[1] == 'B' || p[1] == 'D') && (p[2] >= '0' && p[2] <= '9'))) {   /* TA / TB / TC / TD + frequency */
			*rate = Digits(p + 2, 4);
			while (*p++) ;
		} else if ((p[0] == 'T' && p[1] == 'I' && p[2] == 'T' && p[3] == 'L') || (p[0] == 'C' && p[1] == 'O' && p[2] == 'M' && p[3] == 'M') ||
		           (p[0] == 'R' && p[1] == 'I' && p[2] == 'P' && p[3] == 'P') || (p[0] == 'C' && p[1] == 'O' && p[2] == 'N' && p[3] == 'V') ||
		           (p[0] == 'Y' && p[1] == 'E' && p[2] == 'A' && p[3] == 'R')) {
			p += 4;
			while (*p++) ;
		} else if (p[0] == '!' && p[1] == 'V') {
			*rate = Digits(p + 2, 2);
			while (*p++) ;
		} else if (p[0] == '#' && p[1] == '#') {
			*nsub = Digits(p + 2, 2); p += 4;
		} else if (p[0] == '#' && p[1] == '!' && p[2] != 'S') {
			def = Digits(p + 2, 2); p += 4;
		} else if (p[0] == 'F' && p[1] == 'L' && p[2] == 'A' && p[3] == 'G') {
			p += 4;
			while (*p++) ;
		} else if (p[0] == '#' && p[1] == '!' && p[2] == 'S') {
			break;                                          /* sub tune names: the rate tag comes before them */
		} else p++;
	}
	if (*rate < 1 || *rate > 400) *rate = 50;
	return def;
}

/* subtune 0 = the tune's default.  Returns 0 on success */
int StSndhStart(const u8 *s, int subtune) {
	int rate, nsub, def;
	if (sndhOn) StSndhStop();
	def = StSndhInfo(s, &rate, &nsub);
	if (!def) return -1;
	if (subtune < 1 || subtune > nsub) subtune = def;
	base = s;
	sndhBase = s; sndhRate = (u16)rate; sndhAcc = 0;
	__asm__ volatile("move.w #0x2700,%%sr" ::: "memory");
	sndhInit(s, subtune);
	savedTimerC = *(void *volatile *)0x114;
	savedTcdr = REG8(0xFFFA23); savedTcdcr = REG8(0xFFFA1D);
	*(void *volatile *)0x114 = (void *)sndhTimerC;
	REG8(0xFFFA1D) = (u8)((savedTcdcr & 0x07) | 0x50);     /* timer C: divide by 64 ... */
	REG8(0xFFFA23) = 192;                                  /* ... 2.4576 MHz / 64 / 192 = 200 Hz */
	sndhOn = 1;
	__asm__ volatile("move.w #0x2300,%%sr" ::: "memory");
	return 0;
}

void StSndhStop(void) {
	u8 r;
	if (!sndhOn) return;
	__asm__ volatile("move.w #0x2700,%%sr" ::: "memory");
	sndhOn = 0;
	sndhExit(base);
	*(void *volatile *)0x114 = savedTimerC;
	REG8(0xFFFA1D) = savedTcdcr; REG8(0xFFFA23) = savedTcdr;
	REG8(0xFFFA11) = 0xDF;
	for (r = 8; r <= 10; r++) { REG8(0xFF8800) = r; REG8(0xFF8802) = 0; }     /* silence */
	REG8(0xFF8800) = 7; REG8(0xFF8802) = 0xFF;
	__asm__ volatile("move.w #0x2300,%%sr" ::: "memory");
}
