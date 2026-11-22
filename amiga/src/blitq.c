/* blitq.c - see blitq.h */
#include <hardware/custom.h>
#include "amiga_sys.h"
#include "blitq.h"

BlitOp bq[96];
void (*blitChainDone)(void);
UWORD blitMarkAt;
void (*blitMarkFn)(void);
static volatile UWORD bqNext, bqCount, bqBusy;

static void BlitStart(const BlitOp *o) {
	custom->bltcon0 = o->con0; custom->bltcon1 = o->con1;
	custom->bltafwm = 0xffff; custom->bltalwm = 0xffff;
	custom->bltamod = custom->bltcmod = custom->bltdmod = o->mod;
	if (o->con0 & 0x0400) { custom->bltbmod = o->mod; custom->bltbpt = (APTR)o->b; }       /* channel B (a mask), only if the operation uses it */
	if (o->ext) custom->bltamod = custom->bltbmod = o->smod;           /* a bob: the mask and the shape have their own row modulo */
	custom->bltapt = (APTR)o->a; custom->bltcpt = (APTR)o->c; custom->bltdpt = (APTR)o->d;
	custom->bltsize = o->size;
}

void BlitNext(void) {                                  /* interrupt: the blitter finished one operation */
	if (blitMarkAt && bqNext == blitMarkAt && blitMarkFn) blitMarkFn();     /* the first blitMarkAt operations are done */
	if (bqNext < bqCount) {
		BlitStart(&bq[bqNext++]);
	} else {
		bqBusy = 0;
		if (blitChainDone) blitChainDone();
	}
}

void WaitBlt(void) {                                   /* every queued operation is done */
	while (bqBusy) {}
	UWORD tst = custom->dmaconr; (void)tst;            /* for compatibility with the A1000 */
	while (custom->dmaconr & (1 << 14)) {}
}

void BlitRun(int n) {
	WaitBlt();
	bqCount = n; bqNext = 1; bqBusy = 1;
	if (blitMarkAt >= n) blitMarkAt = 0;
	BlitStart(&bq[0]);
}
