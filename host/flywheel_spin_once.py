"""Execute one FlywheelSpin SPIN without resets or retries; always STOP on exit.

New scheme (2026-10-17): the retired winch/N20 launcher (torque destroyed the motor)
is replaced by two 130 motors with friction flywheels on the original L298N board.
The firmware soft-starts both flywheels to one duty, once per boot, and coasts after.

This tool proves the commanded electrical output only: it cannot see whether the
130 motors are wired, turning, counter-rotating, or drawing how much current.
"""
import argparse
from datetime import datetime, timedelta, timezone
from pathlib import Path
import time
import serial

VERSION = 'FlywheelSpin-v3-100'
ZERO = dict(DUTY1='0', DUTY2='0', IN1='0', IN2='0', IN3='0', IN4='0', STBY='0')
DUTY_MAX = 100
MS_MAX = 10000

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--log', required=True)
    parser.add_argument('--duty', type=int, default=30, help=f'1..{DUTY_MAX} percent (default 30)')
    parser.add_argument('--ms', type=int, default=3000, help=f'1..{MS_MAX} ms (default 3000)')
    parser.add_argument('--d1', type=int, choices=(-1, 1), default=1)
    parser.add_argument('--d2', type=int, choices=(-1, 1), default=1)
    parser.add_argument('--prepare', type=float, default=5, help='idle seconds before SPIN')
    args = parser.parse_args()
    if not 1 <= args.duty <= DUTY_MAX:
        parser.error(f'--duty must be 1..{DUTY_MAX}')
    if not 1 <= args.ms <= MS_MAX:
        parser.error(f'--ms must be 1..{MS_MAX}')
    raw = (args.duty * 255 + 50) // 100
    readback_target = 256 if raw == 255 else raw
    output = Path(args.log)
    output.parent.mkdir(parents=True, exist_ok=True)
    entries = [f'Checked {datetime.now(timezone(timedelta(hours=8))).isoformat()} port={args.port}',
               f'Requested: both flywheels soft-start to {args.duty}% (raw {raw}) for {args.ms}ms, then disable.',
               f'Expected hold peripheral readback: {readback_target} (full-on reads 256 on Arduino-ESP32 3.3.12).',
               f'Polarity d1={args.d1}, d2={args.d2} (experimental; counter-rotation not verified).',
               'One SPIN only; never reset or retry motion.',
               'DUTY readback is the LEDC peripheral value, not the pad level or motor voltage.']
    start = time.monotonic()
    connection = serial.Serial(port=None, baudrate=115200, timeout=.03, write_timeout=1)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    statuses = []
    events = []
    spin_count = 0

    def record(line):
        text = f'{time.monotonic()-start:.3f}s {line}'
        entries.append(text)
        print(text, flush=True)

    def send(command):
        record('TX ' + command)
        connection.write((command + '\n').encode('ascii'))
        connection.flush()

    def receive():
        line = connection.readline().decode('utf-8', errors='replace').strip()
        if not line:
            return
        record('RX ' + line)
        if line.startswith(('FAULT', 'ERR')):
            raise RuntimeError(line)
        if line.startswith('STATUS '):
            s = dict(p.split('=', 1) for p in line.split()[1:] if '=' in p)
            if s.get('fw') != VERSION or s.get('hw') != '1' or s.get('STBY') != '0':
                raise RuntimeError('Unexpected firmware or fault; aborting')
            if s.get('DUTY1') != s.get('DUTY2'):
                raise RuntimeError('Flywheel duties disagree; aborting')
            duty = int(s['DUTY1'])
            expected = dict(ZERO)
            if s.get('phase') == 'RAMP':
                if not 0 <= duty <= readback_target:
                    raise RuntimeError('Ramp duty outside 0..target; aborting')
            elif s.get('phase') == 'HOLD':
                if duty != readback_target:
                    raise RuntimeError('Hold duty differs from expected peripheral value; aborting')
            elif s.get('phase') not in ('IDLE', 'STOPPED', 'DONE'):
                raise RuntimeError('Unexpected phase')
            else:
                duty = 0
            if s.get('phase') in ('RAMP', 'HOLD'):
                expected.update(IN1=str(int(args.d1 > 0)), IN2=str(int(args.d1 < 0)),
                                IN3=str(int(args.d2 > 0)), IN4=str(int(args.d2 < 0)),
                                DUTY1=s['DUTY1'], DUTY2=s['DUTY2'])
            if any(s.get(k) != v for k, v in expected.items()):
                raise RuntimeError('GPIO state inconsistent with phase; aborting')
            statuses.append(s)
        if line.startswith(('START SPIN', 'HOLD duty', 'DONE SPIN')):
            events.append(line)

    def receive_for(seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            receive()

    try:
        connection.open()
        send('STOP'); send('STATUS'); receive_for(1)
        if not statuses or statuses[-1].get('used') != '0' or statuses[-1].get('dirs') != '0':
            raise RuntimeError('Expected unused, unconfigured firmware; no motion issued')
        send(f'DIR {args.d1} {args.d2}'); send('STATUS'); receive_for(.4)
        if any(statuses[-1].get(k) != v for k, v in {
            'dirs': '1', 'd1': str(args.d1), 'd2': str(args.d2), 'used': '0', **ZERO
        }.items()):
            raise RuntimeError('Direction/idle configuration not confirmed')
        record(f'PREPARE {args.prepare:g}s clear of the flywheels; '
               f'then {args.duty}% for {args.ms}ms -> both disabled (flywheel coasts)')
        receive_for(args.prepare)
        send(f'SPIN {args.duty} {args.ms}')
        spin_count += 1
        deadline = time.monotonic() + args.ms / 1000 + 4
        next_status = time.monotonic()
        while time.monotonic() < deadline:
            if time.monotonic() >= next_status:
                send('STATUS')
                next_status = time.monotonic() + .05
            receive()
            if statuses and statuses[-1].get('phase') == 'DONE' and any(
                    e.startswith('DONE SPIN') for e in events):
                break
        if spin_count != 1 or len(events) != 3 or not events[0].startswith('START SPIN') \
                or not events[1].startswith('HOLD duty') or not events[2].startswith('DONE SPIN'):
            raise RuntimeError('Soft-start/hold/completion not fully observed; no retry')
        s = statuses[-1]
        if s.get('phase') != 'DONE' or s.get('used') != '1' or any(s.get(k) != v for k, v in ZERO.items()):
            raise RuntimeError('Final disabled state not confirmed')
        # The run lasts max(ramp, requested ms); a shorter request still pays the ramp.
        if not min(args.ms, 1200) <= int(s.get('elapsed', '-1')) <= max(args.ms, 1200) + 50:
            raise RuntimeError('Elapsed time outside expected tolerance')
        duties = [int(x['DUTY1']) for x in statuses if x.get('phase') == 'RAMP']
        record(f"PASS spin duty={args.duty}% raw={raw} elapsed={s['elapsed']}ms "
               f"ramp_samples={len(duties)} ramp_max={max(duties) if duties else 0} "
               f"both disabled; electrical output only, no mechanical measurement")
    finally:
        try:
            if connection.is_open:
                send('STOP'); send('STATUS')
                deadline = time.monotonic() + .5
                while time.monotonic() < deadline:
                    line = connection.readline().decode('utf-8', errors='replace').strip()
                    if line:
                        record('FINAL ' + line)
        finally:
            connection.close()
            output.write_text('\n'.join(entries) + '\n', encoding='utf-8')
            print(f'Log saved: {output.resolve()}; serial closed', flush=True)

if __name__ == '__main__':
    main()
