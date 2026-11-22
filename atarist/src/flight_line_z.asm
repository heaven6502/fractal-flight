; flight_line_z.asm (the depth band variant of flight_line.asm: the pixel goes into the plane of the edge's depth class, fClassW bytes further) - the byte line routine of the fractal flight (Line51C of flight_core.c, LB51C of the game / line51 of the 800XL port) with the
; pixel plot (PlotCol + planar PlotPixel) inlined as a subroutine, for the 68000.  vasm, Motorola syntax.
;
;   void Line51CAsm(int X, int yi, int z84, int z86);        (C calling convention: arguments on the stack, d2-d7/a2-a6 are saved here)
;
;   X      column of end 1 (end 1 = X, z84 (y), z86 (h));  yi = index of the stack top;  end 2 = the stack top ZX/ZY/ZH[yi]
;   Halves the piece (pushing midpoints with the fractal displacement) until it is 1 or 2 columns wide, plots, pops, goes on to the right.
;
; The three byte stacks are one array fZ: ZX at +0, ZY at +48, ZH at +96 (all indexes below 48).  The ridge buffer fLineBuf is a byte array.
; Plot of (x, y): only if y > fLineBuf[x]; the column is closed ($FF, drawn at yTop) when y >= yTop; the pixel is 2 bits wide and 3 screen rows tall
; in plane 0: row address fRowAddr[y] (longs, built by RenderScene), byte offset fColOff[x], mask fMask[x], row distance = stride (fCfg).
; Columns outside the view hold $FF in fLineBuf, so they never get here: no range checks.
;
;   registers:  d0 = X   d1 = yi   d2 = z84   d3 = z86   d4..d7 temporaries
;               a0 = fZ  a2 = fLineBuf  a3 = fColOff  a4 = fMask  a5 = fRowAddr  a6 = fCfg {colHi.b, yTop.b, stride.w}  a1 = temporary

	section	.text,code

	xref	fZ
	xref	fLineBuf
	xref	fColOff
	xref	fMask
	xref	fRowAddr
	xref	fCfg
	xref	fClassW
	xdef	Line51CAsm
	xdef	Line51CCore

	ifnd	ROWREP
ROWREP	equ	3				; picture rows per ridge row (-DROWREP=1 on the Atari ST)
	endc

ZY	equ	48
ZH	equ	96

Line51CAsm:
	movem.l	d2-d7/a2-a6,-(sp)		; 11 registers = 44 bytes, + the return address
	move.l	48(sp),d0			; X
	move.l	52(sp),d1			; yi
	move.l	56(sp),d2			; z84
	move.l	60(sp),d3			; z86
	bsr	Line51CCore
	movem.l	(sp)+,d2-d7/a2-a6
	rts

; the same with the arguments in registers (d0 = X, d1 = yi, d2 = z84, d3 = z86), for DrawEdgeAsm; clobbers every register except sp
Line51CCore:
	lea	fZ,a0
	lea	fLineBuf,a2
	lea	fColOff,a3
	lea	fMask,a4
	lea	fRowAddr,a5
	lea	fCfg,a6

.loop:
	cmp.b	(a6),d0				; X >= colHi: done
	bhs	.done
	move.b	d0,d4
	sub.b	(a0,d1.w),d4			; a = (X - ZX[yi]) & $FF
	cmp.b	#$FE,d4
	beq	.two				; two columns
	addq.b	#1,d4
	beq	.one				; one column ($FF)

	; ---- wider: push the midpoint with the fractal displacement
	moveq	#0,d4
	move.b	(a0,d1.w),d4			; ZX[yi]
	add.w	d0,d4
	lsr.w	#1,d4				; mid = (X + ZX[yi]) >> 1
	move.b	d4,1(a0,d1.w)			; ZX[yi+1] = mid
	moveq	#0,d5
	move.b	ZH(a0,d1.w),d5			; ZH[yi]
	add.w	d3,d5
	addq.w	#1,d5				; t = z86 + ZH[yi] + 1
	move.b	d5,ZH+1(a0,d1.w)		; ZH[yi+1] = t & $FF   (bit 8 of d5 = carry)
	moveq	#0,d6
	move.b	ZY(a0,d1.w),d6			; ZY[yi]
	add.w	d2,d6				; t2 = z84 + ZY[yi]
	move.w	d6,d7				; (bit 0 = the carry out of the ror)
	lsr.w	#1,d6				; a = t2 >> 1
	btst	#7,d5
	beq	.nodisp				; the h byte is below $80: no displacement
	btst	#8,d5
	bne	.carry
	; h sum without carry: a = a - ((mid - X - 1) >> 1) - (1 - c2)
	move.b	d4,d5
	sub.b	d0,d5
	subq.b	#1,d5
	lsr.b	#1,d5
	sub.b	d5,d6
	btst	#0,d7
	bne	.nodisp
	subq.b	#1,d6
	bra	.nodisp
.carry:	; h sum with carry: a = min(255, a + ((mid - X) >> 1) + c2)
	move.b	d4,d5
	sub.b	d0,d5
	lsr.b	#1,d5
	andi.w	#$FF,d5
	add.w	d5,d6
	btst	#0,d7
	beq	.nc2
	addq.w	#1,d6
.nc2:	cmp.w	#255,d6
	bls	.nodisp
	move.w	#255,d6
.nodisp:
	move.b	d6,ZY+1(a0,d1.w)		; ZY[yi+1] = a
	addq.w	#1,d1
	bra	.loop

	; ---- two columns: plot the middle, then the end
.two:
	moveq	#0,d4
	move.b	ZY(a0,d1.w),d4
	add.w	d2,d4
	addq.w	#1,d4
	lsr.w	#1,d4				; (ZY[yi] + z84 + 1) >> 1
	move.b	(a2,d0.w),d5			; highest ridge value so far in this column
	cmp.b	d5,d4
	bls	.pret1				; y <= fLineBuf[x]: hidden
	move.b	d4,(a2,d0.w)
	cmp.b	1(a6),d4			; y >= yTop: draw at yTop and close the column
	blo	.pok1
	move.b	#$FF,(a2,d0.w)
	move.b	1(a6),d4
.pok1:
	add.w	d4,d4
	add.w	d4,d4				; y * 4
	movea.l	(a5,d4.w),a1			; address of the picture row in plane 0
	moveq	#0,d5
	move.b	(a3,d0.w),d5			; byte offset of the column
	add.w	fClassW,d5			; + the plane of the depth class (flightz)
	move.b	(a4,d0.w),d6			; pixel mask
	or.b	d6,(a1,d5.w)
	ifgt	ROWREP-1
	adda.w	2(a6),a1			; next screen row
	or.b	d6,(a1,d5.w)
	endc
	ifgt	ROWREP-2
	adda.w	2(a6),a1
	or.b	d6,(a1,d5.w)
	endc
.pret1:
	addq.b	#1,d0				; X + 1
	moveq	#0,d2
	move.b	ZY(a0,d1.w),d2			; z84 = ZY[yi]
	move.w	d2,d4
	move.b	(a2,d0.w),d5			; highest ridge value so far in this column
	cmp.b	d5,d4
	bls	.pret2				; y <= fLineBuf[x]: hidden
	move.b	d4,(a2,d0.w)
	cmp.b	1(a6),d4			; y >= yTop: draw at yTop and close the column
	blo	.pok2
	move.b	#$FF,(a2,d0.w)
	move.b	1(a6),d4
.pok2:
	add.w	d4,d4
	add.w	d4,d4				; y * 4
	movea.l	(a5,d4.w),a1			; address of the picture row in plane 0
	moveq	#0,d5
	move.b	(a3,d0.w),d5			; byte offset of the column
	add.w	fClassW,d5			; + the plane of the depth class (flightz)
	move.b	(a4,d0.w),d6			; pixel mask
	or.b	d6,(a1,d5.w)
	ifgt	ROWREP-1
	adda.w	2(a6),a1			; next screen row
	or.b	d6,(a1,d5.w)
	endc
	ifgt	ROWREP-2
	adda.w	2(a6),a1
	or.b	d6,(a1,d5.w)
	endc
.pret2:
	bra	.pop

	; ---- one column
.one:
	moveq	#0,d2
	move.b	ZY(a0,d1.w),d2			; z84 = ZY[yi]
	move.w	d2,d4
	move.b	(a2,d0.w),d5			; highest ridge value so far in this column
	cmp.b	d5,d4
	bls	.pret3				; y <= fLineBuf[x]: hidden
	move.b	d4,(a2,d0.w)
	cmp.b	1(a6),d4			; y >= yTop: draw at yTop and close the column
	blo	.pok3
	move.b	#$FF,(a2,d0.w)
	move.b	1(a6),d4
.pok3:
	add.w	d4,d4
	add.w	d4,d4				; y * 4
	movea.l	(a5,d4.w),a1			; address of the picture row in plane 0
	moveq	#0,d5
	move.b	(a3,d0.w),d5			; byte offset of the column
	add.w	fClassW,d5			; + the plane of the depth class (flightz)
	move.b	(a4,d0.w),d6			; pixel mask
	or.b	d6,(a1,d5.w)
	ifgt	ROWREP-1
	adda.w	2(a6),a1			; next screen row
	or.b	d6,(a1,d5.w)
	endc
	ifgt	ROWREP-2
	adda.w	2(a6),a1
	or.b	d6,(a1,d5.w)
	endc
.pret3:

	; ---- pop: the next piece starts one column further right
.pop:
	subq.w	#1,d1
	bmi	.done
	addq.b	#1,d0
	moveq	#0,d3
	move.b	ZH+1(a0,d1.w),d3		; z86 = ZH[yi+1]
	bra	.loop

.done:
	rts

