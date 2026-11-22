; st_finish_uw.s (the underwater variant of st_finish.s: a vertical strip wave and the rows above the work plane) - the parts of a frame that the Amiga does with the blitter, on the 68000 CPU.  vasm, Motorola syntax.
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
	xref	fStripOff
; The work plane is read from row -4 (the water ripple of the plot puts pixels up to 4 rows above row 0 and below the last row, they feed the drip).  The strip wave: every pair of columns (a strip
; 32 pixels wide) reads its rows with its own offset (fStripOff[strip], a short, in bytes = rows * 40, -80 .. +80, all 0 = no strip wave): output row r shows the picture row r + e of that strip, and
; the drip includes the rows that are skipped (e > 0).  The caller's plane has five spare zero rows in front of it.

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
	movem.l	d2-d6/a2-a4,-(sp)	; 32 bytes + the return address
	move.l	36(sp),a2		; work
	move.l	40(sp),a3		; scr
	lea	fStripOff,a4
	moveq	#9,d4			; 10 pairs of word columns: the work plane is read as longs (two columns), the screen written as longs (plane 0 word + plane 1 word)
.col:
	move.w	(a4)+,d6		; the row offset of this strip in bytes (e * 40)
	move.l	a3,a1
	moveq	#0,d0			; the ground above the first row shown: the rows -4 .. e
	move.w	d6,d5
	ext.l	d5
	divs	#40,d5			; e
	addq.w	#4,d5			; e + 4 = the number of rows - 1
	lea	-160(a2),a0
.o:	or.l	(a0),d0
	lea	40(a0),a0
	dbf	d5,.o
	lea	40(a2,d6.w),a0		; row 1 + e
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
	movem.l	(sp)+,d2-d6/a2-a4
	rts
