; flight_edge.asm - DrawEdge of the fractal flight (LB24D of the game, DrawEdge of flight_core.c) with the exact cull test (CullTest) and the midpoint
; with the fractal displacement (Mid3A7) for the 68000.  vasm, Motorola syntax.
;
;   void DrawEdgeAsm(int x1, int y1, int h1, int x2, int y2, int h2);       (C calling convention: arguments on the stack, d2-d7/a2-a6 are saved here)
;
;   One projected edge from end 1 (x1, y1, h1) to end 2 (x2, y2, h2), x1 < x2 (the caller drops back-facing edges): it is culled when it can't draw anything,
;   otherwise subdivided in 16 bits (bisected at the midpoint with the fractal displacement) until the pieces are either entirely below the clip line
;   (skipped), or narrow enough to be handed to the byte routine: Line51CCore (flight_line.asm) for the common case, SetPixelLineC (C) when end 1 of the piece is
;   left of the view or the piece is one column wide.  The 16 bit stack is fSX / fSY / fSH (words, 16 entries); the byte stacks are in fZ.
;
;   registers:  d0 = end 1 x (long, sign extended)   d1 = end 1 y (long)   d2 = end 1 h (0..255)   d3 = 2 * xi (index into the word stacks)
;               d4 = bisection counter (starts at $14)   d5..d7 temporaries
;               a0 = fSX  a1 = fSY  a2 = fSH  a3 = fCfg {colHi.b, yTop.b, stride.w, colLo.b, yClip.b}  a4 = fLineBuf  a5, a6 temporaries

	section	.text,code

	xref	fSX
	xref	fSY
	xref	fSH
	xref	fZ
	xref	fCfg
	xref	fLineBuf
	xref	z82
	xref	z84
	xref	z86
	xref	SetPixelLineC
	xref	Line51CCore
	xdef	DrawEdgeAsm
	xdef	DrawEdgeBody

ZY	equ	48
ZH	equ	96

DrawEdgeAsm:
	movem.l	d2-d7/a2-a6,-(sp)		; 11 registers = 44 bytes, + the return address
	lea	fSX,a0
	lea	fSY,a1
	lea	fSH,a2
	move.l	48(sp),d0			; x1
	move.l	52(sp),d1			; y1
	move.l	56(sp),d2			; h1
	move.w	62(sp),(a0)			; sx[0] = x2  (the low word of the int)
	move.w	66(sp),(a1)			; sy[0] = y2
	move.w	70(sp),(a2)			; sh[0] = h2
	bsr	DrawEdgeBody
	movem.l	(sp)+,d2-d7/a2-a6
	rts

; DrawEdgeBody: the edge with end 1 = d0 (x1, long), d1 (y1, long), d2 (h1) and end 2 in fSX[0], fSY[0], fSH[0] (words).  Clobbers every register except sp (RenderPlotAsm in st_vtab.s calls it for all edges
; of a picture with no save and restore around it).
DrawEdgeBody:
	lea	fSX,a0
	lea	fSY,a1
	lea	fSH,a2
	lea	fCfg,a3
	lea	fLineBuf,a4
	moveq	#0,d3				; xi = 0
	move.w	(a0),d5
	ext.l	d5
	cmp.l	d5,d0
	bge	.ret				; e1x >= sx[0]: a back-facing or folded edge
	bsr	.cull
	tst.w	d5
	bne	.ret				; nothing of the edge can be plotted
	moveq	#$14,d4				; at most $14 bisections

.main:
	tst.l	d0
	bmi	.left				; end 1 left of the screen

	; ---- end 1 is on the screen (or right of it)
	cmp.l	#$D8,d0
	bge	.ret				; x >= 216 (also >= 256): nothing to draw
.classify:
	move.w	(a0,d3.w),d5			; sx[xi]
	cmp.w	#255,d5
	bhi	.dosub				; (sx >> 8) != 0: the end is more than 255 columns away (or left of 0): bisect
	move.b	d5,d7
	sub.b	d0,d7				; xw = (sx[xi] - e1x) & $FF
	moveq	#0,d6
	move.b	5(a3),d6			; the clip line
	cmp.l	d6,d1
	bge	.e1ge				; e1y >= clip
	move.w	(a1,d3.w),d5			; e1y < clip
	ext.l	d5
	cmp.l	d6,d5
	blt	.skip				; both ends below the clip line: skip the piece
	cmp.b	#$14,d7
	blo	.draw
	moveq	#0,d5
	move.b	d7,d5
	lsr.w	#2,d5				; b6 = xw >> 2
	cmp.l	d5,d1
	blt	.dosub				; e1y - b6 < 0: subdivide
	bra	.draw
.e1ge:
	move.w	(a1,d3.w),d5
	ext.l	d5				; sy[xi]
	cmp.l	d6,d5
	bge	.draw
	cmp.b	#$14,d7
	blo	.draw
	moveq	#0,d6
	move.b	d7,d6
	lsr.w	#2,d6				; b6
	cmp.l	d6,d5
	blt	.dosub				; sy[xi] - b6 < 0: subdivide
	bra	.draw

.dosub:
	subq.w	#1,d4
	bmi	.ret
	bsr	.mid				; d5 = mx, d6 = my, d7 = mh
	addq.w	#2,d3
	move.w	d5,(a0,d3.w)			; push the midpoint
	move.w	d6,(a1,d3.w)
	move.w	d7,(a2,d3.w)
	cmp.w	#$1E,d3
	bhs	.ret				; xi >= 15
	bra	.classify

	; ---- draw the piece: ZX/ZY/ZH[0] = end 2, z82/z84/z86 = end 1, then set_pixel_line
.draw:
	lea	fZ,a5
	move.w	(a0,d3.w),d5
	move.b	d5,(a5)				; ZX[0] = sx[xi] & $FF
	move.w	(a1,d3.w),d5
	ext.l	d5
	tst.l	d5
	bpl	.z1
	moveq	#0,d5
	bra	.z2
.z1:	cmp.l	#255,d5
	ble	.z2
	move.w	#255,d5
.z2:	move.b	d5,ZY(a5)			; ZY[0] = Clamp8(sy[xi])
	move.w	(a2,d3.w),d5
	move.b	d5,ZH(a5)			; ZH[0] = sh[xi]
	move.l	d0,d5
	andi.l	#$FF,d5				; z82 = e1x & $FF
	move.l	d1,d6
	tst.l	d6
	bpl	.z3
	moveq	#0,d6
	bra	.z4
.z3:	cmp.l	#255,d6
	ble	.z4
	move.l	#255,d6
.z4:						; d6 = z84 = Clamp8(e1y)
	moveq	#0,d7
	move.b	(a5),d7				; a = ZX[0]
	cmp.b	4(a3),d7
	bls	.skip				; a <= the first column: nothing
	cmp.b	d5,d7
	blo	.skip				; a < z82: nothing
	beq	.slow				; the same column (one pixel): the C routine
	cmp.b	4(a3),d5
	blo	.slow				; end 1 left of the first column: the C routine bisects it
	movem.l	d3-d4,-(sp)			; the common case: the byte line routine, X = z82, yi = 0; it clobbers everything: only xi and the counter are still needed
	move.l	d2,d3				; z86 = e1h
	move.l	d6,d2				; z84
	move.l	d5,d0				; X = z82
	moveq	#0,d1
	jsr	Line51CCore
	movem.l	(sp)+,d3-d4
	lea	fSX,a0				; the stacks and the config: the pop and the classification need them (d0 .. d2 are loaded by the pop)
	lea	fSY,a1
	lea	fSH,a2
	lea	fCfg,a3
	bra	.skip
.slow:
	move.l	d5,z82
	move.l	d6,z84
	move.l	d2,z86
	movem.l	d0-d1/a0-a1,-(sp)		; the C function may change the caller-saved registers
	jsr	SetPixelLineC
	movem.l	(sp)+,d0-d1/a0-a1

	; ---- pop: end 1 = the stack top
.skip:
	tst.w	d3
	beq	.ret				; xi == 0: done
	move.w	(a0,d3.w),d0
	ext.l	d0
	move.w	(a1,d3.w),d1
	ext.l	d1
	move.w	(a2,d3.w),d2
	subq.w	#2,d3
	bra	.main

	; ---- end 1 is left of the screen: bisect, keep the right part
.left:
	subq.w	#1,d4
	bmi	.ret
	bsr	.mid
	cmp.l	#$28,d5
	bge	.lpush				; the midpoint is not left of column 40: push it
	move.l	d5,d0				; else it becomes the new end 1
	move.l	d6,d1
	move.w	d7,d2
	bra	.main
.lpush:
	addq.w	#2,d3
	move.w	d5,(a0,d3.w)
	move.w	d6,(a1,d3.w)
	move.w	d7,(a2,d3.w)
	cmp.w	#$1E,d3
	bhs	.ret
	bra	.main

.ret:
	rts

; ---- Mid3A7: the midpoint of end 1 (d0, d1, d2) and the stack entry d3, with the fractal displacement:  d5 = mx, d6 = my (long), d7 = mh (0..255)
.mid:
	move.l	d4,-(sp)
	move.w	(a0,d3.w),d5
	ext.l	d5
	add.l	d0,d5
	addq.l	#1,d5
	asr.l	#1,d5				; mx = (e1x + sx + 1) >> 1
	move.w	(a1,d3.w),d6
	ext.l	d6
	add.l	d1,d6
	addq.l	#1,d6
	asr.l	#1,d6				; my = (e1y + sy + 1) >> 1
	move.w	(a2,d3.w),d7
	add.w	d2,d7
	addq.w	#1,d7				; sum = e1h + sh + 1
	btst	#7,d7
	beq	.midend				; mh < $80: no displacement
	move.l	d5,d4
	sub.l	d0,d4
	asr.l	#1,d4				; d = (mx - e1x) >> 1
	cmp.w	#255,d7
	bhi	.midp
	sub.l	d4,d6				; my - d
	bra	.midw
.midp:
	add.l	d4,d6				; my + d
.midw:
	ext.l	d6				; wrap to a signed 16 bit value
.midend:
	andi.w	#$FF,d7				; mh = sum & $FF
	move.l	(sp)+,d4
	rts

; ---- the exact cull test: d5 = 1 if no pixel of the edge can be plotted (see CullTest in flight_core.c), else 0.  Clobbers d4, d5, d6, d7, a5.
.cull:
	cmp.l	#255,d0
	bgt	.cno				; end 1 right of column 255
	moveq	#0,d7
	move.b	4(a3),d7			; the first column
	move.l	d0,d6
	cmp.l	d7,d6
	bge	.c0
	move.l	d7,d6				; c0 = max(x1, first column)
.c0:
	move.w	(a0),d5
	ext.l	d5				; x2
	bmi	.cno				; end 2 left of the view
	movea.l	d5,a5				; keep x2
	moveq	#0,d7
	move.b	(a3),d7				; colHi
	cmp.l	d7,d5
	blt	.c1
	move.l	d7,d5
	subq.l	#1,d5				; c1 = min(x2, colHi - 1)
.c1:
	cmp.l	d6,d5
	blt	.cno				; c1 < c0
	move.w	(a1),d4
	ext.l	d4				; y2
	cmp.l	d1,d4
	bge	.ct
	move.l	d1,d4				; ytop = max(y1, y2)
.ct:
	cmp.l	#256,d4
	bge	.cno
	move.l	a5,d7
	sub.l	d0,d7
	asr.l	#1,d7
	add.l	d7,d4
	addq.l	#3,d4				; ytop += ((x2 - x1) >> 1) + 3
	bmi	.cyes				; below zero: nothing is above the floor
	cmp.l	#256,d4
	bge	.cno
	move.b	(a4,d6.w),d7
	cmp.b	d4,d7
	blo	.cno				; fLineBuf[c0] < ytop
	move.b	(a4,d5.w),d7
	cmp.b	d4,d7
	blo	.cno				; fLineBuf[c1] < ytop
	move.w	d6,d7
	add.w	d5,d7
	lsr.w	#1,d7
	move.b	(a4,d7.w),d7
	cmp.b	d4,d7
	blo	.cno				; fLineBuf[(c0 + c1) / 2] < ytop
	lea	(a4,d6.w),a5			; every column c0 .. c1
	move.w	d5,d7
	sub.w	d6,d7				; c1 - c0
.cs:
	move.b	(a5)+,d5
	cmp.b	d4,d5
	blo	.cno
	dbra	d7,.cs
.cyes:
	moveq	#1,d5
	rts
.cno:
	moveq	#0,d5
	rts
