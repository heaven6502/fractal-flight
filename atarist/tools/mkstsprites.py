#!/usr/bin/env python3
"""-> src/st_sprites.h: the bitmap shapes of the sun for the ST flight (flight320_st.c).
Shapes (bitmap index 0 .. 12 = shape 1 .. 13; shape 0 is the three rings drawn by SunDraw):
  0 the planet disc (3 colours: night side, day side, bright rim), 1 half lit, 2 quarter lit (the same sphere shading as the Amiga's BuildSun),
  3 .. 12 the planets / moon / Death Star / Endor / balloon of the Amiga (src/gyruss_planets.h of the Amiga repo, 8 colours reduced to 6).
Every shape: 42 lines of 32 pixels; per line 6 words = plane 0 left / right 16 pixels, plane 1, plane 2.  The pixel value is the palette entry 2 .. 7 (0 = transparent): the entries 2 .. 7
of the ST palette are free in the flight picture, the sun pixels have plane 3 = 0.
usage: mkstsprites.py [path of the Amiga gyruss_planets.h]"""
import sys, os, re

AMIGA = sys.argv[1] if len(sys.argv) > 1 else r'C:\coding\Amiga\demo\src\gyruss_planets.h'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'st_sprites.h')
ROWS = 42
SUN_T = -4
SUN_CY = 17

def tdiv(a, b):                      # C division (towards zero)
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q

def isqrt(v):
    r = 0
    while (r + 1) * (r + 1) <= v: r += 1
    return r

BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]

def disc(half):
    img = []
    for l in range(ROWS):
        dy = SUN_T + l - SUN_CY
        ny = tdiv(dy * 6 * 256, 5 * 14)
        row = []
        for sx in range(32):
            x = sx - 16
            nx = tdiv(x * 256, 14); r2 = nx * nx + ny * ny; code = 0
            if r2 <= 65536:
                nz = isqrt(65536 - r2); th = BAYER[((SUN_T + l) & 3) * 4 + (x & 3)] * 16 + 8
                I = (nx * -230 + ny * -60 + nz * 90) >> 8
                if I < 0: I = 0
                q = I * 2; code = 1 + (q >> 8) + (1 if (q & 255) > th else 0)
                if code > 2: code = 2
                if r2 > 52000 and I > 40: code = 3
                if half:
                    Lx = -256 if half == 1 else -222
                    Lz = 0 if half == 1 else -128
                    J = (nx * Lx + nz * Lz) >> 8
                    if J <= 0: code = 1
                    elif J < 70: code = 2 if J * 4 > th else 1
                    else: code = 3 if (J > 190 and r2 < 30000 and ((J * 2) & 255) > th) else 2
            row.append(code)
        img.append(row)
    return img

def parse_amiga():
    t = open(AMIGA).read()
    n = int(re.search(r'#define GY_N (\d+)', t).group(1))
    fixed = int(re.search(r'#define GY_FIXED (\d+)', t).group(1))
    pal_txt = t[t.index('gyPal'):t.index('gyWords')]
    words_txt = t[t.index('gyWords'):]
    pals = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{4})', pal_txt)]
    words = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{4})', words_txt)]
    assert len(pals) == n * 8 and len(words) == n * ROWS * 8, (len(pals), len(words))
    shapes = []
    for p in range(n):
        cols = pals[p * 8:(p + 1) * 8]
        img = []
        for l in range(ROWS):
            w = words[(p * ROWS + l) * 8:(p * ROWS + l + 1) * 8]
            row = []
            for sp in range(2):
                a, b, c, d = w[4 * sp:4 * sp + 4]
                for i in range(16):
                    m = 0x8000 >> i
                    row.append((1 if a & m else 0) | (2 if b & m else 0) | (4 if c & m else 0) | (8 if d & m else 0))
            img.append(row)
        shapes.append((cols, img))
    return shapes, fixed

def rgb(c): return ((c >> 8) & 15, (c >> 4) & 15, c & 15)

def reduce(cols, img, k=6):
    """merge the colours (codes 1 .. 8, colour cols[code-1]) down to k; returns the colour list (sorted dark to light) and the image in codes 1 .. k"""
    cnt = {}
    for row in img:
        for v in row:
            if v: cnt[v] = cnt.get(v, 0) + 1
    groups = {v: ([v], list(rgb(cols[v - 1])), cnt[v]) for v in cnt}     # id -> (members, mean colour, pixels)
    while len(groups) > k:
        best = None
        ids = list(groups)
        for i in range(len(ids)):
            for j in range(i + 1, len(ids)):
                a, b = groups[ids[i]], groups[ids[j]]
                d = sum((a[1][c] - b[1][c]) ** 2 for c in range(3))
                cost = a[2] * b[2] / (a[2] + b[2]) * (d + 0.5)
                if best is None or cost < best[0]: best = (cost, ids[i], ids[j])
        _, i, j = best
        a, b = groups[i], groups.pop(j)
        n = a[2] + b[2]
        groups[i] = (a[0] + b[0], [(a[1][c] * a[2] + b[1][c] * b[2]) / n for c in range(3)], n)
    order = sorted(groups, key=lambda g: groups[g][1][0] * 5 + groups[g][1][1] * 9 + groups[g][1][2] * 2)
    remap = {}
    outcols = []
    for newcode, g in enumerate(order, 1):
        for m in groups[g][0]: remap[m] = newcode
        outcols.append(tuple(int(round(x)) for x in groups[g][1]))
    out = [[remap[v] if v else 0 for v in row] for row in img]
    return outcols, out

def planes(img):
    """codes 1 .. 6 -> entries 2 .. 7; 6 words per line"""
    res = []
    for row in img:
        w = []
        for plane in range(3):
            for half in range(2):
                v = 0
                for i in range(16):
                    code = row[half * 16 + i]
                    e = code + 1 if code else 0
                    if (e >> plane) & 1: v |= 0x8000 >> i
                w.append(v)
        res.append(w)
    return res

shapes = []                           # (name, colours or None, img)
shapes.append(('disc', None, disc(0)))
shapes.append(('half', None, disc(1)))
shapes.append(('quarter', None, disc(2)))
am, fixed = parse_amiga()
names = ['Earth', 'Neptune', 'Uranus', 'Saturn', 'Jupiter', 'Mars', 'moon', 'Death Star', 'Endor', 'balloon']
for i, (cols, img) in enumerate(am):
    c6, im = reduce(cols, img)
    shapes.append((names[i] if i < len(names) else 'shape%d' % i, c6, im))

with open(OUT, 'w', newline='\n') as f:
    f.write('/* generated by tools/mkstsprites.py (do not edit): the bitmap shapes of the sun, %d lines of 32 pixels, the pixel value is the palette entry 2 .. 7 (0 = transparent) */\n' % ROWS)
    f.write('#define SPR_N %d\n#define SPR_ROWS %d\n#define SPR_FIXED %d          /* the first bitmap shape that is not tinted by the palette (the moon) */\n' % (len(shapes), ROWS, 3 + fixed))
    f.write('/* the colours of the shapes (4 bit levels 0x0RGB, dark to light, 6 entries = the palette entries 2 .. 7); the discs take the colours of the palette */\n')
    f.write('static const unsigned short sprCol[SPR_N][6] = {\n')
    for name, cols, img in shapes:
        vals = [0] * 6
        if cols:
            for i, c in enumerate(cols): vals[i] = (c[0] << 8) | (c[1] << 4) | c[2]
        f.write('\t{ ' + ', '.join('0x%04X' % v for v in vals) + ' },     /* %s */\n' % name)
    f.write('};\n')
    f.write('static const unsigned char sprNC[SPR_N] = { ' + ', '.join(str(len(cols) if cols else 3) for name, cols, img in shapes) + ' };     /* the number of colours of a shape: its brightest colour is the entry 1 + this (the stars use it) */' + chr(10))
    f.write('static const unsigned short sprW[SPR_N][SPR_ROWS][6] = {\n')
    for name, cols, img in shapes:
        f.write('\t{     /* %s */\n' % name)
        for w in planes(img):
            f.write('\t\t{ ' + ', '.join('0x%04X' % v for v in w) + ' },\n')
        f.write('\t},\n')
    f.write('};\n')
print('%d shapes, fixed from %d -> %s' % (len(shapes), 3 + fixed, OUT))
