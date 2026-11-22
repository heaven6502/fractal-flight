; ---- F3EdgesAsm: the edge loop of F3Plot320 (flight_core.c) in assembly; text included by mkline320_st.py (the same as the Amiga port: tools/edges320_part.asm)
;   void F3EdgesAsm(const unsigned char *edges, int nEdges, int band1, int band2);
;   For every edge (a, b) of the table: dropped when vflag[a] & ($80 | $20) or vflag[b] & ($80 | $40); else both ends are projected (F3Project, C), the edge is drawn
;   when p3x[a] < p3x[b], drawn into the plane of its band (the first band1 edges: plane 0, up to band2: plane 1, then plane 2).  The line core (F3Run) is entered with the registers of F3SegAsm, so there is no
;   call, no argument stack and no register save per edge.  struct VTab offsets (flight_core.c, asserted there): vz16 224, vh 1116, vflag 1336.
	xref	vt
	xref	p3x
	xref	p3y
	xref	f3PitchR
	xref	f3RollS
	xref	ptReal
	xref	ptList
	xref	ptTrash
	xdef	F3ProjectAsm
	xdef	F3EdgesAsm

VX16	equ	0
VZ16	equ	224
VDY16	equ	896
VPDONE	equ	1996
F3SX	equ	183
F3SY	equ	183
F3Y0	equ	100				; F3_H * 56 / 100 with F3_H = 180
VH	equ	1116
VFLAG	equ	1336

F3EdgesAsm:
	movem.l	d2-d7/a2-a6,-(sp)
	lea	-24(sp),sp			; 0: b   4: a   8: edge pointer   12: end of the table   16: end of band 0   20: end of band 1
	move.l	72(sp),d0			; edges
	move.l	76(sp),d1			; nEdges
	add.l	d1,d1
	add.l	d0,d1
	move.l	d1,12(sp)
	move.l	d0,8(sp)
	move.l	80(sp),d1			; band1: the first edge of the middle band
	add.l	d1,d1
	add.l	d0,d1
	move.l	d1,16(sp)
	move.l	84(sp),d1			; band2: the first edge of the far band
	add.l	d1,d1
	add.l	d0,d1
	move.l	d1,20(sp)
	move.l	#ptList,ptPtr			; the contour points of the near and middle band (the far band records into ptTrash)
	clr.l	ptReal
	lea	f3LineBuf,a2			; the line core's constants (it leaves a2 .. a5 alone, and F3ProjectAsm keeps them)
	lea	f3XTab,a4
	lea	f3RowOff,a5
.e:	movea.l	8(sp),a1
	cmpa.l	12(sp),a1
	bhs	.edone
	moveq	#0,d0
	moveq	#0,d1
	move.b	(a1)+,d0			; a
	move.b	(a1)+,d1			; b
	move.l	a1,8(sp)
	move.l	d0,4(sp)
	move.l	d1,0(sp)
	lea	vt+VFLAG,a0
	lsl.w	#2,d0				; the tables hold longs
	lsl.w	#2,d1
	move.b	3(a0,d0.w),d2
	andi.b	#$A0,d2
	bne	.e
	move.b	3(a0,d1.w),d2
	andi.b	#$C0,d2
	bne	.e
	move.l	4(sp),-(sp)
	jsr	F3ProjectAsm
	addq.l	#4,sp
	move.l	0(sp),-(sp)
	jsr	F3ProjectAsm
	addq.l	#4,sp
	move.l	4(sp),d0
	lsl.w	#2,d0
	move.l	0(sp),d1
	lsl.w	#2,d1
	lea	p3x,a0
	move.l	(a0,d0.w),d2			; x1
	move.l	(a0,d1.w),d4			; x2
	cmp.l	d4,d2
	bge	.e				; the edge must run left to right
	move.l	8(sp),d5			; the edge pointer + 2: the band of the edge
	moveq	#0,d6				; its plane word: 0, 2 or 4 (the planes are the words of a group)
	cmp.l	16(sp),d5
	bls.s	.gb
	moveq	#2,d6
	cmp.l	20(sp),d5
	bls.s	.gb
	moveq	#4,d6
	cmp.l	#4,d6
	bne.s	.gb
	tst.l	ptReal
	bne.s	.gb
	move.l	ptPtr,ptReal			; the far band starts: the points so far are the ones to draw
	move.l	#ptTrash,ptPtr
.gb:	movea.l	f3Scr,a3
	lea	vt+VH,a1
	move.l	(a1,d0.w),d3
	addq.w	#1,d3				; h1 + 1
	lea	p3y,a0
	move.l	(a0,d0.w),d7			; y1
	lea	fZW,a0
	move.w	d4,(a0)				; x2
	move.w	2(a1,d1.w),ZWH(a0)		; h2
	lea	p3y,a1
	move.w	2(a1,d1.w),ZWY(a0)		; y2
	move.l	d2,d0				; the registers of F3SegAsm
	move.l	d6,d1				; the plane word
	move.l	d7,d2
	bsr	F3Run
	bra	.e
.edone:	tst.l	ptReal
	bne.s	.pr
	move.l	ptPtr,ptReal
.pr:	lea	24(sp),sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts

; ---- void F3ProjectAsm(int i): F3Project of flight_core.c (exact): the vertex i is projected once (vpdone), x and y with Proj88s (the 8.8 perspective divide), the roll and pitch
;   of the horizon added to the row.  Keeps d2 .. d7 / a2 .. a6.   Proj88s here: d0 = num, d1 = z  ->  d0 = scale * num / z in 8.8 (d2 / d3 are scratch)
F3ProjectAsm:
	move.l	4(sp),d0
	lsl.w	#2,d0				; 4 * i
	movea.w	d0,a1
	lea	vt+VPDONE,a0
	tst.l	(a0,a1.w)
	bne	.pjret
	move.l	#1,(a0,a1.w)
	movem.l	d2-d3,-(sp)
	lea	vt+VX16+4,a0
	move.l	(a0,a1.w),d0			; vx16[i + 1]
	lea	vt+VZ16+4,a0
	move.l	(a0,a1.w),d1			; vz16[i + 1]
	moveq	#0,d2
	bsr	.pj88				; (scale F3SX)
	add.l	#160<<8,d0			; xs
	move.l	d0,-(sp)
	lea	vt+VDY16,a0
	move.l	(a0,a1.w),d0			; vdy16[i]
	lea	vt+VZ16+4,a0
	move.l	(a0,a1.w),d1
	bsr	.pj88
	add.l	#F3Y0<<8,d0			; ys
	move.l	(sp)+,d1			; xs
	move.l	d1,d2
	bpl.s	.px1
	moveq	#0,d2
	bra.s	.px2
.px1:	cmp.l	#319<<8,d2
	ble.s	.px2
	move.l	#319<<8,d2
.px2:	sub.l	#160<<8,d2
	asr.l	#1,d2
	muls.w	f3RollS,d2			; roll / 4096 rows per column
	asr.l	#8,d2
	asr.l	#3,d2				; >> 11
	add.l	f3PitchR,d2
	add.l	d2,d0
	asr.l	#8,d0				; ys
	asr.l	#8,d1				; xs
	cmp.l	#16000,d1
	ble.s	.pc1
	move.l	#16000,d1
	bra.s	.pc2
.pc1:	cmp.l	#-16000,d1
	bge.s	.pc2
	move.l	#-16000,d1
.pc2:	cmp.l	#16000,d0
	ble.s	.pc3
	move.l	#16000,d0
	bra.s	.pc4
.pc3:	cmp.l	#-16000,d0
	bge.s	.pc4
	move.l	#-16000,d0
.pc4:	lea	p3x,a0
	move.l	d1,(a0,a1.w)
	lea	p3y,a0
	move.l	d0,(a0,a1.w)
	movem.l	(sp)+,d2-d3
.pjret:	rts

; Proj88s: the scale is F3SX for the first call (d2 = 0) and F3SY for the second; both are 183, one routine
.pj88:	move.l	d0,d2				; the sign of num
	bpl.s	.pa
	neg.l	d0
.pa:	cmp.l	#1,d1
	bge.s	.pb
	moveq	#1,d1
.pb:	move.l	d1,d3
	lsl.l	#7,d3
	cmp.l	d3,d0
	blt.s	.pok
	move.l	#16384<<8,d0			; num >= z << 7: the game's overflow
	bra.s	.psgn
.pok:	cmp.l	#32767,d1
	ble.s	.pn
.psh:	lsr.l	#1,d1				; the divisor must fit 15 bits
	lsr.l	#1,d0
	cmp.l	#32767,d1
	bgt.s	.psh
.pn:	move.l	d0,d3				; n * 183 (n < 2^22): high word * 183 << 16 + low word * 183
	swap	d3
	mulu.w	#F3SX,d3
	swap	d3
	mulu.w	#F3SX,d0
	add.l	d3,d0
	divu.w	d1,d0				; low word = the integer part, high word = the remainder
	move.l	d0,d3
	swap	d3
	andi.l	#$FFFF,d3
	lsl.l	#8,d3				; remainder << 8
	divu.w	d1,d3				; low word = the 8 bit fraction
	andi.l	#$FFFF,d0
	lsl.l	#8,d0
	andi.l	#$FFFF,d3
	add.l	d3,d0
.psgn:	tst.l	d2
	bpl.s	.pr
	neg.l	d0
.pr:	rts

