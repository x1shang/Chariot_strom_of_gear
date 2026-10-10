"""Read-only motion check after upload: sends STOP and STATUS, never drives."""
import argparse
from datetime import datetime
from pathlib import Path
import re
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
args = parser.parse_args()
log = []
with serial.Serial(port=None, baudrate=115200, timeout=0.2) as connection:
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    connection.open()
    connection.write(b'STOP\nSTATUS\n')
    connection.flush()
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        text = connection.readline().decode('utf-8', errors='replace').strip()
        if text:
            log.append(text)
            print(text, flush=True)
    connection.write(b'STOP\nSTATUS\n')
    connection.flush()
    deadline = time.monotonic() + 1
    while time.monotonic() < deadline:
        text = connection.readline().decode('utf-8', errors='replace').strip()
        if text:
            log.append(text)
            print(text, flush=True)

statuses = [text for text in log if text.startswith('STATUS ') and ' ready=' in text]
pattern = r'^STATUS hw=1 ps=1 mode=(41|73|79) buttons=0000 ready=[01] STBY=0 duty=0,0,0,0$'
passed = bool(statuses) and all(re.fullmatch(pattern, row) for row in statuses)
result = 'PASS startup: hw=1 ps=1 buttons=0000 STBY=0 duty=0,0,0,0' if passed else 'CHECK REQUIRED: startup status did not meet the idle/link checks'
log.append(result)
print(result)
output = Path(__file__).resolve().parent.parent / 'logs' / 'MecanumPS2Dpad-upload-check-2026-10-10.txt'
output.write_text(f'Checked {datetime.now().isoformat()} port={args.port}\nMotor power confirmed OFF by user. No movement command sent.\n' + '\n'.join(log) + '\n', encoding='utf-8')
raise SystemExit(0 if passed else 1)
