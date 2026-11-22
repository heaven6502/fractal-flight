/* amiga_sys.c - system takeover (from the Bartman template) and helpers shared by the Amiga programs */
#include "support/gcc8_c_support.h"
#include <proto/exec.h>
#include <proto/graphics.h>
#include <graphics/gfxbase.h>
#include <exec/execbase.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include "amiga_sys.h"
#include "prof.h"

#ifdef PROFILE
struct Prof prof = { 0x50524F46UL, 0, 0, { 0 } };   /* "PROF" */
#endif

struct ExecBase *SysBase;
volatile struct Custom *custom;
struct GfxBase *GfxBase;

static UWORD SystemInts, SystemDMA, SystemADKCON;
static volatile APTR VBR = 0;
static APTR SystemIrq;
static struct View *ActiView;

static APTR GetVBR(void) {
	APTR vbr = 0;
	UWORD getvbr[] = { 0x4e7a, 0x0801, 0x4e73 }; // MOVEC.L VBR,D0 RTE
	if (SysBase->AttnFlags & AFF_68010)
		vbr = (APTR)Supervisor((ULONG (*)())getvbr);
	return vbr;
}
static void SetIrq(APTR interrupt) { *(volatile APTR *)(((UBYTE *)VBR) + 0x6c) = interrupt; }
static APTR GetIrq(void) { return *(volatile APTR *)(((UBYTE *)VBR) + 0x6c); }

int SysOpen(void) {
	SysBase = *((struct ExecBase **)4UL);
	custom = (struct Custom *)0xdff000;
	GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 0);
	return GfxBase ? 0 : 1;
}

void SysClose(void) { CloseLibrary((struct Library *)GfxBase); }

void WaitVbl(void) {
	while (1) { volatile ULONG vpos = *(volatile ULONG *)0xDFF004; vpos &= 0x1ff00; if (vpos != (311 << 8)) break; }
	while (1) { volatile ULONG vpos = *(volatile ULONG *)0xDFF004; vpos &= 0x1ff00; if (vpos == (311 << 8)) break; }
}

void TakeSystem(void) {
	Forbid();
	SystemADKCON = custom->adkconr;
	SystemInts = custom->intenar;
	SystemDMA = custom->dmaconr;
	ActiView = GfxBase->ActiView;
#ifndef PUAE_FASTLOAD   /* puae-debugger injects the program before graphics.library delivers vertical blank signals: WaitTOF would never return */
	LoadView(0);
	WaitTOF();
	WaitTOF();
#endif
	WaitVbl();
	WaitVbl();
	OwnBlitter();
	WaitBlit();
	Disable();
	custom->intena = 0x7fff;
	custom->intreq = 0x7fff;
	custom->dmacon = 0x7fff;
	for (int a = 0; a < 32; a++) custom->color[a] = 0;
	WaitVbl();
	WaitVbl();
	VBR = GetVBR();
	SystemIrq = GetIrq();
}

void FreeSystem(void) {
	WaitVbl();
	WaitBlit();
	custom->intena = 0x7fff;
	custom->intreq = 0x7fff;
	custom->dmacon = 0x7fff;
	SetIrq(SystemIrq);
	custom->cop1lc = (ULONG)GfxBase->copinit;
	custom->cop2lc = (ULONG)GfxBase->LOFlist;
	custom->copjmp1 = 0x7fff;
	custom->intena = SystemInts | 0x8000;
	custom->dmacon = SystemDMA | 0x8000;
	custom->adkcon = SystemADKCON | 0x8000;
	WaitBlit();
	DisownBlitter();
	Enable();
	LoadView(ActiView);
	WaitTOF();
	WaitTOF();
	Permit();
}

void SetVblHandler(void (*handler)(void)) {
	SetIrq((APTR)handler);
	custom->intena = INTF_SETCLR | INTF_INTEN | INTF_VERTB;
	custom->intreq = (1 << INTB_VERTB);
}

short MouseLeft(void) { return !((*(volatile UBYTE *)0xbfe001) & 64); }

void MakeChunkTables(UBYTE *tab0, UBYTE *tab1) {
	for (int b = 0; b < 256; b++) {
		UBYTE p0 = 0, p1 = 0;
		for (int i = 0; i < 4; i++) {
			int v = (b >> (6 - 2 * i)) & 3;
			p0 = (p0 << 2) | ((v & 1) ? 3 : 0);
			p1 = (p1 << 2) | ((v & 2) ? 3 : 0);
		}
		tab0[b] = p0;
		tab1[b] = p1;
	}
}
