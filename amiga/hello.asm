; Hello world for Amiga OCS: copper-driven colour gradient, exit on left mouse button.
; Build: vasmm68k_mot -Fhunkexe -kick1hunks -nosym -o out/hello.exe hello.asm

CUSTOM    = $dff000
INTENAR   = $01c
DMACONR   = $002
INTENA    = $09a
DMACON    = $096
COP1LC    = $080
COPJMP1   = $088
COLOR00   = $180

	section code,code
start:
	movem.l	d0-d7/a0-a6,-(sp)
	move.l	4.w,a6			; ExecBase
	lea	gfxname(pc),a1
	moveq	#0,d0
	jsr	-552(a6)		; OpenLibrary
	move.l	d0,a5			; a5 = GfxBase (kept to restore copper)
	move.l	38(a5),d3		; gb_copinit: system copper list

	lea	CUSTOM,a6
	move.w	INTENAR(a6),d4
	or.w	#$8000,d4
	move.w	DMACONR(a6),d5
	or.w	#$8000,d5

	move.w	#$7fff,INTENA(a6)	; all interrupts off
	move.w	#$7fff,DMACON(a6)	; all DMA off
	lea	copper,a0
	move.l	a0,COP1LC(a6)
	move.w	COPJMP1(a6),d0		; strobe
	move.w	#$8080,DMACON(a6)	; SET + COPEN
	move.w	#$8200,DMACON(a6)	; DMAEN

.wait:	btst	#6,$bfe001		; left mouse button (active low)
	bne.s	.wait

	move.w	#$7fff,DMACON(a6)
	move.l	d3,COP1LC(a6)
	move.w	COPJMP1(a6),d0
	move.w	d5,DMACON(a6)
	move.w	#$7fff,INTENA(a6)
	move.w	d4,INTENA(a6)
	movem.l	(sp)+,d0-d7/a0-a6
	moveq	#0,d0
	rts

gfxname:	dc.b	"graphics.library",0
	even

	section chipdata,data_c
copper:
	dc.w	COLOR00,$0000
n	set	0
	rept	32
	dc.w	(($2c+n*3)&$ff)<<8|$01,$fffe
	dc.w	COLOR00,((n/4)<<8)|(n/2)
n	set	n+1
	endr
	rept	32
	dc.w	(($2c+n*3)&$ff)<<8|$01,$fffe
	dc.w	COLOR00,(((63-n)/4)<<8)|((63-n)/2)
n	set	n+1
	endr
	dc.w	(($2c+64*3)&$ff)<<8|$01,$fffe
	dc.w	COLOR00,$0000
	dc.w	$ffff,$fffe
