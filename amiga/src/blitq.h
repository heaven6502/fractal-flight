/* blitq.h - a short list of blitter operations, chained by the blitter-finished interrupt (level 3, shared with the vertical blank), so that the CPU can go on
   working while the blitter runs a whole sequence.  Shared by the Amiga programs; the program's level 3 handler calls BlitNext() when INTF_BLIT is set. */
#ifndef BLITQ_H
#define BLITQ_H
#include <exec/types.h>

typedef struct { UWORD con0, size, mod; UBYTE *a, *c, *d, *b; UWORD con1, smod, ext; } BlitOp;   /* one modulo for A, B, C and D; BLTCON1 = 0, masks $FFFF; b is only used when con0 enables channel B (USEB, $0400); con1 = BLTCON1 (0, or $0002 = descending); ext = 1: A and B (the mask and the shape of a bob) use the modulo smod, C and D mod */

extern BlitOp bq[96];                      /* fill bq[0 .. n-1], then BlitRun(n) */
extern void (*blitChainDone)(void);       /* called from the interrupt when a whole chain has finished (may be 0) */
extern UWORD blitMarkAt;                  /* if non-zero: blitMarkFn is called from the interrupt when the first blitMarkAt operations of the chain are done */
extern void (*blitMarkFn)(void);

void BlitRun(int n);                      /* waits for the blitter, then runs bq[0 .. n-1] one after the other (returns at once) */
void BlitNext(void);                      /* from the level 3 interrupt on INTF_BLIT: start the next operation or finish the chain */
void WaitBlt(void);                       /* the whole chain is done */

#endif
