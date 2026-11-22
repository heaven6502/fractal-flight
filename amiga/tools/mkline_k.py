"""mkline_k.py [KMAX] [out] [CULLMIN] [REGS]: writes src/flight_line_k.asm = src/flight_line.asm (the byte line routine, version 2) with UNROLLED KERNELS for the small pieces.

    python tools/mkline_k.py 16 src/flight_line_k.asm

Idea (see Atari\\rof\\jaggie_3d_tab.pb, 'precalculated plans'): the subdivision of a piece of n columns depends on n alone, so for every n = 3 .. KMAX the whole recursion (the midpoints, their
widths, the displacement constants, the 1 and 2 column pieces and their pixels) is written out as straight line code:  no stack, no loop, no width arithmetic;  the displacement
constants ((m - 1) >> 1 and m >> 1 with m = n >> 1) are immediates.  The pieces of 3 .. 16 columns are about 83% of all midpoints of a picture.  The result is bit for bit the same as the recursion
(checked by the benchmark checksum and by the host test of the plans in jaggie_3d_tab.pb: 0 wrong ridge columns in 600 pictures).

A midpoint needs  y = (y0 + y1) >> 1  with the displacement from the hash h = h0 + h1 + 1:  h bit 7 clear: none;  set without carry (h < 256): a - ((m - 1) >> 1) - (1 - c2)  (byte arithmetic, wraps);
set with carry: min(255, a + (m >> 1) + c2),  c2 = bit 0 of y0 + y1.   The points of a piece are numbered by their column offset 0 .. n:  point 0 = the end 1 (y in d2, h + 1 in d3), point n = the stack top
(ZWY(a0), ZWH(a0)), the midpoints are kept in two small arrays kY / kH (words) in the bss.

REGS = 1: a midpoint whose two halves are both leaves (pieces of 3 or 4 columns) is not stored: its y stays in d7 and its pixels are plotted at once (the order of the pixels does not matter, every
column is plotted once).  CULLMIN: pieces narrower than this skip the cull test (0 = all are tested; measured: testing is better).
"""
import sys

KMAX = int(sys.argv[1]) if len(sys.argv) > 1 else 16
OUT = sys.argv[2] if len(sys.argv) > 2 else "src/flight_line_k.asm"
CULLMIN = int(sys.argv[3]) if len(sys.argv) > 3 else 0
REGS = int(sys.argv[4]) if len(sys.argv) > 4 else 1
SRC = open("src/flight_line.asm").read()
_n = [0]
_reg = {}      # point -> register, while a bottom midpoint is kept in d7


def tag():
    _n[0] += 1
    return _n[0]


def plan(n):
    """the midpoints (lo, mid, hi) of a piece of n columns, parents first, and its pieces of 1 or 2 columns, left to right"""
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


def node(lo, mid, hi, n, bottom=False):
    t = tag()
    w = hi - lo
    m = w >> 1
    bdn = ((m - 1) & 0xFF) >> 1
    bup = (m & 0xFF) >> 1
    s = []
    s.append(f"	; midpoint {mid} of the piece {lo} .. {hi} ({w} columns)")
    s.append(f"	move.w	{yloc(lo, n)},d4")
    s.append(f"	add.w	{yloc(hi, n)},d4			; t2 = y{lo} + y{hi}")
    s.append("	move.w	d4,d6")
    s.append("	lsr.w	#1,d6				; a = t2 >> 1")
    if lo == 0:
        s.append(f"	move.w	d3,d7				; h{lo} + 1")
        s.append(f"	add.w	{hloc(hi, n)},d7			; t = h{lo} + h{hi} + 1 (9 bits)")
    else:
        s.append(f"	move.w	{hloc(lo, n)},d7")
        s.append(f"	add.w	{hloc(hi, n)},d7")
        s.append("	addq.w	#1,d7				; t = h + h + 1 (9 bits)")
    s.append("	tst.b	d7")
    s.append(f"	bpl.s	.k{t}d				; the h byte is below $80: no displacement")
    s.append("	cmp.w	#256,d7")
    s.append(f"	bhs.s	.k{t}c")
    # down: a - bdn - (1 - c2)  (byte arithmetic)
    s.append(f"	sub.b	#{bdn},d6")
    s.append("	btst	#0,d4")
    s.append(f"	bne.s	.k{t}d")
    s.append("	subq.b	#1,d6")
    s.append(f"	bra.s	.k{t}d")
    # up: min(255, a + bup + c2)
    s.append(f".k{t}c:")
    if bup:
        s.append(f"	add.w	#{bup},d6")
    s.append("	btst	#0,d4")
    s.append(f"	beq.s	.k{t}n")
    s.append("	addq.w	#1,d6")
    s.append(f".k{t}n:	cmp.w	#255,d6")
    s.append(f"	bls.s	.k{t}d")
    s.append("	move.w	#255,d6")
    s.append(f".k{t}d:")
    if bottom:
        s.append("	move.w	d6,d7				; both halves are leaves: y stays in d7 (the hash is not needed any more)")
    else:
        s.append("	andi.w	#$FF,d7")
        s.append(f"	move.w	d7,kH+{2 * mid}")
        s.append(f"	move.w	d6,kY+{2 * mid}")
    return "\n".join(s) + "\n"


def plot(off):
    """plot of (d0 + off = column, d4 = y): ridge buffer test, update, the pixel rows (the same as in mkline.py with a constant column offset)"""
    t = tag()
    o = f"{off}" if off else ""
    return f"""	move.b	{o}(a2,d0.w),d5			; highest ridge value so far in this column
	cmp.b	d5,d4
	bls.s	.p{t}				; y <= fLineBuf[x]: hidden
	move.b	(a6,d4.w),{o}(a2,d0.w)		; fLineBuf[x] = y, or $FF when the column is closed (y >= yTop): fLbMap
	add.w	d4,d4
	add.w	d4,d4				; y * 4
	movea.l	(a5,d4.w),a1			; address of the picture row in plane 0
	moveq	#0,d5
	move.b	{o}(a3,d0.w),d5			; byte offset of the column
	move.b	{o}(a4,d0.w),d6			; pixel mask
	or.b	d6,(a1,d5.w)
	ifgt	ROWREP-1
	adda.w	fCfg+2,a1			; next screen row
	or.b	d6,(a1,d5.w)
	endc
	ifgt	ROWREP-2
	adda.w	fCfg+2,a1
	or.b	d6,(a1,d5.w)
	endc
.p{t}:
"""


def leaf_code(lo, hi, n):
    s = []
    if hi - lo == 1:
        s.append(f"	move.w	{yloc(hi, n)},d4			; one column: the pixel at {lo} = y{hi}")
        s.append(plot(lo))
    else:
        s.append(f"	move.w	{yloc(lo, n)},d4			; two columns: the middle ((y{lo} + y{hi} + 1) >> 1) at {lo}, then y{hi} at {lo + 1}")
        s.append(f"	add.w	{yloc(hi, n)},d4")
        s.append("	addq.w	#1,d4")
        s.append("	lsr.w	#1,d4")
        s.append(plot(lo))
        s.append(f"	move.w	{yloc(hi, n)},d4")
        s.append(plot(lo + 1))
    return s


def kernel(n):
    nodes, leaves = plan(n)
    s = [f".K{n}:				; a piece of {n} columns: {len(nodes)} midpoints, {sum(hi - lo for lo, hi in leaves)} pixels"]
    done = set()
    leafset = set(leaves)
    for lo, mid, hi in nodes:
        bottom = bool(REGS) and (lo, mid) in leafset and (mid, hi) in leafset
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


def dispatch():
    ktab_words = "".join(f"	dc.w	.K{n}-.kbase\n" for n in range(3, KMAX + 1))
    return f"""	; ---- pieces of 3 .. {KMAX} columns: the unrolled kernel of that width (mkline_k.py); wider: push the midpoint with the fractal displacement
.nocull:
	cmp.w	#{KMAX - 3},d4
	bhi.s	.wide
	add.w	d4,d4
	move.w	.ktab(pc,d4.w),d4
	lea	.kbase(pc),a1
	jmp	(a1,d4.w)
.ktab:
{ktab_words}.wide:
"""


kernels = "\n; ---- the unrolled kernels (generated)\n.kbase:\n" + "".join(kernel(n) for n in range(3, KMAX + 1))

out = SRC
if CULLMIN:
    marker = "	bcs	.leaf				; n = 1 or 2: the end of the recursion\n"
    k = out.index(marker) + len(marker)
    out = out[:k] + f"	cmp.w	#{CULLMIN - 3},d4\n	blo	.nocull				; a small piece: no cull test\n" + out[k:]
a = out.index(".nocull:\n")
out = out[:a] + dispatch() + out[a + len(".nocull:\n"):]
b = out.index("	section	.bss,bss")
out = out[:b] + kernels + "\n" + out[b:]
out = out.replace("fZW:	ds.w	150", "fZW:	ds.w	150\nkY:	ds.w	17				; the midpoints of the piece being unrolled (y, h)\nkH:	ds.w	17")
out = out.replace("; flight_line.asm - the byte line routine", f"; flight_line_k.asm (GENERATED by tools/mkline_k.py {KMAX} from flight_line.asm: unrolled kernels for pieces of 3 .. {KMAX} columns) - the byte line routine", 1)
open(OUT, "w", newline="\n").write(out)
print(f"{OUT}: kernels for 3 .. {KMAX} columns, {len(out.splitlines())} lines")
