"""Execute one N20Sequence RUN without resets or retries; always STOP on exit.

User confirmed motor2 +1 is clockwise and motor1 +1 is counterclockwise.
The defaults use motor1 -1, motor2 +1 for clockwise; no resets or retries.
"""
import argparse
from datetime import datetime, timedelta, timezone
from pathlib import Path
import time
import serial

VERSION = 'N20Sequence-3s-500ms-v1'
ZERO = dict(ENA='0', ENB='0', IN1='0', IN2='0', IN3='0', IN4='0', STBY='0')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--log', required=True)
    parser.add_argument('--d1', type=int, choices=(-1, 1), default=-1)
    parser.add_argument('--d2', type=int, choices=(-1, 1), default=1)
    args = parser.parse_args()
    output = Path(args.log)
    output.parent.mkdir(parents=True, exist_ok=True)
    entries = [f'Checked {datetime.now(timezone(timedelta(hours=8))).isoformat()} port={args.port}',
               'Requested: motor2 forward 3000ms, disable motor2, motor1 forward 500ms, disable all.',
               f'Polarity d1={args.d1}, d2={args.d2}; observed clockwise mapping is motor1=-1, motor2=1.',
               'One RUN only; never reset or retry motion.']
    start = time.monotonic()
    connection = serial.Serial(port=None, baudrate=115200, timeout=.03, write_timeout=1)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    statuses = []
    events = []
    command_count = 0

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
            expected = ZERO.copy()
            if s.get('phase') == 'MOTOR2':
                expected.update(ENB='1', IN3=str(int(args.d2 > 0)), IN4=str(int(args.d2 < 0)))
            elif s.get('phase') == 'MOTOR1':
                expected.update(ENA='1', IN1=str(int(args.d1 > 0)), IN2=str(int(args.d1 < 0)))
            elif s.get('phase') not in ('IDLE', 'STOPPED', 'DONE'):
                raise RuntimeError('Unexpected phase')
            if any(s.get(k) != v for k, v in expected.items()):
                raise RuntimeError('GPIO state inconsistent with sequence; aborting')
            statuses.append(s)
        if line.startswith(('START SEQUENCE', 'TRANSITION ', 'DONE SEQUENCE')):
            events.append(line)

    def receive_for(seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            receive()

    try:
        connection.open()
        send('STOP'); send('STATUS'); receive_for(1)
        if not statuses or statuses[-1].get('used') != '0' or statuses[-1].get('phase') != 'STOPPED':
            raise RuntimeError('Expected unused idle firmware; no motion issued')
        send(f'DIR {args.d1} {args.d2}'); send('STATUS'); receive_for(.4)
        if any(statuses[-1].get(k) != v for k, v in {
            'dirs':'1', 'd1':str(args.d1), 'd2':str(args.d2), 'used':'0', **ZERO
        }.items()):
            raise RuntimeError('Direction/idle configuration not confirmed')
        record('PREPARE 5s; then motor2 3s -> motor1 0.5s -> all stopped')
        receive_for(5)
        send('RUN')
        command_count += 1
        deadline = time.monotonic() + 7
        next_status = time.monotonic()
        while time.monotonic() < deadline:
            if time.monotonic() >= next_status:
                send('STATUS')
                next_status = time.monotonic() + .05
            receive()
            if statuses and statuses[-1].get('phase') == 'DONE' and any(e.startswith('DONE SEQUENCE') for e in events):
                break
        if command_count != 1 or len(events) != 3 or not events[0].startswith('START SEQUENCE') or not events[1].startswith('TRANSITION ') or not events[2].startswith('DONE SEQUENCE'):
            raise RuntimeError('Full sequence completion not confirmed; no retry')
        s = statuses[-1]
        if s.get('phase') != 'DONE' or s.get('used') != '1' or any(s.get(k) != v for k, v in ZERO.items()):
            raise RuntimeError('Final disabled state not confirmed')
        if not 3000 <= int(s.get('m2ms', '-1')) <= 3050 or not 500 <= int(s.get('m1ms', '-1')) <= 550:
            raise RuntimeError('Timing outside expected tolerance')
        record(f"PASS sequence m2ms={s['m2ms']} m1ms={s['m1ms']} both disabled; GPIO timing does not measure mechanical rotation")
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
