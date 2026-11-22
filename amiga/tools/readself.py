"""Reads the result block of an Amiga self-test (src/flight_selftest.c) from the running emulator through the GDB server.
usage: python tools/readself.py [flight]
       (start it first: tools/run.sh fselftest gdb; the block has the magic number "SELF")"""
import socket, struct, sys

prog = sys.argv[1] if len(sys.argv) > 1 else "flight"
MAGIC, FMT, SIZE = {"flight": (b"SELF", ">10L", 40)}[prog]

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

hits = []                      # the data of the program may also sit in a disk buffer: take the block that has `done` set
for base, end in ((0xC00000, 0xC80000), (0x000000, 0x080000)):
    a = base
    while a < end:
        blk = readmem(a, 2048 + 8)
        if blk is not None:
            i = 0
            while True:
                k = blk.find(MAGIC, i)
                if k < 0 or k >= 2048: break
                hits.append(a + k); i = k + 1
        a += 2048
if not hits: sys.exit("self-test block not found (is the self-test build running?)")
found = hits[-1]
for h in hits:
    d = struct.unpack(FMT, readmem(h, SIZE))
    if d[1] == 1: found = h
d = struct.unpack(FMT, readmem(found, SIZE))
print("block at $%X: finished=%d cases=%d" % (found, d[1], d[2]))
if prog == "flight":
    magic, done, cases, planeDiffs, rAC, rC6, rA6, tC, tA, first = d
    print("  assembly plane differs from the C plane in %d bytes; ridge buffer asm-vs-C %d columns" % (planeDiffs, rAC))
    print("  ridge buffer vs the 6502: C %d columns, assembly %d columns differ" % (rC6, rA6))
else:
    magic, done, cases, planeDiffs, tC, tA, first, plots = d
    print("  assembly planes differ from the C planes in %d bytes (%d non-grey plane 0 bytes drawn in total)" % (planeDiffs, plots))
print("  first bad case: %s" % ("none" if first == 0xFFFF else first))
if cases:
    unit = "RenderScene" if prog == "flight" else "edge loop"
    print("  %s on average: C %.1f ms, assembly %.1f ms (%.2fx)" % (unit, tC / cases * 1.4097 / 1000, tA / cases * 1.4097 / 1000, tC / max(tA, 1)))
