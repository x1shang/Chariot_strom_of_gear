"""One bounded N20 jog; physical motion must be observed by the operator."""
import argparse
import json
import time
from pathlib import Path
import serial

p = argparse.ArgumentParser()
p.add_argument('--port', required=True)
p.add_argument('--percent', type=int, required=True)
p.add_argument('--milliseconds', type=int, default=150)
p.add_argument('--direction', type=int, choices=(-1, 1), default=1)
p.add_argument('--power-state', choices=('off', 'n20-only'), required=True)
p.add_argument('--unloaded-confirmed', action='store_true')
p.add_argument('--loaded-confirmed', action='store_true')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
if not 1 <= a.percent <= 100 or not 1 <= a.milliseconds <= 3000:
    p.error('percent must be 1..100 and milliseconds 1..3000')
if a.unloaded_confirmed and a.loaded_confirmed:
    p.error('choose either --unloaded-confirmed or --loaded-confirmed, not both')
if a.power_state == 'n20-only' and not (a.unloaded_confirmed or a.loaded_confirmed):
    p.error('powered jog requires --unloaded-confirmed or --loaded-confirmed')
load_state = 'loaded' if a.loaded_confirmed else ('unloaded' if a.unloaded_confirmed else 'unstated')
a.output.mkdir(parents=True, exist_ok=True)
s = serial.Serial(port=None, baudrate=115200, timeout=.04)
s.dtr = False
s.rts = False
s.port = a.port
s.open()
began = time.monotonic()
rows, statuses, commands = [], [], []
error = None
automatic_stop = False

def send(command):
    s.write((command + '\n').encode('ascii'))
    s.flush()
    commands.append(command)

def read_for(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        data = s.readline()
        if data:
            line = data.decode('utf-8', errors='replace').strip()
            rows.append(f'{time.monotonic()-began:.3f} {line}')
            print(line, flush=True)
            if line.startswith('STATUS '):
                statuses.append(dict(item.split('=', 1) for item in line.split()[1:] if '=' in item))

try:
    send('STOP')
    read_for(.35)
    send('STATUS')
    read_for(.15)
    state = statuses[-1] if statuses else {}
    expected = {'fw': 'ChariotIntegrated-v3.6-HOLDUD', 'hw': '1', 'ps': '1',
                'ready': '1', 'armed': '1', 'buttons': '0000', 'turn': '0',
                'fly': '0', 'feed': '0', 'drive': '0,0,0,0'}
    if not all(state.get(k) == v for k, v in expected.items()):
        raise RuntimeError('Jog refused: version/link/neutral/idle precondition failed')
    send(f'JOG {a.direction} {a.milliseconds} {a.percent}')
    send('STATUS')
    read_for(.06)
    send('STATUS')
    read_for(a.milliseconds / 1000 + .12)
    send('STATUS')
    read_for(.1)
    automatic_stop = bool(statuses) and all(statuses[-1].get(k) == v for k, v in
        {'feed': '0', 'TB3': '0', 'N20pwm': '0,0'}.items())
except Exception as exc:
    error = str(exc)
finally:
    try:
        send('STOP')
        send('STATUS')
        read_for(.2)
    finally:
        s.close()
        final = statuses[-1] if statuses else {}
        active = [v for v in statuses if v.get('feed') == '1' and v.get('TB3') == '1'
                  and v.get('N20pct') == str(a.percent)]
        stopped = all(final.get(k) == v for k, v in
                      {'feed':'0', 'TB3':'0', 'fly':'0', 'drive':'0,0,0,0', 'N20pwm':'0,0'}.items())
        result = {'power_state_confirmed_by_user': a.power_state,
                  'unloaded_confirmed_by_user': a.unloaded_confirmed,
                  'loaded_confirmed_by_user': a.loaded_confirmed,
                  'load_state': load_state,
                  'requested_percent': a.percent, 'requested_ms': a.milliseconds,
                  'direction': a.direction, 'host_commands_sent': commands,
                  'active_statuses': active, 'stopped_at_end': stopped,
                  'timed_stop_before_host_stop': automatic_stop,
                  'error': error, 'last_status': final,
                  'physical_motion': 'Not established by telemetry; operator report required'}
        (a.output / 'jog.txt').write_text('\n'.join(rows) + '\n', encoding='utf-8')
        (a.output / 'jog.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result), flush=True)
if error or not active or not stopped or not automatic_stop:
    raise SystemExit(1)
