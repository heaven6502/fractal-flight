"""atari_fly.py [seconds] - flies the Atari 800 demo flight (flight800_far_pm_iron.xex, autopilot on) in AltirraBridgeServer and writes tools/ap/atari_fly.txt:
the 256 byte terrain ($0900) and one line per second after the flight started: jiffies since the start, clearance, alt, speed, thrust, heading, posX, posZ"""
import sys, os
sys.path.insert(0, r"C:\coding\Atari\AltirraBridge-nightly-windows-x86_64\sdk\python")
from altirra_bridge import AltirraBridge
import srv
HERE = os.path.dirname(os.path.abspath(__file__))
SEC = int(sys.argv[1]) if len(sys.argv) > 1 else 60
def w(a, ad): return a.peek(ad)[0] | a.peek(ad + 1)[0] << 8
p, tok = srv.start()
try:
    with AltirraBridge.from_token_file(tok) as a:
        a.boot(r"C:/coding/Atari/rof/flight800/flight800_far_pm_iron.xex")
        a.frame(60)
        # wait for the flight: the position leaves its start value (X hi = $80 at the start) after the intro
        start_clock = None
        for i in range(2000):
            a.frame(5)
            px = w(a, 0x289B)
            if px != 0x8000 and px != 0 and start_clock is None:
                start_clock = i
                break
        hm = a.memdump(0x0900, 256)
        ADDRS = [0x2886,0x2887,0x2888,0x2889,0x288A,0x288B,0x288C,0x288D,0x288E,0x2884,0x2891,0x2892,0x2893,0x2894,0x2895,0x2896,0x2897,0x2898,
                 0x2899,0x289A,0x289B,0x289C,0x289D,0x289E,0x289F,0x28A0,0x2878,0x287A,0x28F0,0x28F1,0x28F2,0x28F3,
                 0x0626,0x28A5,0x061C,0x065F,0x28A4,0x293D,0x283F,0x0625,0x061D]
        state = [a.peek(x)[0] for x in ADDRS] + [a.peek(0x549F)[0], a.peek(0x54A0)[0], a.peek(0x54A1)[0], a.peek(0x54A2)[0]]   # ... ap_ph, ap_t, ap_l, ap_r
        lines = []
        j0 = None
        for s in range(SEC):
            clock = (a.peek(0x12)[0] << 16) | (a.peek(0x13)[0] << 8) | a.peek(0x14)[0]
            if j0 is None: j0 = clock
            lines.append("%d %d %d %d %d %d %d %d" % (clock - j0, a.peek(0x61D)[0], w(a, 0x289F), w(a, 0x2897), a.peek(0x2887)[0], w(a, 0x2899), w(a, 0x289B), w(a, 0x289D)))
            a.frame(50)
        open(os.path.join(HERE, "atari_fly.txt"), "w").write(hm.hex() + "\n" + " ".join("%d" % x for x in state) + "\n" + "\n".join(lines) + "\n")
        a.quit()
finally:
    p.terminate()
print("done")
