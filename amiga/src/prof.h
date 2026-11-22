/* prof.h - optional stage timing for the Amiga programs (build with PROFILE=1).  Uses CIA-B timer A (E clock, 709379 Hz PAL = 1.4097 us
   per tick) chained with timer B (counts timer A underflows) as a free-running 32 bit down counter (one frame = 14186 ticks).  Sums are
   kept in `prof`, which carries a magic number so tools/readprof.py can find it in the emulator's memory through the GDB server.
   Without PROFILE everything is empty. */
#ifndef PROF_H
#define PROF_H

#ifdef PROFILE
struct Prof { unsigned long magic, n, frames, t[16]; };
extern struct Prof prof;
static inline void ProfInit(void) {
	volatile unsigned char *c = (volatile unsigned char *)0xBFD000;
	c[0x400] = 0xFF; c[0x500] = 0xFF;     /* timer A latch = 65535 */
	c[0x600] = 0xFF; c[0x700] = 0xFF;     /* timer B latch = 65535 */
	c[0xE00] = 0x11;                      /* timer A: start, force load, continuous, counts the E clock */
	c[0xF00] = 0x51;                      /* timer B: start, force load, continuous, counts timer A underflows */
}
static inline unsigned long ProfNow(void) {
	volatile unsigned char *c = (volatile unsigned char *)0xBFD000;
	unsigned char bh, bl, ah, al, bh2;
	do { bh = c[0x700]; bl = c[0x600]; ah = c[0x500]; al = c[0x400]; bh2 = c[0x700]; } while (bh != bh2);
	return ((unsigned long)bh << 24) | ((unsigned long)bl << 16) | ((unsigned long)ah << 8) | al;
}
#define PROF(k, ...) do { unsigned long _a = ProfNow(); __VA_ARGS__; prof.t[k] += _a - ProfNow(); } while (0)
#define PCOUNT(k) (prof.t[k]++)
#else
#define PCOUNT(k) ((void)0)
static inline void ProfInit(void) {}
#define PROF(k, ...) __VA_ARGS__
#endif

#endif
