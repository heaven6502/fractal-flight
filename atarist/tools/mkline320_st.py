"""mkline320_st.py [KMAX] [out] [H]: writes src/flight_line320_st.asm, the fractal line of flight320 on the Atari ST (F3Seg of flight_core.c) in 68000 assembly, with unrolled kernels for the small pieces.
The same as tools/mkline320.py of the Amiga port except the plot: the screen is the interleaved low resolution screen (4 words per 16 pixels, 160 byte lines), the depth bands: every band draws its ridge pixels into its own plane (plane 0 = near, 1 = middle, 2 = far), plane 3 is always 1.

    python tools/mkline320_st.py 16 src/flight_line320_st.asm 180

    void F3SegAsm(int lit, int x1, int y1, int h1, int x2, int y2, int h2);       (C calling convention: arguments on the stack, d2-d7/a2-a6 are saved here)

The same pieces, in the same order, as F3Seg: a piece (x1, x2) draws the columns x1 .. x2 - 1; the columns are 0 .. 319, the rows (y up) 0 .. 199, words, no byte wrap:
  - a piece entirely left of the screen (x2 <= 0) is skipped, the line ends when x1 >= 320
  - 1 column: plot (x1, y2);  2 columns: plot (x1, (y1 + y2 + 1) >> 1), plot (x1 + 1, y2)
  - 4 or more columns: if max(y1, y2) + n / 2 + 3 is not above the ridge buffer in any of its columns (and <= 255) the piece is skipped (exact)
  - otherwise the midpoint: h = h1 + h2 + 1 (carry c), bit 7 of h set: moved by (m - 1) / 2 down (no carry) or m / 2 up (carry), m = n >> 1, with the rounding term of the parity c2 of y1 + y2
  - a piece of 3 .. KMAX columns that lies completely on the screen is done by an unrolled kernel (no stack, constants as immediates), as in mkline_k.py
The plot: ridge buffer f3LineBuf ($FF = column closed), y >= 200 closes the column and is drawn on row 199; the pixel goes into the plane word d1 (0, 2, 4 = the band); a pixel that covers an older one records it (ptPtr).
  registers:  d0 = X   d1 = the plane word of the band   d2 = y of end 1   d3 = h of end 1 + 1   d4 .. d7 temporaries
              a0 = top of the word stack (X; Y at +100, H at +200)   a1 = temporary   a2 = f3LineBuf   a3 = f3Scr (the top line of the screen)   a4 = f3XTab (per column: byte offset of the group, pixel mask)   a5 = f3RowOff
"""
import sys

KMAX = int(sys.argv[1]) if len(sys.argv) > 1 else 16
OUT = sys.argv[2] if len(sys.argv) > 2 else "src/flight_line320_st.asm"
H = int(sys.argv[3]) if len(sys.argv) > 3 else 180          # rows of the picture
_n = [0]
_reg = {}


def tag():
    _n[0] += 1
    return _n[0]


def plot(off):
    """plot of (d0 + off = column, d4 = y): d5, d6, d4, a1 are clobbered"""
    t = tag()
    o = f"{off}" if off else ""
    o4 = f"{4 * off}" if off else ""
    o42 = f"{4 * off + 2}"
    xoff = f"\tadd.w\t#{off},d6\n" if off else ""
    return f"""	moveq	#0,d5
	move.b	{o}(a2,d0.w),d5			; ridge buffer
	cmp.w	#255,d5
	beq.s	.p{t}				; the column is closed
	cmp.w	d5,d4
	ble.s	.p{t}				; y <= ridge buffer: hidden
	tst.w	d5
	beq.s	.z{t}				; the column was empty: no pixel is covered
	movea.l	ptPtr,a1			; the covered pixel is a contour point (x, y)
	move.w	d0,d6
{xoff}	move.w	d6,(a1)+
	move.w	d5,(a1)+
	move.l	a1,ptPtr
.z{t}:	cmp.w	#{H},d4
	blt.s	.q{t}
	move.b	#-1,{o}(a2,d0.w)		; y >= {H}: the column is closed, drawn on the top row
	move.w	#{H - 1},d4
	bra.s	.r{t}
.q{t}:	move.b	d4,{o}(a2,d0.w)
.r{t}:	add.w	d4,d4
	move.w	(a5,d4.w),d5			; row offset ({H - 1} - y) * 160
	move.w	d0,d6
	add.w	d6,d6
	add.w	d6,d6				; x * 4
	add.w	{o4}(a4,d6.w),d5		; + the byte offset of the group of 16 pixels
	move.w	{o42}(a4,d6.w),d6		; pixel mask
	lea	(a3,d5.w),a1
	or.w	d6,(a1,d1.w)			; the pixel into the plane of the band of the edge (d1 = 0, 2, 4: the plane word; dripped down later)
.p{t}:
"""


def plan(n):
    nodes, leaves = [], []

    def split(lo, hi):
        w = hi - lo
        if w >= 3:
            mid = lo + (w >> 1)
            nodes.append((lo, mid, hi))
            split(lo, mid)
            split(mid, hi)
        else:
            leaves.append((lo, hi))
    split(0, n)
    return nodes, leaves


def yloc(i, n):
    if i in _reg:
        return _reg[i]
    return "d2" if i == 0 else ("ZWY(a0)" if i == n else f"kY+{2 * i}")


def hloc(i, n):
    return "d3" if i == 0 else ("ZWH(a0)" if i == n else f"kH+{2 * i}")


def node(lo, mid, hi, n, bottom):
    t = tag()
    w = hi - lo
    m = w >> 1
    bdn = (m - 1) >> 1
    bup = m >> 1
    s = [f"	; midpoint {mid} of the piece {lo} .. {hi} ({w} columns)",
         f"	move.w	{yloc(lo, n)},d4",
         f"	add.w	{yloc(hi, n)},d4			; t2 = y{lo} + y{hi}",
         "	move.w	d4,d6",
         "	asr.w	#1,d6				; a = t2 >> 1"]
    if lo == 0:
        s += [f"	move.w	d3,d7				; h{lo} + 1", f"	add.w	{hloc(hi, n)},d7			; t = h{lo} + h{hi} + 1 (9 bits)"]
    else:
        s += [f"	move.w	{hloc(lo, n)},d7", f"	add.w	{hloc(hi, n)},d7", "	addq.w	#1,d7				; t = h + h + 1 (9 bits)"]
    s += ["	tst.b	d7", f"	bpl.s	.k{t}d", "	cmp.w	#256,d7", f"	bhs.s	.k{t}c"]
    if bdn:
        s.append(f"	sub.w	#{bdn},d6")
    s += ["	btst	#0,d4", f"	bne.s	.k{t}d", "	subq.w	#1,d6", f"	bra.s	.k{t}d", f".k{t}c:"]
    if bup:
        s.append(f"	add.w	#{bup},d6")
    s += ["	btst	#0,d4", f"	beq.s	.k{t}d", "	addq.w	#1,d6", f".k{t}d:"]
    if bottom:
        s.append("	move.w	d6,d7				; both halves are leaves: y stays in d7")
    else:
        s += ["	andi.w	#$FF,d7", f"	move.w	d7,kH+{2 * mid}", f"	move.w	d6,kY+{2 * mid}"]
    return "\n".join(s) + "\n"


def leaf_code(lo, hi, n):
    s = []
    if hi - lo == 1:
        s += [f"	move.w	{yloc(hi, n)},d4			; one column: the pixel at {lo} = y{hi}", plot(lo)]
    else:
        s += [f"	move.w	{yloc(lo, n)},d4			; two columns: the middle at {lo}, then y{hi} at {lo + 1}", f"	add.w	{yloc(hi, n)},d4", "	addq.w	#1,d4", "	asr.w	#1,d4", plot(lo),
              f"	move.w	{yloc(hi, n)},d4", plot(lo + 1)]
    return s


def kernel(n):
    nodes, leaves = plan(n)
    s = [f".K{n}:				; a piece of {n} columns: {len(nodes)} midpoints"]
    done = set()
    leafset = set(leaves)
    for lo, mid, hi in nodes:
        bottom = (lo, mid) in leafset and (mid, hi) in leafset
        s.append(node(lo, mid, hi, n, bottom))
        if bottom:
            _reg[mid] = "d7"
            for lf in ((lo, mid), (mid, hi)):
                s.extend(leaf_code(lf[0], lf[1], n))
                done.add(lf)
            del _reg[mid]
    for lo, hi in leaves:
        if (lo, hi) not in done:
            s.extend(leaf_code(lo, hi, n))
    s.append("	bra	.pop")
    return "\n".join(s) + "\n"


import os
edges_part = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "edges320_st_part.asm")).read()

ktab_words = "".join(f"	dc.w	.K{n}-.kbase\n" for n in range(3, KMAX + 1))

head = f"""; flight_line320_st.asm - GENERATED by tools/mkline320_st.py {KMAX} {H}: the fractal line of flight320 (F3Seg of flight_core.c) in 68000 assembly.  vasm, Motorola syntax.  See the generator for the rules.
;   void F3SegAsm(int lit, int x1, int y1, int h1, int x2, int y2, int h2);

	section	.text,code

	xref	f3LineBuf
	xref	f3Scr
	xref	f3XTab
	xref	f3RowOff
	xref	ptPtr
	xdef	F3SegAsm

ZWY	equ	100
ZWH	equ	200

{edges_part}F3SegAsm:
	movem.l	d2-d7/a2-a6,-(sp)		; 11 registers = 44 bytes, + the return address
	move.l	48(sp),d1			; lit
	move.l	52(sp),d0			; x1
	move.l	56(sp),d2			; y1
	move.l	60(sp),d3			; h1
	addq.w	#1,d3				; h1 + 1
	lea	fZW,a0
	move.w	66(sp),(a0)			; x2
	move.w	70(sp),ZWY(a0)			; y2
	move.w	74(sp),ZWH(a0)			; h2
	lea	f3LineBuf,a2
	movea.l	f3Scr,a3
	lea	f3XTab,a4
	lea	f3RowOff,a5
	bsr.s	F3Run
	movem.l	(sp)+,d2-d7/a2-a6
	rts

F3Run:
.loop:
	cmp.w	#320,d0				; X >= 320: done
	bge	.done
.step:
	move.w	(a0),d4				; the right end of the piece
	ble	.pop				; entirely left of the screen: skip it
	sub.w	d0,d4				; n
	subq.w	#3,d4				; n - 3
	bcs	.leaf				; n = 1 or 2
	beq	.nocull				; n = 3: no cull test

	; ---- piece culling (n >= 4): nothing of it can be above the ridge buffer if max(y0, y1) + n / 2 + 3 <= the ridge buffer in every column of it (exact); not tried above 255
	move.w	d4,d6
	addq.w	#3,d6
	lsr.w	#1,d6
	addq.w	#3,d6				; half = (n >> 1) + 3
	move.w	ZWY(a0),d7			; y1
	cmp.w	d7,d2
	bge.s	.y0hi
	move.w	d7,d5
	bra.s	.cb
.y0hi:	move.w	d2,d5
.cb:	add.w	d6,d5				; bound = max(y0, y1) + half
	bmi	.hidden				; below the screen
	cmp.w	#255,d5
	bgt	.nocull
	move.w	d0,d6
	bpl.s	.c0
	moveq	#0,d6				; first column on the screen
.c0:	move.w	(a0),d7
	cmp.w	#320,d7
	ble.s	.c1
	move.w	#320,d7				; end column
.c1:	sub.w	d6,d7
	subq.w	#1,d7				; columns - 1
	lea	(a2,d6.w),a1
.scan:	cmp.b	(a1)+,d5			; bound > ridge buffer: a column that can show a pixel
	bhi.s	.nocull
	dbra	d7,.scan
.hidden:
	bra	.pop

	; ---- pieces of 3 .. {KMAX} columns that lie completely on the screen: the unrolled kernel of that width (mkline320.py); else push the midpoint with the fractal displacement
.nocull:
	tst.w	d0
	bmi.s	.wide
	move.w	(a0),d4
	cmp.w	#320,d4
	bgt.s	.wide
	sub.w	d0,d4				; n
	cmp.w	#{KMAX},d4
	bhi.s	.wide
	subq.w	#3,d4
	add.w	d4,d4
	move.w	.ktab(pc,d4.w),d4
	lea	.kbase(pc),a1
	jmp	(a1,d4.w)
.ktab:
{ktab_words}.wide:
	move.w	(a0),d4
	sub.w	d0,d4				; n
	move.w	d4,d5
	lsr.w	#1,d5				; m = n >> 1 = mid - X
	move.w	d0,d6
	add.w	d5,d6				; mid = X + m
	move.w	d6,2(a0)			; X of the new top
	move.w	ZWH(a0),d7
	add.w	d3,d7				; t = h1 + h2 + 1 (9 bits)
	move.w	d2,d6
	add.w	ZWY(a0),d6			; t2 = y1 + y2
	move.w	d6,d4				; (bit 0 = the parity)
	asr.w	#1,d6				; a = t2 >> 1
	tst.b	d7
	bpl.s	.nodisp				; the h byte is below $80: no displacement
	cmp.w	#256,d7
	bhs.s	.carry
	subq.w	#1,d5
	lsr.w	#1,d5				; (m - 1) >> 1
	sub.w	d5,d6
	btst	#0,d4
	bne.s	.nodisp
	subq.w	#1,d6
	bra.s	.nodisp
.carry:	lsr.w	#1,d5				; m >> 1
	add.w	d5,d6
	btst	#0,d4
	beq.s	.nodisp
	addq.w	#1,d6
.nodisp:
	andi.w	#$FF,d7
	move.w	d7,ZWH+2(a0)
	move.w	d6,ZWY+2(a0)
	addq.l	#2,a0
	bra	.step

	; ---- one or two columns
.leaf:
	addq.w	#1,d4
	bne	.one				; n = 1
	move.w	ZWY(a0),d4			; two columns: the middle (y1 + y2 + 1) >> 1 at X, then y2 at X + 1
	move.w	d4,d7
	add.w	d2,d4
	addq.w	#1,d4
	asr.w	#1,d4
	cmp.w	#320,d0
	bhs	.l1
{plot(0)}.l1:
	addq.w	#1,d0
	move.w	d7,d4
	cmp.w	#320,d0
	bhs	.pop
{plot(0)}	bra	.pop
.one:	move.w	ZWY(a0),d4
	cmp.w	#320,d0
	bhs	.pop
{plot(0)}
	; ---- pop: the next piece starts at the end of this one
.pop:
	move.w	(a0),d0
	move.w	ZWY(a0),d2
	move.w	ZWH(a0),d3
	addq.w	#1,d3
	subq.l	#2,a0
	cmpa.l	#fZW,a0
	bhs	.loop
.done:
	rts
"""

kernels = "\n; ---- the unrolled kernels (generated)\n.kbase:\n" + "".join(kernel(n) for n in range(3, KMAX + 1))
tail = """
	section	.bss,bss
fZW:	ds.w	150
kY:	ds.w	17
kH:	ds.w	17
"""
open(OUT, "w", newline="\n").write(head + kernels + tail)
print(f"{OUT}: kernels for 3 .. {KMAX} columns")
