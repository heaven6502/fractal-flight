"""make_bgs.py - converts every image of gfx/ to a dithered 4 colour background (320 x 150, ordered dithering) and stores, in gfx/st4/:
  <name>.png   the picture with the ST colours (what the ST shows, 320 x 150)
  <name>.pal   the 4 ST palette entries (0RGB hex, 3 bits per channel, darkest first = colour 0, also the screen border)
  palettes.txt all palettes in one list, overview.png a contact sheet.
Choose one:  python tools/gfx/gen_bg.py gfx/<name>.<ext> ordered [crop]   (then ./build.sh)"""
import os, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bgconv
from PIL import Image, ImageDraw
ROOT = os.path.join(HERE, "..", "..", "gfx")
OUT = os.path.join(ROOT, "st4")
os.makedirs(OUT, exist_ok=True)
CROP = {}                                                       # per image crop: rows cut off the top, or "mid" (default: the middle when the image is taller than 150 rows, else the top)
files = sorted(f for f in glob.glob(os.path.join(ROOT, "*")) if f.lower().endswith((".png", ".gif", ".jpg", ".bmp")))
sheet = []
lines = []
for f in files:
    name = os.path.splitext(os.path.basename(f))[0]
    crop = CROP.get(name, "mid" if bgconv.scaled_height(f) > 150 else 0)
    rows, pal, pv = bgconv.convert(f, "ordered", crop)
    pv.save(os.path.join(OUT, name + ".png"))
    open(os.path.join(OUT, name + ".pal"), "w").write("; %s: the 4 ST colours (0RGB, 3 bits per channel, darkest first), crop %s\n\tdc.w %s\n" % (os.path.basename(f), crop, ", ".join("$%04X" % c for c in pal)))
    lines.append("%-24s %s   (crop %s)" % (name, " ".join("$%04X" % c for c in pal), crop))
    sheet.append((name, pv, pal))
open(os.path.join(OUT, "palettes.txt"), "w").write("; ST palette entries (0RGB, 3 bits per channel) of the 4 colour backgrounds, darkest first = colour 0\n" + "\n".join(lines) + "\n")
cw, ch = 320, 150
cols = 2
rowsn = (len(sheet) + 1) // 2
ov = Image.new("RGB", (cols * (cw + 8) + 8, rowsn * (ch + 28) + 8), (40, 40, 40))
d = ImageDraw.Draw(ov)
for i, (name, pv, pal) in enumerate(sheet):
    x = 8 + (i % cols) * (cw + 8)
    y = 8 + (i // cols) * (ch + 28)
    ov.paste(pv, (x, y + 18))
    d.text((x, y + 2), name, fill=(255, 255, 255))
    for k, c in enumerate(pal):
        r, g, b = ((c >> 8) & 7) * 255 // 7, ((c >> 4) & 7) * 255 // 7, (c & 7) * 255 // 7
        d.rectangle((x + cw - 4 * 14 + k * 14, y + 2, x + cw - 4 * 14 + k * 14 + 12, y + 14), fill=(r, g, b))
ov.save(os.path.join(OUT, "overview.png"))
print("\n".join(lines))
