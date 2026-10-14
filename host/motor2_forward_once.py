"""Issue one FWD3S to N20Motor2Fwd3s-v2; never retry motion or reset the MCU.

Requires the user to confirm motor2 wiring, remaining travel and power state.
Firmware must boot idle; motor1 and chassis must stay disabled.
"""
import argparse
from datetime import datetime
from pathlib import Path
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--log', required=True)
    parser.add_argument('--prepare', type=float, default=5)
    args = parser.parse_args()
    if not 0 <= args.prepare <= 10:
        parser.error('--prepare must be 0..10 seconds')
    output = Path(args.log)
    output.parent.mkdir(parents=True, exist_ok=True)
    entries = [f'Checked {datetime.now().isoformat()} port={args.port}',
               'User confirmed L298N channel wiring/jumpers, remaining travel and test power.',
               'Motor1 release output must stay disabled. Single motor2 FWD3S only.']
    start = time.monotonic()
    connection = serial.Serial(port=None, baudrate=115200, timeout=.05, write_timeout=1)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    statuses = []
    saw_start = saw_done = False

    def record(line):
        entries.append(f'{time.monotonic()-start:.3f}s {line}')
        print(entries[-1], flush=True)

    def send(command):
        record('TX ' + command)
        connection.write((command + '\n').encode('ascii'))
        connection.flush()

    def receive():
        nonlocal saw_start, saw_done
        line = connection.readline().decode('utf-8', errors='replace').strip()
        if not line:
            return
        record('RX ' + line)
        if line.startswith('STATUS '):
            status = dict(part.split('=', 1) for part in line.split()[1:] if '=' in part)
            if status.get('fw') != 'N20Motor2Fwd3s-v2' or status.get('hw') != '1':
                raise RuntimeError('Unexpected firmware or hardware fault; refusing motion')
            if status.get('ENA') != '0' or status.get('STBY') != '0':
                raise RuntimeError('Motor1/chassis reported enabled; aborting')
            statuses.append(status)
        if line.startswith('START FWD3S '):
            saw_start = True
        if line.startswith('DONE FWD3S '):
            saw_done = True
        if line.startswith('ERR') or line.startswith('FAULT'):
            raise RuntimeError(line)

    try:
        connection.open()
        send('STOP')
        send('STATUS')
        deadline = time.monotonic() + 1.5
        while time.monotonic() < deadline:
            receive()
        if not statuses or any(statuses[-1].get(k) != v for k, v in {
            'used':'0', 'running':'0', 'ENA':'0', 'ENB':'0',
            'IN3':'0', 'IN4':'0', 'STBY':'0',
        }.items()):
            raise RuntimeError('Expected unused idle firmware; no movement command sent')
        record(f'PREPARE {args.prepare:g}s; motor2 will run forward for 3s once')
        deadline = time.monotonic() + args.prepare
        while time.monotonic() < deadline:
            receive()
        send('FWD3S')  # Exactly one movement command; no retry under any failure.
        deadline = time.monotonic() + 7
        next_status = time.monotonic()
        while time.monotonic() < deadline:
            if time.monotonic() >= next_status:
                send('STATUS')
                next_status = time.monotonic() + .25
            receive()
            if saw_done and statuses and statuses[-1].get('running') == '0' and statuses[-1].get('used') == '1':
                break
        if not (saw_start and saw_done and statuses and statuses[-1].get('ENB') == '0'):
            raise RuntimeError('Completion not confirmed; STOP sent, movement will not be retried')
        record('PASS firmware start/3s completion/disable; physical direction requires user observation')
    finally:
        try:
            if connection.is_open:
                send('STOP')
                send('STATUS')
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
