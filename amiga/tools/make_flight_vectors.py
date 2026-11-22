"""Makes the test data and the tables for the C port of the fractal flight (Behind Jaggi Lines! / Rescue on Fractalus):

  src/flight_data.c           sine tables, walk tables, edges (parsed from Atari\\rof\\jaggie_data.pbi / jaggie_flight.pbi)
  testdata/flight_render.bin  reference renders: terrain + camera -> line pixels of the 168 x 48 bitmap (jaggie_3d.exe, the PureBasic
                              renderer that was checked against the game) for the saved states state_*.txt and random terrain / camera / roll
  testdata/flight_flight.bin  flight model sequences from the Python model (jaggie_flight.py): random sticks, keys, ground hits

usage: python tools/make_flight_vectors.py   (needs the Atari\\rof folder; Pillow for the PNGs)"""
import os, re, random, struct, subprocess, sys, tempfile, glob
from PIL import Image

ROF = r"C:\coding\Atari\rof"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..")
sys.path.insert(0, ROF)
import jaggie_flight as jf

# ---------------------------------------------------------------- tables
# src/flight_data.c is written by gen_flight_tables.py; the sine table below is the one the Python flight model uses
subprocess.run([sys.executable, os.path.join(HERE, "gen_flight_tables.py")], check=True)
sinhi = [int(v) for v in jf.load_sintable()[:65]]

# ---------------------------------------------------------------- renderer vectors
EXE = os.path.join(ROF, "jaggie_3d.exe")
tmp = tempfile.mkdtemp()
random.seed(2026)

def run_pb(state_file):
    png, ridge = os.path.join(tmp, "r.png"), os.path.join(tmp, "r.txt")
    for f in (png, ridge):
        if os.path.exists(f): os.remove(f)
    subprocess.run([EXE, "--state", state_file, "--shot", png, "--ridge", ridge], timeout=60, check=True)
    im = Image.open(png).convert("RGB")
    rows = []
    for r in range(43):                                      # rows 43..47 are covered by the cockpit bar in the picture
        rows.append([1 if im.getpixel((c * 3 + 1, r * 4 + 1)) == (60, 26, 8) else 0 for c in range(168)])
    ridgev = [0xFF] * 168
    for line in open(ridge):
        c, v = line.split()
        ridgev[int(c) - 44] = int(v[1:], 16)
    return rows, ridgev

cases = []
for f in sorted(glob.glob(os.path.join(ROF, "state_*.txt"))):
    cases.append(f)
# random cases: random terrain from the PB generator (--seed), random camera including roll
for i in range(36):
    st = os.path.join(tmp, "rnd%d.txt" % i)
    subprocess.run([EXE, "--seed", str(1000 + i), "--save", st, "--shot", os.path.join(tmp, "x.png")], timeout=60, check=True)
    lines = open(st).read().split("\n")
    cam = [random.randrange(0, 65536), random.randrange(0, 65536), random.randrange(0x0800, 0x4F00),
           random.randrange(0, 0x4000), random.choice([random.randrange(0, 0x0C00), random.randrange(0xF400, 0x10000)]),
           random.choice([0, random.randrange(0, 0x0500), random.randrange(0xFB00, 0x10000)]), 0, 0x1240]
    lines[1] = " ".join(str(v) for v in cam)
    open(st, "w").write("\n".join(lines))
    cases.append(st)

out = bytearray(struct.pack("<H", len(cases)))
for f in cases:
    ls = open(f).read().split("\n")
    hm = bytes(int(ls[0][i * 2:i * 2 + 2], 16) for i in range(256))
    cam = [int(v) for v in ls[1].split()]                    # posx posz alt heading pitch roll bank speed
    rows, ridge = run_pb(f)
    out += hm + struct.pack("<8H", *cam)
    for r in rows:                                           # 43 rows x 21 bytes, MSB first
        for b in range(21):
            v = 0
            for k in range(8): v = (v << 1) | r[b * 8 + k]
            out.append(v)
    out += bytes(ridge)
    print(os.path.basename(f), "pixels", sum(map(sum, rows)))
open(os.path.join(OUT, "testdata", "flight_render.bin"), "wb").write(out)
print("flight_render.bin", len(cases), "cases", len(out), "bytes")

# ---------------------------------------------------------------- flight model vectors
FIELDS = jf.STATE_ADDRS
def snap(m): return bytes(m[a] for a in FIELDS)
out = bytearray(); nseq = 0
for s in range(60):
    m = jf.new_state()
    m[jf.SPEED_CFG] = random.choice([0, 10, 30, 30, 60, 100])
    m.w16(jf.SPEED_LO, random.randrange(0, 0x3000))
    m.w16(jf.ALT_LO, random.randrange(0x0800, 0x4800))
    m.w16(jf.HEAD_LO, random.randrange(0, 0x4000))
    m.w16(jf.POSX_LO, random.randrange(0, 65536)); m.w16(jf.POSZ_LO, random.randrange(0, 65536))
    m[jf.M0625] = random.choice([0, 219, 219])
    m[jf.M293D] = random.choice([0, 0xFF, 0xFF])
    hm = bytes(random.randrange(0, 0xC0) for _ in range(256)) if s % 2 else bytes(random.randrange(0x40, 0x80) for _ in range(256))
    seed = random.randrange(1, 255)
    rnd = jf.Lcg(seed)
    init = snap(m); seq = bytearray(); n = 0
    porta = 0xF
    for t in range(random.randrange(150, 400)):
        if t % random.choice([5, 17, 40]) == 0:
            porta = random.choice([0xF, 0xF, 0xE, 0xD, 0xB, 0x7, 0xA, 0x6, 0x9, 0x5])    # bit set = not pressed
        ev = 0
        r = random.random()
        if r < 0.015: ev = 1
        elif r < 0.03: ev = 2
        if ev == 1: jf.key_land(m)
        elif ev == 2: jf.key_takeoff(m)
        try:
            hit = jf.tick(m, porta, sinhi, rnd=rnd, heightmap=hm)
        except jf.Unsupported:
            break
        seq += bytes([ev, porta, 1 if hit else 0]) + snap(m)
        n += 1
    out += struct.pack("<H", n) + init + hm + bytes([seed]) + seq
    nseq += 1
open(os.path.join(OUT, "testdata", "flight_flight.bin"), "wb").write(struct.pack("<H", nseq) + out)
print("flight_flight.bin", nseq, "sequences", len(out) + 2, "bytes, state fields", len(FIELDS))
