"""benchnum.py shot.png: reads the 4 digit number drawn by StDrawNumber in the top left corner of a Hatari screenshot of the low resolution screen (blue digits on black)"""
import sys
from PIL import Image
FONT = {(7,5,5,5,7):0,(2,6,2,2,7):1,(7,1,7,4,7):2,(7,1,7,1,7):3,(5,5,7,1,1):4,(7,4,7,1,7):5,(7,4,7,5,7):6,(7,1,1,1,1):7,(7,5,7,5,7):8,(7,5,7,1,7):9}
im = Image.open(sys.argv[1]).convert("RGB").crop((96, 56, 736, 460))
px = im.load()
DARK = len(sys.argv) > 2 and sys.argv[2] == "dark"
def lit(x, y):
    c = px[x, y]
    return max(c) < 60 if DARK else c[2] > c[0] + 40          # dark digits (flight) or blue digits
def number(y0):
    val = ""
    for d in range(4):
        rows = []
        for r in range(5):
            bits = 0
            for c in range(3):
                x = 8 * 2 + (d * 8 + c * 2) * 2 + 2; y = (y0 + r * 2) * 2 + 2     # centre of the 2 x 2 pixel cell, screenshot scale 2
                bits = (bits << 1) | (1 if lit(x, y) else 0)
            rows.append(bits)
        val += str(FONT.get(tuple(rows), "?")) if any(rows) else " "
    return val.strip()
print("VBLs:", number(6), " checksum:", number(180 if DARK else 24))
