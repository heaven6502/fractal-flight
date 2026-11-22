"""starts AltirraBridgeServer headless and returns (process, token file path)"""
import subprocess, re, time, os
EXE = r"C:\coding\Atari\AltirraBridge-nightly-windows-x86_64\AltirraBridgeServer.exe"
def start(port=6511, errfile=None):
    errfile = errfile or os.path.join(os.environ.get("TEMP", "."), "ap_srv.err")
    f = open(errfile, "w")
    p = subprocess.Popen([EXE, "--bridge=tcp:127.0.0.1:%d" % port], stdout=subprocess.DEVNULL, stderr=f)
    tok = None
    for _ in range(100):
        time.sleep(0.2)
        txt = open(errfile, errors="ignore").read()
        m = re.search(r"token-file:\s*(\S.*)", txt)
        if m: tok = m.group(1).strip(); break
    if not tok: raise SystemExit("server did not start: " + txt[-300:])
    return p, tok
