"""Reference renders from the real 6502 build of the latest iron port (flight800_far_pm_iron.asm: FAR = 1, D80 = 1, BIG = 1) through AltirraBridge.

For every case (terrain + camera) the heightmap and the camera are poked into the running game at the start of render pass 1, the pass is run to the entry of SKY_FILL
(the bitmap then holds only the ridge pixels: the game's sky fill works on 4 pixel bytes with blend tables and even overwrites ridge pixels) and
the ridge buffer ($2608) and the bitmap (80 rows, 2 bits per pixel, the half that pass 1 draws) are read back:

  testdata/far40_render.bin   40 byte columns (-d:NARROW=1: pixel columns 48 .. 207, the one to show)
  testdata/far42_render.bin   42 byte columns (pixel columns 44 .. 211)

usage: python tools/make_far_vectors.py <token file> <40|42> <xex> <lab>
The xex files are built from the iron source with standalone = 1 and -d:NOINTRO=1 (see make_far_vectors.md / the Amiga project notes)."""
import glob, os, random, re, struct, subprocess, sys, tempfile, time

sys.path.insert(0, r"C:\coding\Atari\AltirraBridge-nightly-windows-x86_64\sdk\python")
from altirra_bridge import AltirraBridge

ROF = r"C:\coding\Atari\rof"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "testdata")
tok, variant, xex, lab = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
narrow = variant == "40"
COL_LO, COL_HI, BOFF = (48, 208, 4) if narrow else (44, 212, 3)
BYTES = (COL_HI - COL_LO) // 4
ROWS = 80

def labels(p):
    return {m.group(2).upper(): int(m.group(1), 16) for m in re.finditer(r"^\w+\s+([0-9A-F]{4})\s+(\w+)", open(p).read(), re.M)}
L = labels(lab)

# ---- cases: the saved states + random terrain / camera (terrain from the PureBasic generator: game-like maps)
random.seed(4242)
EXE = os.path.join(ROF, "jaggie_3d.exe")
tmp = tempfile.mkdtemp()
cases = sorted(glob.glob(os.path.join(ROF, "state_*.txt")))
for i in range(36):
    st = os.path.join(tmp, "rnd%d.txt" % i)
    subprocess.run([EXE, "--seed", str(2000 + i), "--save", st, "--shot", os.path.join(tmp, "x.png")], timeout=60, check=True)
    lines = open(st).read().split("\n")
    cam = [random.randrange(0, 65536), random.randrange(0, 65536), random.randrange(0x0800, 0x4F00),
           random.randrange(0, 0x4000), random.choice([random.randrange(0, 0x0C00), random.randrange(0xF400, 0x10000)]),
           random.choice([0, random.randrange(0, 0x0500), random.randrange(0xFB00, 0x10000)]), 0, 0x1240]
    lines[1] = " ".join(str(v) for v in cam)
    open(st, "w").write("\n".join(lines))
    cases.append(st)

def wait_pc(a, addr, tries=300):
    a.resume()
    for _ in range(tries):
        time.sleep(0.05)
        if a.regs()["PC"].lower() == "$%04x" % addr:
            return True
    return False

out = bytearray(struct.pack("<H", len(cases)))
with AltirraBridge.from_token_file(tok) as a:
    a.bp_clear_all(); a.boot(xex); a.frame(60)
    for n, f in enumerate(cases):
        hm_hex, vals = open(f).read().split("\n")[:2]
        px, pz, alt, head, pitch, roll, bank, speed = (int(x) for x in vals.split())
        a.bp_clear_all(); a.bp_set(L["RENDER_FRAME"])
        for _ in range(12):                                  # the start of a pass that draws into the right half (A = $30)
            assert wait_pc(a, L["RENDER_FRAME"]), "no RENDER_FRAME"
            if a.regs()["A"].lower() == "$30": break
        else: raise SystemExit("no pass 1")
        a.bp_clear_all()
        a.memload(0x900, bytes.fromhex(hm_hex))
        for ad, v in ((0x289B, px), (0x289D, pz), (0x289F, alt), (0x2899, head), (0x288D, pitch), (0x2889, roll), (0x288B, bank)):
            a.poke16(ad, v)
        a.bp_set(L["SKY_FILL"])
        assert wait_pc(a, L["SKY_FILL"], 600), "no SKY_FILL"
        lb = bytes(a.memdump(0x2608, 256))
        raw = b"".join(bytes(a.memdump(base, 0xC00)) for base in (0x6000, 0x7000, 0x8000))
        bm = b"".join(raw[r * 0x60 + 0x30 + BOFF: r * 0x60 + 0x30 + BOFF + BYTES] for r in range(ROWS))
        a.bp_clear_all()
        out += bytes.fromhex(hm_hex) + struct.pack("<8H", px, pz, alt, head, pitch, roll, bank, speed) + lb[COL_LO:COL_HI] + bm
        print(n, os.path.basename(f), "pixels", sum(1 for b in bm for k in range(4) if (b >> (6 - 2 * k)) & 3 == 2))
open(os.path.join(OUT, "far%s_render.bin" % variant), "wb").write(out)
print("far%s_render.bin" % variant, len(cases), "cases", len(out), "bytes")
