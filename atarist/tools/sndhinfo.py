#!/usr/bin/env python
"""sndhinfo.py tune.sndh - header of an SNDH file: title, composer, sub tunes, replay rate, flags (what src/st_sndh.c will do with it).
Install a tune:  copy it to music/tune.sndh and run ./build.sh.  ICE! packed files must be unpacked first (unice68 / sndh archive downloads are usually plain)."""
import sys, re
d = open(sys.argv[1], "rb").read()
if d[:4] == b"ICE!":
    sys.exit("ICE! packed: unpack it first (e.g. unice68 in out)")
if d[12:16] != b"SNDH":
    sys.exit("not an SNDH file (no 'SNDH' at offset 12)")
end = d.find(b"HDNS")
h = d[16:end]
def s(tag):
    m = re.search(tag + rb"([^\0]*)\0", h); return m.group(1).decode("latin1") if m else "?"
print("title    :", s(b"TITL")); print("composer :", s(b"COMM")); print("ripper   :", s(b"RIPP")); print("year     :", s(b"YEAR"))
m = re.search(rb"##(\d\d)", h); print("sub tunes:", int(m.group(1)) if m else 1)
m = re.search(rb"#!(\d\d)", h); print("default  :", int(m.group(1)) if m else 1)
m = re.search(rb"T([ABCD])(\d+)\0", h); v = re.search(rb"!V(\d+)\0", h)
print("replay   : %s Hz (%s)" % ((m.group(2).decode(), "timer " + m.group(1).decode() + " tag; the ST player always uses timer C") if m else (v.group(1).decode(), "VBL tag") if v else ("50", "default")))
m = re.search(rb"FLAG~([^\0]*)\0", h); print("flags    :", m.group(1).decode() if m else "-", "(a/b/c/d = uses that timer itself, e = STe, y = YM2149, g = digi, k = kills the system, B = blitter, s = DSP...)")
print("size     : %d bytes" % len(d))
