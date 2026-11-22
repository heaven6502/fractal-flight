"""ap_check.py [cases] - checks the C autopilot (flight_core.c) against the original 6502 code of flight800_far_pm_iron.asm running in AltirraBridgeServer.
The 6502 routine is assembled into tools/ap/ap_unit.xex (make_unit.py copies it verbatim from the Atari source).  Random cases go to both; the results must be identical."""
import sys, random, subprocess, os, re
sys.path.insert(0, r"C:\coding\Atari\AltirraBridge-nightly-windows-x86_64\sdk\python")
from altirra_bridge import AltirraBridge
import srv
HERE = os.path.dirname(os.path.abspath(__file__))
N = int(sys.argv[1]) if len(sys.argv) > 1 else 400
NV = 55
lst = open(os.path.join(HERE, "ap_unit.lst"), errors="ignore").read()
def label(name):
    m = re.search(r"^\s*\d+\s+([0-9A-F]{4})\s+(?:[0-9A-F]{2}\s+)*\s*%s\s" % name, lst, re.M)
    return int(m.group(1), 16)
AP_ON, AP_PH, AP_T = label("ap_on"), label("ap_ph"), label("ap_t")

rnd = random.Random(20261106)
cases = []
for i in range(N):
    alt = rnd.choice([rnd.randint(0x10, 0x30), 0x18, 0x19, 0x1A, 0x1E, 0x1F, 0x20])
    clr = rnd.choice([rnd.randint(0, 60), 19, 20, 21, 0])
    thr = rnd.choice([rnd.randint(0, 110), 0, 1, 99, 100, 30])
    m65 = rnd.choice([0xFF, 0xFF, 0xFF, 0])
    t = rnd.choice([0, 1, 2, 3, 40, 249, 250, rnd.randint(0, 250)])
    ph = rnd.randint(0, 4) if t == 0 else rnd.randint(1, 4)
    verts = []
    for v in range(NV):
        fl = rnd.choice([0, 0, 0, 0, 0x20, 0x40, 0x80, 0x10 * rnd.randint(0, 15)])
        vh = rnd.choice([rnd.randint(0, 255), rnd.randint(100, 120), 110, 109])
        vz = rnd.choice([rnd.randint(0, 60), rnd.randint(4, 6), 39, 40, rnd.randint(0, 255)])
        vx = rnd.choice([rnd.randint(0, 255), rnd.randint(0, 6), rnd.randint(0xF9, 0xFF), 4, 5, 0xFA, 0xFB])
        verts.append((fl, vh, vz, vx))
    cases.append((alt, clr, thr, m65, ph, t, verts))

# ---- the original 6502 code
atari = []
p, tok = srv.start()
try:
    with AltirraBridge.from_token_file(tok) as a:
        a.boot(os.path.join(HERE, "ap_unit.xex").replace("\\", "/"))
        a.frame(30)
        for (alt, clr, thr, m65, ph, t, verts) in cases:
            a.memload(0x1180, bytes(v[3] for v in verts))      # VXH
            a.memload(0x1200, bytes(v[2] for v in verts))      # VZH
            a.memload(0x12C0, bytes(v[1] for v in verts))      # VH
            a.memload(0x1400, bytes(v[0] for v in verts))      # VFLAG
            a.poke(0x28A0, alt); a.poke(0x061D, clr); a.poke(0x2887, thr); a.poke(0x065F, m65)
            a.poke(AP_ON, 1); a.poke(AP_PH, ph); a.poke(AP_T, t); a.poke(0x02FC, 0xFF)
            a.poke(0x0600, 1)
            a.frame(2)
            assert a.peek(0x0600)[0] == 0, "harness did not run"
            o = a.peek(0x0610, 7)
            atari.append((o[0] & 0x0F, o[1], o[2], o[3], o[4], o[5]))
        a.quit()
finally:
    p.terminate()

# ---- the C code
cf = os.path.join(HERE, "cases.txt")
with open(cf, "w") as f:
    for (alt, clr, thr, m65, ph, t, verts) in cases:
        f.write(" ".join("%x" % x for x in [alt, clr, thr, m65, ph, t] + [b for v in verts for b in v]) + "\n")
exe = os.path.join(HERE, "ap_check.exe")
subprocess.check_call(["gcc", "-O1", "-DPLOT_PLANAR", "-DAP_DEAD=1", "-DAP_PULSE=1", "-I" + os.path.join(HERE, "..", "..", "src"), "-o", exe, os.path.join(HERE, "ap_check.c"), os.path.join(HERE, "..", "..", "src", "flight_data.c")])
res = subprocess.run([exe, cf], capture_output=True, text=True).stdout.split("\n")
cres = [tuple(int(x, 16) for x in l.split()) for l in res if l.strip()]
bad = 0
for i, (x, y) in enumerate(zip(atari, cres)):
    if x != tuple(y):
        bad += 1
        if bad <= 8: print("case %d differs: atari %s  C %s  inputs %s" % (i, x, y, cases[i][:6]))
print("%d cases, %d differences" % (len(cases), bad))
sys.exit(1 if bad else 0)
