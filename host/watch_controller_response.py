"""Observe human PS2 input and firmware commands; send STATUS, then final STOP only."""
import argparse
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import re
import time

import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--seconds', type=int, default=45, choices=range(5, 61))
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--chassis-power-state', choices=['on', 'off', 'unknown'], default='unknown')
parser.add_argument('--wheels-raised-confirmed', action='store_true')
parser.add_argument('--reset-first', action='store_true', help='Pulse RTS reset; requires motor power off')
args = parser.parse_args()
if args.reset_first and args.chassis_power_state != 'off':
    parser.error('--reset-first requires --chassis-power-state off')
args.output.mkdir(parents=True, exist_ok=True)
stamp = datetime.now(timezone(timedelta(hours=8))).isoformat()
rows, statuses, faults = [], [], []
started = time.monotonic()
last_signature = None

def receive(connection, until, poll=True):
    global last_signature
    next_request = 0
    while time.monotonic() < until:
        if poll and time.monotonic() >= next_request:
            connection.write(b'STATUS\n')
            connection.flush()
            next_request = time.monotonic() + 0.1
        row = connection.readline().decode('utf-8', errors='replace').strip()
        if not row:
            continue
        elapsed = round(time.monotonic() - started, 3)
        rows.append(f'{elapsed:.3f} {row}')
        if row.startswith('FAULT ') or 'Brownout' in row or 'Guru Meditation' in row:
            faults.append(row)
            return
        if not row.startswith('STATUS '):
            print(f'{elapsed:.3f} {row}', flush=True)
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', row))
        fields['elapsed_s'] = elapsed
        statuses.append(fields)
        signature = tuple(fields.get(k) for k in ['hw', 'ps', 'ready', 'buttons', 'RX', 'turn', 'chassis', 'drive', 'fly', 'feed'])
        if signature != last_signature:
            print(f'{elapsed:.3f} {row}', flush=True)
            last_signature = signature
        if fields.get('hw') == '0':
            faults.append('hw=0')
            return

with serial.Serial(port=None, baudrate=115200, timeout=0.08) as connection:
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    connection.open()
    try:
        if args.reset_first:
            connection.dtr = False
            connection.rts = True
            time.sleep(0.15)
            connection.rts = False
        receive(connection, started + args.seconds)
    finally:
        connection.write(b'STOP\nSTATUS\n')
        connection.flush()
        receive(connection, time.monotonic() + 0.4, poll=False)

inputs = [r for r in statuses if r.get('ps') == '1' and (int(r.get('buttons', '0'), 16) or int(r.get('turn', '0')))]
drive_rows = [r for r in statuses if r.get('chassis') == '1' and any(int(x) for x in r.get('drive', '0,0,0,0').split(','))]
last = statuses[-1] if statuses else {}
result = {'started_at': stamp, 'duration_s': args.seconds, 'port': args.port,
          'chassis_power_state_confirmed_by_user': args.chassis_power_state,
          'wheels_raised_confirmed_by_user': args.wheels_raised_confirmed,
          'host_commands_sent': ['STATUS', 'STOP'], 'host_motion_commands_sent': False,
          'reset_first': args.reset_first,
          'status_count': len(statuses), 'input_sample_count': len(inputs),
          'drive_output_sample_count': len(drive_rows),
          'observed_button_values': sorted(set(r.get('buttons', '') for r in statuses)),
          'versions_seen': sorted(set(r.get('fw', '') for r in statuses)),
          'timer_frequencies_seen': sorted(set(r.get('pwmhz', '') for r in statuses)),
          'fault_reasons_seen': sorted(set(r.get('fault', '') for r in statuses)),
          'observed_turn_range': [min((int(r.get('turn', '0')) for r in statuses), default=0),
                                  max((int(r.get('turn', '0')) for r in statuses), default=0)],
          'faults': faults, 'last_status': last,
          'stopped_at_end': bool(last) and all(last.get(k) == v for k, v in
                  {'chassis': '0', 'drive': '0,0,0,0', 'fly': '0', 'feed': '0', 'TB3': '0'}.items()),
          'physical_wheel_motion_observation': 'awaiting user report; telemetry alone is not motion evidence'}
(args.output / 'controller-response.txt').write_text('\n'.join(rows) + '\n', encoding='utf-8')
(args.output / 'controller-response.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, ensure_ascii=True), flush=True)
raise SystemExit(0 if statuses and not faults and result['stopped_at_end'] else 2)
