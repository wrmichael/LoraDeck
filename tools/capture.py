# Record the LoRaDeck console to tools\monitor.log (restarts the board first, like idf.py monitor).
# Usage: capture.bat [COMx] [seconds]. Picks the Heltec's CP210x USB port by itself.
import sys, time, os
import serial, serial.tools.list_ports

here = os.path.dirname(os.path.abspath(__file__))
port = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].upper().startswith("COM") else None
secs = int(sys.argv[-1]) if len(sys.argv) > 1 and sys.argv[-1].isdigit() else 90
out = open(os.path.join(here, "monitor.log"), "w", encoding="utf-8", errors="replace")

def log(s):
    out.write(s); out.flush(); print(s, end="")

ports = list(serial.tools.list_ports.comports())
for p in ports:
    log(f"# port {p.device}: {p.description} [{p.hwid}]\n")
if not port:
    pick = [p.device for p in ports if "10C4" in (p.hwid or "").upper() or "CP210" in (p.description or "").upper()]
    port = pick[0] if pick else "COM9"
log(f"# using {port} for {secs} s\n")
try:
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = False
    s.rts = False
    s.open()
except Exception as e:
    log(f"# could not open {port}: {e}\n# (close any idf.py monitor window and try again)\n")
    sys.exit(1)
s.rts = True                     # reset the board (EN low), then let it run
time.sleep(0.2)
s.rts = False
t0, buf = time.time(), b""
while time.time() - t0 < secs:
    d = s.read(4096)
    if not d:
        continue
    buf += d
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        log(f"[{time.time() - t0:7.2f}] " + line.decode("utf-8", "replace").rstrip("\r") + "\n")
