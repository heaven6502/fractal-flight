"""Reads the profiling block (src/prof.h) of a running PROFILE=1 build through Amiberry's GDB server and prints the stage times.
usage: python tools/readprof.py flight320|flight       (start the program first: tools/run.sh <prog> prof gdb)
The block is found by its magic number "PROF" (the program is loaded into slow RAM at $C00000.. or chip RAM, so both are scanned)."""
import socket, struct, sys

prog = sys.argv[1] if len(sys.argv) > 1 else "flight320"
if prog == "flight320": prog = "f320"
NAMES = {
    "f320":    ["FreeFrame (wait for a free frame)", "QueueClear (waits for the last finish chain)", "F3Prepare (camera tables)", "WaitBlt (the clear)", "snapshot", "  projection (inside the edges)", "  lines (inside the edges)", "QueueFinish", "F3Plot320 (all edges)"],
    "flight":  ["clear scr + init", "UpdateTables", "edges + lines", "wait for the blitter (finish chain)", "snapshot", "flight tick (per tick)", "RenderPrepare total (init + tables)"],
}[prog]
TICK_US = 1.4097          # CIA E clock 709379 Hz
FRAME_US = 20000.0        # PAL 50 Hz

def pkt(b): return b"$" + b + b"#" + b"%02x" % (sum(b) & 255)
s = socket.create_connection(("127.0.0.1", 2345), timeout=20)
def rx():
    d = b""
    while True:
        c = s.recv(65536)
        if not c: break
        d += c
        if d.count(b"#") and len(d.split(b"#")[-1]) >= 2: break
    return d
def cmd(c):
    s.sendall(pkt(c)); r = rx(); s.sendall(b"+"); return r
cmd(b"?")
def readmem(addr, n):
    r = cmd(("m%x,%x" % (addr, n)).encode())
    i = r.find(b"$"); j = r.rfind(b"#")
    body = r[i + 1:j]
    if body.startswith(b"E") or len(body) != 2 * n: return None
    return bytes.fromhex(body.decode())

magic = b"PROF"
found = None
for base, end in ((0xC00000, 0xC80000), (0x000000, 0x080000)):
    a = base
    while a < end and not found:
        blk = readmem(a, 2048 + 8)
        if blk is not None:
            k = blk.find(magic)
            if k >= 0 and k + 52 <= len(blk):
                found = a + k
        a += 2048
    if found: break
if not found:
    sys.exit("PROF block not found (is a PROFILE=1 build running, and has the main loop run at least once?)")
raw = readmem(found, 4 * 19)
magic_, n, frames, *t = struct.unpack(">19L", raw)
print("block at $%X: %d loop iterations, %.2f frames per iteration" % (found, n, frames / max(n, 1)))
if n == 0: sys.exit()
tot = 0
for i, name in enumerate(NAMES):
    if name == "-": continue
    if "per tick" in name:
        continue
    avg = t[i] / n
    print("  %-22s %8.0f ticks  %7.2f ms  %5.2f frames" % (name, avg, avg * TICK_US / 1000, avg * TICK_US / FRAME_US))
if prog == "flight":
    ticks = frames / 2.0                                    # flight tick every second frame
    print("  flight tick (each)     %8.0f ticks  %7.2f ms   (runs in the interrupt, every 2nd frame)" % (t[5] / max(ticks, 1), t[5] / max(ticks, 1) * TICK_US / 1000))
if prog == "flight":
    print("  inside the edge stage (per picture):")
    def ms(k): return t[k] / n * TICK_US / 1000
    print("    ProjectVertex          %7.2f ms" % ms(7))
    print("    DrawEdge (all)         %7.2f ms   %6.1f calls" % (ms(8), t[10] / n))
    print("    Line51C (inside)       %7.2f ms   %6.1f calls, %7.1f loop iterations" % (ms(9), t[11] / n, t[15] / n))
    print("    PlotCol calls %7.1f, plotted pixels %7.1f, Mid3A7 calls %7.1f" % (t[12] / n, t[13] / n, t[14] / n))
