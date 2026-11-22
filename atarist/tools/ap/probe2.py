import sys, time
sys.path.insert(0, r"C:\coding\Atari\AltirraBridge-nightly-windows-x86_64\sdk\python")
from altirra_bridge import AltirraBridge
import srv
p, tok = srv.start()
try:
    with AltirraBridge.from_token_file(tok) as a:
        a.boot(r"C:/coding/Atari/rof/flight800/flight800_far_pm_iron.xex")
        a.frame(900)
        b1 = a.bp_set(0x5310); b2 = a.bp_set(0x52AE)
        out = []
        for i in range(8):
            a.resume(); time.sleep(0.4)
            r = a.regs(); out.append((r["PC"], r.get("A")))
        print(out)
        a.bp_clear_all(); a.quit()
finally:
    p.terminate()
