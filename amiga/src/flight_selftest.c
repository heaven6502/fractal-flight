/* flight_selftest.c - runs on the Amiga (Amiberry) and checks the 68000 assembly line routine (flight_line.asm) of the flight renderer.
   For every one of the 55 reference cases (testdata/far40_render.bin: terrain + camera and the ridge buffer of the REAL 6502 build) it renders the
   picture twice into a planar buffer: once with the C routine (fUseAsm = 0) and once with the assembly routine, and checks
     - the assembly picture is byte for byte identical to the C picture (plane 0 and the ridge buffer),
     - the ridge buffer of the C picture and of the assembly picture equals the 6502's.
   It also adds up the CIA ticks of both renders.  Results are in `selftest` (found by its magic number through the GDB server, tools/readself.py).
   Build and run:  tools/run.sh fselftest gdb ; python tools/readself.py */
#include "support/gcc8_c_support.h"
#include <proto/exec.h>
#include "flight_core.h"

INCBIN(vectors, "testdata/far40_render.bin")

struct SelfTest {
	unsigned long magic;          /* "SELF" */
	unsigned long done;           /* 1 when finished */
	unsigned long cases;
	unsigned long planeDiffs;     /* assembly plane 0 differs from the C plane 0: number of differing bytes */
	unsigned long ridgeAsmVsC;    /* ridge buffer columns: assembly differs from C */
	unsigned long ridgeCVs6502;   /* ridge buffer columns: C differs from the 6502 */
	unsigned long ridgeAsmVs6502;
	unsigned long ticksC, ticksAsm;
	unsigned long firstBadCase;
};
struct SelfTest selftest = { 0x53454C46UL, 0, 0, 0, 0, 0, 0, 0, 0, 0xFFFF };

static UBYTE planeC[FSTRIDE * FROWS], planeA[FSTRIDE * FROWS], lbC[256];

static void TimerInit(void) {
	volatile UBYTE *c = (volatile UBYTE *)0xBFD000;
	c[0x400] = 0xFF; c[0x500] = 0xFF; c[0x600] = 0xFF; c[0x700] = 0xFF;
	c[0xE00] = 0x11; c[0xF00] = 0x51;     /* CIA-B timer A (E clock) + timer B (counts A underflows): a 32 bit down counter */
}
static unsigned long TimerNow(void) {
	volatile UBYTE *c = (volatile UBYTE *)0xBFD000;
	UBYTE bh, bl, ah, al, bh2;
	do { bh = c[0x700]; bl = c[0x600]; ah = c[0x500]; al = c[0x400]; bh2 = c[0x700]; } while (bh != bh2);
	return ((unsigned long)bh << 24) | ((unsigned long)bl << 16) | ((unsigned long)ah << 8) | al;
}

int main(void) {
	const UBYTE *p = (const UBYTE *)vectors;
	unsigned long ncase = p[0] | (p[1] << 8), c;
	TFlight fl;
	p += 2;
	FInit();
	TimerInit();
	for (c = 0; c < ncase; c++) {
		UWORD cam[8];
		const UBYTE *lb6502;
		unsigned long t0, i, bad;
		memcpy(heightmap, p, 256); p += 256;
		for (i = 0; i < 8; i++) { cam[i] = p[0] | (p[1] << 8); p += 2; }
		lb6502 = p; p += VIEW_W;
		p += F_ROWS * (VIEW_W / 4);                                   /* the reference bitmap: checked on the PC, not needed here */
		NewFlight(&fl);
		fl.posXLo = cam[0] & 255; fl.posXHi = cam[0] >> 8; fl.posZLo = cam[1] & 255; fl.posZHi = cam[1] >> 8;
		fl.altLo = cam[2] & 255; fl.altHi = cam[2] >> 8; fl.headLo = cam[3] & 255; fl.headHi = cam[3] >> 8;
		fl.pitchLo = cam[4] & 255; fl.pitchHi = cam[4] >> 8; fl.rollLo = cam[5] & 255; fl.rollHi = cam[5] >> 8;
		fl.bankLo = cam[6] & 255; fl.bankHi = cam[6] >> 8; fl.speedLo = cam[7] & 255; fl.speedHi = cam[7] >> 8;

		memset(planeC, 0, sizeof planeC); fPlane = planeC; fUseAsm = 0;       /* the C routine */
		t0 = TimerNow(); RenderScene(&fl); selftest.ticksC += t0 - TimerNow();
		memcpy(lbC, fLineBuf, 256);

		memset(planeA, 0, sizeof planeA); fPlane = planeA; fUseAsm = 1;       /* the assembly routine */
		t0 = TimerNow(); RenderScene(&fl); selftest.ticksAsm += t0 - TimerNow();

		bad = 0;
		for (i = 0; i < sizeof planeA; i++) if (planeA[i] != planeC[i]) bad++;
		selftest.planeDiffs += bad;
		for (i = 0; i < 256; i++) if (fLineBuf[i] != lbC[i]) { selftest.ridgeAsmVsC++; bad++; }
		for (i = 0; i < VIEW_W; i++) {
			if (lbC[F_COL_LO + i] != lb6502[i]) { selftest.ridgeCVs6502++; bad++; }
			if (fLineBuf[F_COL_LO + i] != lb6502[i]) { selftest.ridgeAsmVs6502++; bad++; }
		}
		if (bad && selftest.firstBadCase == 0xFFFF) selftest.firstBadCase = c;
		selftest.cases = c + 1;
	}
	selftest.done = 1;
	for (;;) {}
	return 0;
}
