"""gen_bg.py image [flat|ordered|fs] [crop]  - makes src/sky_data.c (the background of the flight) from a source image; see bgconv.py.  Default style: ordered.
Example: python tools/gfx/gen_bg.py gfx/earthworm16c.png ordered
The ready made 4 colour versions of all source images and their ST palettes are in gfx/st4/ (python tools/gfx/make_bgs.py)."""
import sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bgconv
src = sys.argv[1]
style = next((a for a in sys.argv[2:] if a in ("flat", "ordered", "fs")), "ordered")
crop = next((a for a in sys.argv[2:] if a == "mid" or a.isdigit()), 0)
rows, pal, pv = bgconv.convert(src, style, crop)
open(os.path.join(HERE, "..", "..", "src", "sky_data.c"), "w").write(bgconv.planar_c(rows, pal, os.path.basename(src)))
pv.resize((640, 300), 0).save(os.path.join(HERE, "bg_preview.png"))
print("src/sky_data.c written, ST colours", " ".join("%03X" % c for c in pal))
