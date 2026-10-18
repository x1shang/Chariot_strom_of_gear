"""Timestamped R1 pulse monitor: polls STATUS and logs feed transitions only.

Survives the USB port disappearing mid-run (reopens when it comes back), so a
cable or brownout hiccup does not throw away the whole capture.
"""
import serial, time, sys
from pathlib import Path

port = sys.argv[1]
seconds = float(sys.argv[2])
out = Path(sys.argv[3])

rows = []
last_feed = None
start_t = None
t0 = time.monotonic()


def log(text):
    stamp = time.monotonic() - t0
    rows.append(f"{stamp:7.2f} {text}")
    print(f"{stamp:7.2f} {text}", flush=True)


def connect():
    s = serial.Serial(port=None, baudrate=115200, timeout=0.05)
    s.dtr = False
    s.rts = False
    s.port = port
    s.open()
    s.reset_input_buffer()
    return s


s = None
waiting_logged = False
last_poll = 0.0
try:
    while time.monotonic() - t0 < seconds:
        if s is None:
            try:
                s = connect()
                log("PORT OPEN (reconnected)" if waiting_logged else "PORT OPEN")
                waiting_logged = False
            except Exception as exc:
                if not waiting_logged:
                    log(f"WAIT port {port}: {type(exc).__name__}")
                    waiting_logged = True
                time.sleep(1.0)
                continue
        try:
            if time.monotonic() - last_poll >= 0.2:
                last_poll = time.monotonic()
                s.write(b"STATUS\n")
                s.flush()
            line = s.readline()
        except Exception as exc:
            log(f"PORT LOST: {type(exc).__name__}; will retry")
            try:
                s.close()
            except Exception:
                pass
            s = None
            waiting_logged = True
            continue
        if not line:
            continue
        text = line.decode("utf-8", "replace").strip()
        if not text.startswith("STATUS "):
            log(f"RAW {text}")
            continue
        st = dict(i.split("=", 1) for i in text.split()[1:] if "=" in i)
        feed = st.get("feed")
        if feed != last_feed:
            if feed == "1":
                start_t = time.monotonic() - t0
                log(f"START next={st.get('N20next')} pct={st.get('N20pct')} ms_cfg={st.get('N20ms')} "
                    f"TB3={st.get('TB3')} pwm={st.get('N20pwm')} armed={st.get('armed')}")
            else:
                dur = (time.monotonic() - t0 - start_t) if start_t is not None else -1
                log(f"STOP  duration={dur:.2f}s next={st.get('N20next')} last={st.get('N20last')} "
                    f"ms_cfg={st.get('N20ms')} fault={st.get('fault')}")
            last_feed = feed
finally:
    if s is not None:
        try:
            s.close()
        except Exception:
            pass
    out.write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"[monitor finished after {seconds:.0f}s; {len(rows)} events]")
