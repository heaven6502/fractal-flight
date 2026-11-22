; st_f320.s - the clear and the drip of flight320 on the 68000 CPU (the Amiga does them with the blitter).  vasm, Motorola syntax.
;
;   void F3ClearRows(u8 *scr, int from, int to);    the lines from .. to - 1 of the screen (160 bytes each, all four planes) := 0
;   void F3DripRows(u8 *scr, int from, int to);    for the lines from .. to - 1: plane 1 (the ground; the second word of every group of 4) |= the same of the line above
;
; The depth bands: every band draws its ridge pixels into its own plane (plane 0 = near, 1 = middle, 2 = far), plane 3 is 1 in the whole picture (the colours 8 .. 15).  The drip ORs all four words
; of a group down, g(row) = g(row - 1) | line(row), the lines are processed top down so the dripped line above is used.  `from` must be >= 1.  A pixel's colour index is 8 + the set of bands that lie above it.

	section	.text,code
	xdef	F3ClearRows
	xdef	F3DripRows
	xdef	F3DrawContoursAsm
	xref	ptList
	xref	ptReal
	xref	f3RowOff
	xref	f3XTab

F3ClearRows:
	movem.l	d2-d7/a2-a6,-(sp)
	movea.l	48(sp),a0			; scr
	move.l	52(sp),d0			; from
	move.l	56(sp),d7			; to
	sub.l	d0,d7				; rows
	ble.s	.cdone
	mulu	#160,d0
	adda.l	d0,a0
	mulu	#160,d7
	adda.l	d7,a0				; the end of the lines: fill downwards
	divu	#40,d7				; blocks of 40 bytes = 5 groups (planes 0, 1, 2 = 0, plane 3 = $FFFF)
	subq.w	#1,d7
	bmi.s	.cdone
	moveq	#0,d0
	move.l	#$0000FFFF,d1			; one group in memory order: long $00000000 (planes 0, 1), long $0000FFFF (plane 2 = 0, plane 3 = 1)
	moveq	#0,d2
	move.l	d1,d3
	moveq	#0,d4
	move.l	d1,d5
	moveq	#0,d6
	move.l	d1,a1
	moveq	#0,d2
	move.l	d2,a2
	move.l	d1,a3
.blk:	movem.l	d0-d6/a1-a3,-(a0)		; 10 registers = 40 bytes
	dbra	d7,.blk
.cdone:
	movem.l	(sp)+,d2-d7/a2-a6
	rts

F3DripRows:
	movem.l	d2-d7/a2-a6,-(sp)
	movea.l	48(sp),a0			; scr
	move.l	52(sp),d0			; from
	move.l	56(sp),d7			; to
	sub.l	d0,d7				; rows
	ble	.sdone
	mulu	#160,d0
	adda.l	d0,a0				; the first line to drip
	move.w	d7,d6
	lsr.w	#2,d6				; groups of 4 rows
	andi.w	#3,d7				; the remaining rows
	moveq	#19,d5				; 20 groups of 16 pixels, one after the other, down the whole picture: the line above is kept in registers (planes 0 and 1 as a long in d0, plane 2 as a word in d1; plane 3 is 1 everywhere and stays)
.grp:	movea.l	a0,a1
	move.l	-160(a1),d0
	move.w	-156(a1),d1
	move.w	d6,d4
	subq.w	#1,d4
	bmi.s	.rem
.quad:	or.l	(a1),d0
	move.l	d0,(a1)
	or.w	4(a1),d1
	move.w	d1,4(a1)
	or.l	160(a1),d0
	move.l	d0,160(a1)
	or.w	164(a1),d1
	move.w	d1,164(a1)
	or.l	320(a1),d0
	move.l	d0,320(a1)
	or.w	324(a1),d1
	move.w	d1,324(a1)
	or.l	480(a1),d0
	move.l	d0,480(a1)
	or.w	484(a1),d1
	move.w	d1,484(a1)
	lea	640(a1),a1
	dbra	d4,.quad
.rem:	move.w	d7,d4
	subq.w	#1,d4
	bmi.s	.gnext
.r1:	or.l	(a1),d0
	move.l	d0,(a1)
	or.w	4(a1),d1
	move.w	d1,4(a1)
	lea	160(a1),a1
	dbra	d4,.r1
.gnext:	addq.l	#8,a0
	dbra	d5,.grp
.sdone:
	movem.l	(sp)+,d2-d7/a2-a6
	rts

; void F3DrawContoursAsm(u8 *scr): the contour points (ptList .. ptReal, x and y words) as sky coloured pixels: the pixel's bit is cleared in planes 0 .. 2 (plane 3 stays 1: colour 8)
F3DrawContoursAsm:
	movem.l	d2-d3/a2-a5,-(sp)		; 6 registers = 24 bytes + the return address
	movea.l	28(sp),a2			; scr
	lea	ptList,a0
	movea.l	ptReal,a3
	lea	f3RowOff,a4
	lea	f3XTab,a5
	cmpa.l	a3,a0
	bhs.s	.dd
.dl:	move.w	(a0)+,d0			; x
	move.w	(a0)+,d1			; y
	add.w	d1,d1
	move.w	(a4,d1.w),d2			; the row offset
	add.w	d0,d0
	add.w	d0,d0				; x * 4
	add.w	(a5,d0.w),d2			; + the byte offset of the group of 16 pixels
	move.w	2(a5,d0.w),d3			; the pixel mask
	not.w	d3
	lea	(a2,d2.w),a1
	and.w	d3,(a1)
	and.w	d3,2(a1)
	and.w	d3,4(a1)
	cmpa.l	a3,a0
	blo.s	.dl
.dd:	movem.l	(sp)+,d2-d3/a2-a5
	rts
