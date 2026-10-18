"""Bounded USB-only startup check. Sends only STOP and STATUS; no motion commands."""
import argparse
import json
import re
import time
from datetime import datetime
from pathlib import Path

import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--version', default='ChariotIntegrated-v3.1-R1')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
lines = []
with serial.Serial(port=None, baudrate=115200, timeout=0.2) as connection:
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    connection.open()
    # Opening some adapters can reset the board. Keep requesting STOP as it boots.
    deadline = time.monotonic() + 6
    next_stop = 0
    next_status = 0
    while time.monotonic() < deadline:
        if time.monotonic() >= next_stop:
            connection.write(b'STOP\n')
            connection.flush()
            next_stop = time.monotonic() + 1.5
        # Sample between STOP commands so five neutral frames can be observed.
        if time.monotonic() >= next_status:
            connection.write(b'STATUS\n')
            connection.flush()
            next_status = time.monotonic() + 0.3
        row = connection.readline().decode('utf-8', errors='replace').strip()
        if row:
            lines.append(row)
            print(row, flush=True)
    connection.write(b'STOP\nSTATUS\n')
    connection.flush()
    deadline = time.monotonic() + 1
    while time.monotonic() < deadline:
        row = connection.readline().decode('utf-8', errors='replace').strip()
        if row:
            lines.append(row)
            print(row, flush=True)

statuses = [dict(re.findall(r'(\w+)=([^\s]+)', row)) for row in lines if row.startswith('STATUS ')]
expected = {'fw': args.version, 'hw': '1', 'chassis': '0', 'fly': '0', 'duty': '0',
            'feed': '0', 'TB3': '0', 'servo': '0', 'drive': '0,0,0,0'}
idle_ok = bool(statuses) and all(all(row.get(k) == v for k, v in expected.items()) for row in statuses)
link_ok = any(row.get('ps') == '1' and row.get('buttons') == '0000' and row.get('ready') == '1'
              for row in statuses)
faults = [row for row in lines if row.startswith('FAULT ') or 'Guru Meditation' in row or 'Brownout' in row]
result = {'checked_at': datetime.now().isoformat(), 'port': args.port, 'version': args.version,
          'motor_power_off_confirmed_by_user': True, 'commands_sent': ['STOP', 'STATUS'],
          'idle_pass': idle_ok and not faults, 'controller_neutral_seen': link_ok,
          'status_count': len(statuses), 'faults': faults, 'last_status': statuses[-1] if statuses else None,
          'physical_motion_tested': False}
(args.output / 'startup.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
(args.output / 'startup.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, ensure_ascii=False), flush=True)
raise SystemExit(0 if result['idle_pass'] and link_ok else 2)
