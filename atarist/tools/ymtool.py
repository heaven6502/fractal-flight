#!/usr/bin/env python
"""ymtool.py - YM2149 music for the Atari ST programs (src/st_sys.c: StYmStart, played by an MFP timer at 50 Hz).

  python tools/ymtool.py conv in.ym  out.ymr      convert a YM3!/YM5!/YM6! register dump (LHA packed or not) -- digidrums / special effects are ignored
  python tools/ymtool.py demo        out.ymr      write a small demo tune (3 voices + noise percussion)
  python tools/ymtool.py info  tune.ymr           print frames, loop and size

.ymr format (all big endian):  u32 loop offset (bytes from the start of the frame data)  |  frames ...  |  u16 $FFFF
  frame = u16 mask (bit r set = register r follows, r = 0 .. 13)  +  one byte per set bit.
  The first frame and the loop frame write all registers 0 .. 12 (13 only if used), so the loop is exact.  $FFFF at the end = jump to the loop offset.
"""
import struct, sys, math

REGS = 14
MASKS = [0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F]


def write_ymr(frames, loop, path, env_used=True):
    """frames: list of 14 register values (reg 13 = 0xFF means 'no write'); loop: index of the loop frame."""
    out = bytearray(b"\0\0\0\0")
    prev = None
    loop_off = 0
    for i, f in enumerate(frames):
        if i == loop:
            loop_off = len(out) - 4
            prev = None                               # the loop frame writes everything
        mask = 0
        data = bytearray()
        for r in range(REGS):
            v = f[r]
            if r == 13:
                if v == 0xFF:
                    continue                        # no write
                if prev is not None and prev[13] == v:
                    continue                        # writing the shape register restarts the envelope: only when it changes
            elif prev is not None and prev[r] == v:
                continue
            mask |= 1 << r
            data.append(v & 0xFF)
        out += struct.pack(">H", mask) + data
        prev = f[:]
    out += b"\xff\xff"
    out[0:4] = struct.pack(">I", loop_off)
    open(path, "wb").write(out)
    return len(out)


def conv(src, dst):
    raw = open(src, "rb").read()
    if raw[:2] not in (b"YM",):                      # LHA archive (-lh5-) around the real file
        import lhafile, io
        z = lhafile.LhaFile(io.BytesIO(raw))
        raw = z.read(z.namelist()[0])
    tag = raw[:4]
    if tag in (b"YM5!", b"YM6!"):
        assert raw[4:12] == b"LeOnArD!"
        n, attr, ndd, clock, rate, loop, extra = struct.unpack(">IIHIHIH", raw[12:34])
        pos = 34 + extra
        for _ in range(ndd):                         # skip the digidrum samples
            sz = struct.unpack(">I", raw[pos:pos + 4])[0]; pos += 4 + sz
        for _ in range(3):                           # name, author, comment
            e = raw.index(b"\0", pos); print(raw[pos:e].decode("latin1")); pos = e + 1
        data = raw[pos:pos + n * REGS]
        if attr & 1:
            cols = [data[r * n:(r + 1) * n] for r in range(REGS)]
            frames = [[cols[r][i] for r in range(REGS)] for i in range(n)]
        else:
            frames = [list(data[i * REGS:(i + 1) * REGS]) for i in range(n)]
        print("YM%s: %d frames at %d Hz, clock %d, loop frame %d, %d digidrums" % (tag[2:3].decode(), n, rate, clock, loop, ndd))
        if clock != 2000000:
            print("warning: master clock %d is not the ST's 2 MHz: pitches will be off" % clock)
        if rate != 50:
            print("warning: frame rate %d (the player runs at 50 Hz)" % rate)
    elif tag == b"YM3!":
        data = raw[4:]
        n = len(data) // REGS
        frames = [[data[r * n + i] for r in range(REGS)] for i in range(n)]
        loop = 0
        print("YM3: %d frames" % n)
    else:
        raise SystemExit("unsupported format %r (YM3!, YM5!, YM6! are)" % tag)
    for f in frames:
        for r in range(REGS):
            f[r] &= MASKS[r] if r != 13 else 0xFF
    size = write_ymr(frames, loop, dst)
    print("wrote %s: %d bytes (%.1f bytes per frame)" % (dst, size, size / len(frames)))


def demo(dst):
    """A minor / F / C / G, 3 voices: bass, arpeggio, lead; noise ticks on the beat."""
    def per(note):                                    # MIDI note -> YM tone period (2 MHz / 16 / f)
        return int(round(125000.0 / (440.0 * 2 ** ((note - 69) / 12.0))))
    chords = [(57, [69, 72, 76]), (53, [65, 69, 72]), (48, [67, 72, 76]), (55, [67, 71, 74])]    # bass, chord tones
    lead = [81, 79, 76, 79, 81, 84, 83, 81,  79, 77, 76, 77, 79, 81, 79, 76]                      # one bar of eighth notes... two per chord
    frames = []
    beat = 25                                         # 50 frames per second, 120 bpm
    for bar in range(16):
        bass, tones = chords[bar % 4]
        for fr in range(4 * beat):
            t = fr // 6
            f = [0] * REGS
            f[13] = 0xFF
            pb = per(bass - 12)
            f[0], f[1] = pb & 255, pb >> 8                   # A: bass
            pa = per(tones[(fr // 3) % 3])                    # B: arpeggio, a new note every 3 frames
            f[2], f[3] = pa & 255, pa >> 8
            idx = (bar * 8 + fr // 12) % len(lead)
            pl = per(lead[idx] if bar >= 2 else lead[idx] - 12)   # C: lead melody (an octave lower in the first two bars)
            if bar < 2:
                pl = per(tones[(fr // 12) % 3] - 12)
            f[4], f[5] = pl & 255, pl >> 8
            f[6] = 12                                         # noise period
            bt = fr % beat
            noise_on = (bt < 2) and (fr // beat) % 2 == 1      # a hi-hat tick on beats 2 and 4
            mixer = 0b111000                                  # noise off, tone on in all three channels
            if noise_on:
                mixer &= ~(1 << 5)                            # noise on in channel C: bits 3..5 = noise off A..C
            f[7] = 0xC0 | mixer
            f[8] = max(0, 14 - (bt * 14) // beat)             # bass: decays within a beat
            f[9] = 8 if (fr % 3) != 2 else 6                  # arpeggio
            f[10] = max(0, 13 - ((fr % 12) * 13) // 12) if not noise_on else 10   # lead: short decay per note
            frames.append(f)
    size = write_ymr(frames, 0, dst)
    print("wrote %s: %d frames (%.1f s), %d bytes" % (dst, len(frames), len(frames) / 50.0, size))


def info(path):
    d = open(path, "rb").read()
    loop = struct.unpack(">I", d[:4])[0]
    p, n = 4, 0
    while True:
        m = struct.unpack(">H", d[p:p + 2])[0]
        if m == 0xFFFF:
            break
        p += 2 + bin(m).count("1"); n += 1
    print("%d frames (%.1f s at 50 Hz), loop at byte %d, %d bytes" % (n, n / 50.0, loop, len(d)))


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == "conv": conv(a[1], a[2])
    elif len(a) == 2 and a[0] == "demo": demo(a[1])
    elif len(a) == 2 and a[0] == "info": info(a[1])
    else: print(__doc__)
