; st_sndh.s - the SNDH replay: Timer C interrupt (200 Hz) that calls the tune's PLAY routine at its own rate, and the INIT / EXIT calls.  vasm, Motorola syntax.
;
; SNDH: +0 bra.w INIT (d0.w = sub tune, 1 based), +4 bra.w EXIT, +8 bra.w PLAY.  The tune may use any register and the other MFP timers (A, B, D) itself.
; Timer C (TOS system tick, 200 Hz) is ours while the tune plays; the PLAY rate nn Hz is made from it with an accumulator (the "smart routine" of the SNDH spec):
; acc += nn at every tick, PLAY when acc >= 200, then acc -= 200.

	section	.text,code
	xdef	sndhTimerC
	xdef	sndhInit
	xdef	sndhExit
	xdef	sndhBase
	xdef	sndhRate
	xdef	sndhAcc
	xdef	sndhPlays
	xdef	sndhTicks

sndhTimerC:
	movem.l	d0/d1/a0,-(sp)
	addq.l	#1,sndhTicks
	move.w	sndhAcc,d0
	add.w	sndhRate,d0
	cmp.w	#200,d0
	blo.s	.wait
	sub.w	#200,d0
	move.w	d0,sndhAcc
	addq.l	#1,sndhPlays
	movem.l	d2-d7/a1-a6,-(sp)
	move.l	sndhBase,a0
	jsr	8(a0)				; PLAY
	movem.l	(sp)+,d2-d7/a1-a6
	bra.s	.done
.wait:	move.w	d0,sndhAcc
.done:	movem.l	(sp)+,d0/d1/a0
	move.b	#$DF,$FFFA11			; end of interrupt: ISRB bit 5 (timer C)
	rte

; void sndhInit(const u8 *base, int subtune)
sndhInit:
	movem.l	d2-d7/a2-a6,-(sp)		; 44 bytes + the return address
	move.l	48(sp),a0
	move.l	52(sp),d0
	jsr	(a0)				; INIT
	movem.l	(sp)+,d2-d7/a2-a6
	rts

; void sndhExit(const u8 *base)
sndhExit:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),a0
	jsr	4(a0)				; EXIT
	movem.l	(sp)+,d2-d7/a2-a6
	rts

	section	.bss,bss
sndhBase:	ds.l	1
sndhRate:	ds.w	1
sndhAcc:	ds.w	1
	cnop	0,4
sndhPlays:	ds.l	1
sndhTicks:	ds.l	1
