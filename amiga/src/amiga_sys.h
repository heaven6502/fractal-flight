/* amiga_sys.h - system takeover and small helpers shared by the Amiga programs (OCS, 68000, no libc) */
#ifndef AMIGA_SYS_H
#define AMIGA_SYS_H
#include <exec/types.h>

extern struct ExecBase *SysBase;
extern volatile struct Custom *custom;
extern struct GfxBase *GfxBase;

int   SysOpen(void);                 /* 0 = ok: SysBase, custom, graphics.library */
void  SysClose(void);
void  TakeSystem(void);              /* save and stop the OS: interrupts, DMA, colours black */
void  FreeSystem(void);
void  WaitVbl(void);                 /* wait for the start of the vertical blank (line 311) */
void  SetVblHandler(void (*handler)(void));   /* level 3 vector, enables the vertical blank interrupt */
short MouseLeft(void);
void  MakeChunkTables(UBYTE *tab0, UBYTE *tab1);  /* 4 two-bit pixels in a byte -> 8 doubled pixels: plane 0 / plane 1 byte */

#endif
