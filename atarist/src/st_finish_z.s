; st_finish_z.s - the depth band variant of st_finish.s (flightz).  vasm, Motorola syntax.
;
;   void ClearWorkZ(u8 *work);                                  work = 5 planes of 3200 bytes (80 rows of 40 bytes) := 0
;   void FinishFrameZ(const u8 *work, u8 *scr, const u32 *masks);
;
; The ridge pixels of the edges are drawn into 5 planes, one per depth class (plane 0 = the nearest cell row, plane 4 = the farthest).  The ground is the OR drip of all of them
; (as before).  The band of a pixel is the nearest depth class whose ridge lies above it in its column: because the edges are drawn from near to far and a farther pixel is only plotted
; above the nearer ones, the classes in a column come in depth order, so   T(k) = drip of the ridge pixels of the classes 0 .. k   is a plain OR drip (T(k) is a subset of T(k+1)).
;   band k   =  T(k) and not T(k-1)         ground = T(4)         line pixels = the OR of the 5 planes
; Every band is a fixed ordered dither (Bayer 4 x 4, single screen pixels like the background) of two ground tones: the near tone (colour class 10) and the far tone (colour class 01): the band 0 (nearest) 0 % far tone,
; then 25 %, 50 %, 75 %, the band 4 (farthest) 100 %.  masks = 4 x 3 longs (picture row & 3: the 25 %, 50 %, 75 % pattern of that row, the same word in both halves).
; Rows 1 .. 75 are written (two screen lines each, the picture is doubled vertically), row 0 only feeds the drip.  Two word columns at a time (longs).

	section	.text,code
	xdef	ClearWorkZ
	xdef	FinishFrameZ

ClearWorkZ:
	move.l	4(sp),a0
	lea	16000(a0),a0
	movem.l	d2-d7/a2-a6,-(sp)
	moveq	#0,d1
	moveq	#0,d2
	moveq	#0,d3
	moveq	#0,d4
	moveq	#0,d5
	moveq	#0,d6
	moveq	#0,d7
	move.l	d1,a1
	move.l	d1,a2
	move.l	d1,a3
	move.l	d1,a4
	move.l	d1,a5
	move.l	d1,a6
	move.w	#306,d0				; 307 * 52 bytes
.cl:	movem.l	d1-d7/a1-a6,-(a0)		; 13 longs = 52 bytes
	dbf	d0,.cl
	movem.l	d1-d7/a1-a2,-(a0)		; 16000 - 307 * 52 = 36 bytes
	movem.l	(sp)+,d2-d7/a2-a6
	rts

; the dither at single screen pixel resolution, like the background: every screen line has its own Bayer 4 x 4 row, so the two lines of a picture row get different patterns
; (masks[(screen line) & 3]: the 25 %, 50 %, 75 % pattern of that line, the same word in both halves).  Picture row r starts at the screen line 25 + 2 (r - 1): odd rows use the mask
; lines 1 and 2, even rows 3 and 0.

LINEZ	macro					; \1 = mask line (0 .. 3), \2 = line offset (0 or 160), \3 = row in the group of 4.  in: d4 = ground, d5 = line pixels, d0 .. d3 = drip states
	move.l	d4,d6
	eor.l	d3,d6				; band 4: 100 % far tone
	move.l	d3,d7
	eor.l	d2,d7				; band 3
	and.l	12*\1+8(a2),d7			; 75 %
	or.l	d7,d6
	move.l	d2,d7
	eor.l	d1,d7				; band 2
	and.l	12*\1+4(a2),d7			; 50 %
	or.l	d7,d6
	move.l	d1,d7
	eor.l	d0,d7				; band 1
	and.l	12*\1(a2),d7			; 25 %
	or.l	d7,d6				; d6 = the far tone pixels
	move.l	d5,d7
	not.l	d7
	and.l	d7,d6				; not the line pixels
	or.l	d6,d5				; d5 = plane 0 : line pixels or far tone pixels
	move.l	d6,d7
	not.l	d7
	and.l	d4,d7				; d7 = plane 1 : ground without the far tone pixels (line pixels stay: they are not in d6)
	move.l	d5,d6
	swap	d7
	move.w	d7,d6				; d6 = first column: plane 0 word : plane 1 word
	swap	d7
	swap	d5
	move.w	d7,d5				; d5 = second column
	move.l	d6,320*\3+\2(a1)
	move.l	d5,320*\3+8+\2(a1)
	endm

ROWZ	macro					; \1 = row in the group of 4 (0 .. 3), \2 / \3 = the mask lines of its two screen lines
	move.l	40*\1(a0),d5			; class 0: ridge pixels of the nearest row
	or.l	d5,d0				; T(0) := T(0) or U(0)
	or.l	3200+40*\1(a0),d5		; U(1) = classes 0 .. 1
	or.l	d5,d1
	or.l	6400+40*\1(a0),d5
	or.l	d5,d2
	or.l	9600+40*\1(a0),d5
	or.l	d5,d3
	or.l	12800+40*\1(a0),d5		; all line pixels
	or.l	d5,d4				; d4 = T(4) = the ground
	movea.l	d5,a6				; keep the line pixels for the second screen line
	LINEZ	\2,0,\1
	move.l	a6,d5
	LINEZ	\3,160,\1
	endm

FinishFrameZ:
	movem.l	d2-d7/a2-a6,-(sp)		; 44 bytes + the return address
	move.l	48(sp),a3			; work
	move.l	52(sp),a4			; scr
	move.l	56(sp),a2			; masks
	lea	40(a3),a0
	move.l	a0,-(sp)			; end of the 10 column pairs (compared with the stack: a6 is a scratch register)
.col:
	move.l	a3,a0
	move.l	a4,a1
	move.l	(a0),d5				; row 0: only the drip states
	move.l	d5,d0
	or.l	3200(a0),d5
	move.l	d5,d1
	or.l	6400(a0),d5
	move.l	d5,d2
	or.l	9600(a0),d5
	move.l	d5,d3
	or.l	12800(a0),d5
	move.l	d5,d4
	lea	40(a0),a0			; row 1
	lea	2880(a0),a5			; 18 groups of 4 rows = rows 1 .. 72
.grp:
	ROWZ	0,1,2				; row 1 + 4 n: odd: the mask lines 1 and 2; the next row (even): 3 and 0
	ROWZ	1,3,0
	ROWZ	2,1,2
	ROWZ	3,3,0
	lea	160(a0),a0
	lea	1280(a1),a1
	cmpa.l	a5,a0
	bne	.grp
	ROWZ	0,1,2				; rows 73, 74, 75
	ROWZ	1,3,0
	ROWZ	2,1,2
	addq.l	#4,a3
	lea	16(a4),a4
	cmpa.l	(sp),a3
	bne	.col
	addq.l	#4,sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts
