; st_finish.s - the parts of a frame that the Amiga does with the blitter, on the 68000 CPU.  vasm, Motorola syntax.
;
;   void ClearWork(u8 *work);                       work = 3200 bytes (80 rows of 40 bytes) := 0
;   void FinishFrame(const u8 *work, u8 *scr);      work plane (ridge pixels, 1 row per picture row) -> two screen lines per picture row
;
; Ground = every ridge pixel dripped downwards (g(row) = g(row-1) | line(row)).  Screen colour index (plane1, plane0):
;   00 sky   10 ground   11 ridge line   01 border / cockpit bar (written once at start-up).
; So plane 0 = line pixels, plane 1 = ground, planes 2 and 3 stay zero.  Row 0 of the work plane is above the window (only feeds the drip),
; rows 1 .. 75 are written to scr (the first of 150 lines = row 1); rows 76 .. 79 (cockpit bar) are not shown.
; The screen is interleaved: 20 groups of 4 words per line, 160 bytes per line.

	section	.text,code
	xdef	ClearWork
	xdef	FinishFrame

ClearWork:
	move.l	4(sp),a0
	lea	3200(a0),a0
	movem.l	d2-d7/a2-a6,-(sp)
	moveq	#0,d0
	moveq	#0,d1
	moveq	#0,d2
	moveq	#0,d3
	moveq	#0,d4
	moveq	#0,d5
	moveq	#0,d6
	moveq	#0,d7
	move.l	d0,a1
	move.l	d0,a2
	move.l	d0,a3
	move.l	d0,a4
	move.l	d0,a5
	move.l	d0,a6
	moveq	#60,d0			; 61 * 52 bytes
	moveq	#0,d1
.cl:	movem.l	d1-d7/a1-a6,-(a0)	; 13 longs = 52 bytes
	dbf	d0,.cl
	movem.l	d1-d7,-(a0)		; 3200 - 61 * 52 = 28 bytes
	movem.l	(sp)+,d2-d7/a2-a6
	rts

FinishFrame:
	movem.l	d2-d5/a2-a3,-(sp)	; 24 bytes + the return address
	move.l	28(sp),a2		; work
	move.l	32(sp),a3		; scr
	moveq	#9,d4			; 10 pairs of word columns: the work plane is read as longs (two columns), the screen written as longs (plane 0 word + plane 1 word)
.col:
	move.l	a2,a0
	move.l	a3,a1
	move.l	(a0),d0			; row 0: ground = its line pixels (d0 = ground word of column c in the high half, column c+1 in the low half)
	lea	40(a0),a0
	moveq	#14,d5			; 15 groups of 5 rows = rows 1 .. 75
.grp:
k	set	0
	rept	5
	move.l	40*k(a0),d1		; line pixels of the row: column c high, column c+1 low
	or.l	d1,d0			; ground := ground above OR line
	move.l	d0,d2
	swap	d2			; low word = ground of column c
	move.l	d1,d3
	move.w	d2,d3			; d3 = line c : ground c    (plane 0 word : plane 1 word of the group of column c)
	swap	d1
	move.w	d0,d1			; d1 = line c+1 : ground c+1
	move.l	d3,320*k(a1)		; screen line 1, column c
	move.l	d1,320*k+8(a1)		; screen line 1, column c+1
	move.l	d3,320*k+160(a1)	; screen line 2 (the picture is doubled vertically)
	move.l	d1,320*k+168(a1)
k	set	k+1
	endr
	lea	200(a0),a0
	lea	1600(a1),a1
	dbf	d5,.grp
	addq.l	#4,a2
	lea	16(a3),a3
	dbf	d4,.col
	movem.l	(sp)+,d2-d5/a2-a3
	rts
