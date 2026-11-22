; st_vtab.s - the vertex walk of UpdateTables (flight_core.c) in assembly, for the 68000.  vasm, Motorola syntax.
;
;   void UpdateWalkAsm(int s, int c, const unsigned char *walk, int b7, int xc, int alt);        (C calling convention: arguments on the stack, d2-d7/a2-a6 are saved here)
;
; The 55 steps of the vertex fan (F_NV = 55): from the vertex y (camera space X = vx16[y], Z = vz16[y], 16 times the game's units) to the vertex y + 1 by one step of the walk table (a row
; step of +-1 cell and / or a column step of +-1 cell, rotated by the heading: s = sin, c = cos, 16 times the game's units), then for every vertex:
;     vx16 / vz16[y + 1], the game units vx / vz[y + 1] = (short)(X >> 4),  vh[y] = the height of the map cell,  vdy16[y] = h * 256 - alt (the height above the camera in 1/256 height units),
;     vflag[y] = $80 (Z < 96: too near), else $40 / $20 (outside the view to the left / right: |X| >= 27/32 Z), else 0.
; The arrays are the members of the struct vt (flight_core.c: struct VTab, the offsets below are checked by _Static_assert there).  Only for F_NARROW = 1, F_WIDE = 1.
;
;   registers:  d0 = X  d1 = Z (running)   d2 = s  d3 = c   d4, d7 = temporaries   d5 = b7 (0 .. 255)   d6 = xc (0 .. 255)
;               a0 = the walk table pointer  a1 = &vt + 4 * y  a2 = heightmap  a3 = alt  a5 = end of the walk table

	section	.text,code

	xref	vt
	xref	heightmap
	xref	fPitchBase
	xref	fRollS
	xref	fFlagLimMask
	xdef	UpdateWalkAsm
	xdef	ProjectVertexAsm
	xdef	RenderPlotAsm
	ifd	UW
	xref	fWobTab
	xref	fWobPh
	endc
	xref	DrawEdgeBody
	xref	fEdgesFar
	xref	fSX
	xref	fSY
	xref	fSH

NV		equ	55
O_VX16		equ	0
O_VZ16		equ	(NV+1)*4
O_VX		equ	2*(NV+1)*4
O_VZ		equ	3*(NV+1)*4
O_VDY16		equ	4*(NV+1)*4
O_VH		equ	4*(NV+1)*4+NV*4
O_VFLAG		equ	4*(NV+1)*4+2*NV*4
O_VPX		equ	4*(NV+1)*4+3*NV*4
O_VPY		equ	4*(NV+1)*4+4*NV*4
O_VPDONE	equ	4*(NV+1)*4+5*NV*4

UpdateWalkAsm:
	movem.l	d2-d7/a2-a6,-(sp)		; 11 registers = 44 bytes, + the return address
	move.l	48(sp),d2			; s
	move.l	52(sp),d3			; c
	movea.l	56(sp),a0			; walk
	move.l	60(sp),d5			; b7
	move.l	64(sp),d6			; xc
	move.l	68(sp),a3			; alt
	lea	vt,a1
	lea	heightmap,a2
	lea	NV(a0),a5			; end of the walk steps
	move.l	O_VX16(a1),d0			; vx16[0]
	move.l	O_VZ16(a1),d1			; vz16[0]

.v:	move.b	(a0)+,d4			; b
	bpl.s	.n80
	; ---- b & $80: row -1
	add.l	d2,d0				; X += s
	sub.l	d3,d1				; Z -= c
	add.b	#$F0,d5				; b7 = (b7 + $F0) & $FF
	add.b	d4,d4				; r0 = b & $40 -> bit 7, r1 = b & $20 -> bit 6
	bra.s	.col
.n80:	btst	#6,d4
	beq.s	.n40
	; ---- b & $40: row +1
	sub.l	d2,d0				; X -= s
	add.l	d3,d1				; Z += c
	add.b	#$10,d5				; b7 = (b7 + $10) & $FF
	add.b	d4,d4
	add.b	d4,d4				; r0 = b & $20 -> bit 7, r1 = b & $10 -> bit 6
	bra.s	.col
.n40:	add.b	d4,d4
	add.b	d4,d4				; no row step: the same bits
.col:	tst.b	d4
	bpl.s	.nr0
	; ---- r0: column -1
	sub.l	d3,d0				; X -= c
	sub.l	d2,d1				; Z -= s
	subq.b	#1,d6				; xc = (xc - 1) & $FF
	bra.s	.cd
.nr0:	btst	#6,d4
	beq.s	.cd
	; ---- r1: column +1
	add.l	d3,d0				; X += c
	add.l	d2,d1				; Z += s
	addq.b	#1,d6				; xc = (xc + 1) & $FF
.cd:	move.l	d0,O_VX16+4(a1)
	move.l	d1,O_VZ16+4(a1)
	move.l	d0,d7
	asr.l	#4,d7
	ext.l	d7				; W16(X >> 4)
	move.l	d7,O_VX+4(a1)
	move.l	d1,d7
	asr.l	#4,d7
	ext.l	d7
	move.l	d7,O_VZ+4(a1)
	; ---- the map cell and its height
	moveq	#15,d4
	and.b	d6,d4				; xc & $0F
	or.b	d5,d4				; idxh = b7 | (xc & $0F)
	moveq	#0,d7
	move.b	(a2,d4.w),d7			; h = heightmap[idxh]
	move.l	d7,O_VH(a1)
	lsl.l	#8,d7
	sub.l	a3,d7				; v = h * 256 - alt
	move.l	d7,O_VDY16(a1)
	; ---- the frustum flag
	moveq	#0,d7				; fl = 0
	cmp.l	#96,d1
	blt.s	.near				; Z < 96: $80
	move.l	d1,d4
	asr.l	#3,d4				; Z >> 3
	move.l	d1,d7
	asr.l	#5,d7				; Z >> 5
	and.l	fFlagLimMask,d7			; (0 in flight320: the limit is 7/8 Z, see flight_core.c)
	add.l	d7,d4
	move.l	d1,d7
	sub.l	d4,d7				; lim = Z - (Z >> 3) - (Z >> 5)
	move.l	d0,d4
	bmi.s	.xneg
	cmp.l	d7,d0				; X >= 0
	blt.s	.f0				; X < lim: 0
	moveq	#$20,d4
	bra.s	.fl
.xneg:	neg.l	d4				; X < 0: -X
	cmp.l	d7,d4
	blt.s	.f0				; -X < lim: 0
	moveq	#$40,d4
	bra.s	.fl
.f0:	moveq	#0,d4
	bra.s	.fl
.near:	move.l	#$80,d4
.fl:	move.l	d4,O_VFLAG(a1)
	addq.l	#4,a1
	cmpa.l	a5,a0
	bne	.v
	movem.l	(sp)+,d2-d7/a2-a6
	rts

; void ProjectVertexAsm(int i): ProjectVertex of flight_core.c (the wide version): the vertex i of the camera tables -> the column vpx[i] and the row vpy[i] (rounded down to whole pixels from 8.8),
; once per picture (vpdone).  Proj88: 96 * num / z in 8.8 with two 16 bit divisions (the whole part and an 8 bit fraction), |num| >= 128 z gives +- 16384 pixels; the horizon ramp is exact
; (the pitch and the roll per column), the roll multiplication a 16 x 16 bit muls.w.  Bit for bit the same as the C function.
;   registers:  d1 = z normalised below 2^15  d2 = z << 7 (the overflow limit)  d4 = the shift count  d3 = the x offset  d0 = the y offset  d5 = sign  d6, d7 temporaries  a1 = &vt + 4 * i
ProjectVertexAsm:
	move.l	4(sp),d0			; i
	add.w	d0,d0
	add.w	d0,d0				; 4 * i
	lea	vt,a1
	adda.l	d0,a1				; &vt + 4 * i: the members are at constant displacements
ProjectA1:					; the same with a1 = &vt + 4 * i (RenderPlotAsm); clobbers d0, d1, a1
	tst.l	O_VPDONE(a1)
	bne	.pdone
	move.l	#1,O_VPDONE(a1)
	movem.l	d2-d7,-(sp)
	move.l	O_VX16+4(a1),d3		; X
	move.l	O_VZ16+4(a1),d1		; Z
	move.l	O_VDY16(a1),d7		; dy
	move.l	d1,d2
	lsl.l	#7,d2				; z << 7
	moveq	#0,d4				; shift count
	cmp.l	#32767,d1
	ble	.pn
.psh:	lsr.l	#1,d1
	addq.b	#1,d4
	cmp.l	#32767,d1
	bgt	.psh
.pn:	move.l	d3,d0				; x
	bsr	.proj
	move.l	d0,d3				; the x offset
	move.l	d7,d0				; dy
	bsr	.proj				; d0 = the y offset
	add.l	#$8000,d3			; xs = x offset + 128 pixels
	add.l	#$8000,d0			; ys
	move.l	d3,d4
	bpl	.pc1
	moveq	#0,d4
.pc1:	cmp.l	#$FF00,d4
	ble	.pc2
	move.l	#$FF00,d4			; xr = xs limited to 0 .. 255 columns
.pc2:	sub.l	#$8000,d4			; xr - 128 columns: fits a signed 16 bit word
	muls.w	fRollS+2,d4			; * roll
	lsl.l	#4,d4
	swap	d4
	ext.l	d4				; >> 12
	add.l	fPitchBase,d4			; the ramp r of this column
	add.l	d4,d0				; ys + r
	ifd	UW
	move.l	d3,d4				; the underwater wobble: a sine of the column and the time moves every vertex up and down (8.8 rows)
	lsr.l	#7,d4				; 2 * column
	add.l	fWobPh,d4
	andi.w	#63,d4
	add.w	d4,d4
	lea	fWobTab,a0
	move.w	(a0,d4.w),d4
	ext.l	d4
	add.l	d4,d0
	endc
	asr.l	#8,d0
	asr.l	#8,d3
	move.l	d3,O_VPX(a1)
	move.l	d0,O_VPY(a1)
	movem.l	(sp)+,d2-d7
.pdone:	rts

.proj:	tst.l	d0
	smi	d5
	bpl	.pp
	neg.l	d0
.pp:	cmp.l	d2,d0
	bge	.povf				; |num| >= 128 z: the game's overflow
	lsr.l	d4,d0				; n = |num| >> shift
	move.l	d0,d6
	add.l	d0,d0
	add.l	d6,d0				; 3 n
	lsl.l	#5,d0				; 96 n
	divu.w	d1,d0				; low word = the whole part, high word = the remainder
	move.l	d0,d6
	swap	d6
	andi.l	#$FFFF,d6
	lsl.l	#8,d6
	divu.w	d1,d6				; low word = the 8 bit fraction
	andi.l	#$FFFF,d0
	lsl.l	#8,d0
	andi.l	#$FFFF,d6
	add.l	d6,d0
	tst.b	d5
	beq	.pr
	neg.l	d0
.pr:	rts
.povf:	move.l	#4194304,d0			; 16384 pixels in 8.8
	tst.b	d5
	beq	.pr
	neg.l	d0
	rts

; void RenderPlotAsm(void): the edge loop of RenderPlot (flight_core.c) for the 90 edges of fEdgesFar, with no save and restore of registers per edge: an edge is drawn when neither end is too near / outside
; the view on the wrong side (vflag), after both ends are projected (once per picture each) and x1 < x2.  DrawEdgeBody (flight_edge.asm) clobbers every register; the loop state is on the stack:
; (sp).w = the number of edges left - 1, 2(sp).l = the pointer to the next edge's vertex pair.   Not for the depth band variants (they classify each edge: RenderPlot of the C code).
;   registers:  a2 = 4 * a  a3 = 4 * b  a4 = &vpy  a0 = &vh  a5 = &vpx  d0 = x1  d1 = y1  d2 = h1  d3 = x2
RenderPlotAsm:
	movem.l	d2-d7/a2-a6,-(sp)
	pea	fEdgesFar
	move.w	#89,-(sp)
.e:	movea.l	2(sp),a6
	moveq	#0,d0
	moveq	#0,d1
	move.b	(a6)+,d0			; a
	move.b	(a6)+,d1			; b
	move.l	a6,2(sp)
	add.w	d0,d0
	add.w	d0,d0				; 4 * a
	add.w	d1,d1
	add.w	d1,d1				; 4 * b
	lea	vt+O_VFLAG+3,a5
	move.b	(a5,d0.w),d2			; vflag[a] (the low byte of the int)
	andi.b	#$A0,d2				; too near, or outside to the left
	bne	.next
	move.b	(a5,d1.w),d3
	andi.b	#$C0,d3				; too near, or outside to the right
	bne	.next
	movea.w	d0,a2
	movea.w	d1,a3
	lea	vt,a1
	adda.l	a2,a1
	bsr	ProjectA1
	lea	vt,a1
	adda.l	a3,a1
	bsr	ProjectA1
	lea	vt+O_VPX,a5
	lea	vt+O_VPY,a4
	lea	vt+O_VH,a0
	move.l	a2,d4
	move.l	a3,d5
	move.l	(a5,d4.w),d0			; x1 = vpx[a]
	move.l	(a5,d5.w),d3			; x2 = vpx[b]
	cmp.l	d3,d0
	bge	.next				; x1 >= x2: back-facing or folded
	move.l	(a4,d4.w),d1			; y1
	move.l	(a0,d4.w),d2			; h1
	move.w	d3,fSX				; end 2: the low words
	move.l	(a4,d5.w),d6
	move.w	d6,fSY
	move.l	(a0,d5.w),d6
	move.w	d6,fSH
	bsr	DrawEdgeBody
.next:	subq.w	#1,(sp)
	bpl	.e
	addq.l	#6,sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts
