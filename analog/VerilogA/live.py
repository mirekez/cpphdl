"""Synchronous ngspice control for externally clocked C++RTL co-simulation."""
import os
from pathlib import Path
import selectors
import re
import math
import signal
import subprocess
import time


class NgspiceSession:
    def __init__(self, binary, work, log, environment=None):
        self.process = subprocess.Popen([str(Path(binary).resolve()), '-n', '-i', '-p'],
            cwd=work, env=environment, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, start_new_session=True, bufsize=0)
        self.log = Path(log).open('wb')
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.pending = b''
        self.sequence = 0

    def command(self, command, timeout=120):
        token = f'CPPHDL_LIVE_{self.process.pid}_{self.sequence}_DONE'.encode()
        self.sequence += 1
        self.process.stdin.write(command.encode() + b'\necho ' + token + b'\n')
        self.process.stdin.flush()
        deadline = time.monotonic() + timeout
        while token not in self.pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('ngspice did not complete the control command: ' + command[:100])
            if not self.selector.select(min(remaining, 1)):
                continue
            data = os.read(self.process.stdout.fileno(), 65536)
            if not data:
                raise RuntimeError('ngspice closed before completing: ' + command[:100])
            self.log.write(data)
            self.log.flush()
            self.pending += data
        response, self.pending = self.pending.split(token, 1)
        result = response.decode(errors='replace')
        if re.search(r'(?im)(?:^|->\s*)(?:error\b|fatal\b|syntax error\b)|no such command', result):
            raise RuntimeError('ngspice rejected the control command: ' + result[-2000:])
        return result

    def close(self):
        try:
            if self.process.poll() is None:
                self.process.stdin.write(b'quit\n')
                self.process.stdin.flush()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(self.process.pid, signal.SIGKILL)
                    self.process.wait()
        finally:
            self.selector.close()
            self.process.stdin.close()
            self.process.stdout.close()
            self.log.close()

    def last(self, vector):
        """Read the most recent accepted point of a paused transient vector."""
        if not re.fullmatch(r'time|v\([A-Za-z0-9_.]+(?:,[A-Za-z0-9_.]+)?\)', vector):
            raise ValueError('unsupported live observation vector')
        response = self.command(f'print {vector}[length({vector})-1]')
        values = re.findall(r'=\s*([-+0-9.eE]+)', response)
        if len(values) != 1 or not math.isfinite(float(values[0])):
            raise ValueError('cannot read a finite paused ngspice value: ' + response)
        return float(values[0])

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class NgspiceVoltageClock:
    """Own breakpoint scheduling for one achieved electrical clock.

    Construct while the transient is paused at a known rail. Every advance
    replaces this session's breakpoints with the opposite threshold and an end
    guard. The stop occurs at an accepted simulator point, not an interpolated
    exact crossing; maximum integration step and refinement bound that error.
    """
    def __init__(self, session, vector, low, high):
        if not all(math.isfinite(value) for value in (low, high)) or low >= high:
            raise ValueError('voltage clock thresholds need a finite interval')
        self.session, self.vector, self.low, self.high = session, vector, low, high
        self.time = session.last('time')
        value = session.last(vector)
        if low < value < high:
            raise ValueError('electrical clock startup is ambiguous')
        self.high_state = value >= high
        self.ended = False

    def advance(self, end, timeout=120):
        if self.ended or not math.isfinite(end) or end <= self.time:
            raise ValueError('voltage clock needs a later finite end time')
        relation, threshold = ('le', self.low) if self.high_state else ('ge', self.high)
        self.session.command(f'delete all\nstop when time = {end:.17g}\n'
                             f'stop when {self.vector} {relation} {threshold:.17g}')
        self.session.command('resume', timeout)
        time, value = self.session.last('time'), self.session.last(self.vector)
        tolerance = max(abs(end) * 1e-10, 1e-18)
        if not self.time < time <= end + tolerance:
            raise ValueError('voltage clock failed to advance within its end guard')
        reached = value <= self.low if self.high_state else value >= self.high
        self.time = time
        self.ended = abs(time - end) <= tolerance
        if not reached and not self.ended:
            raise ValueError('transient stopped without a clock threshold or end guard')
        edge = None
        if reached:
            self.high_state = not self.high_state
            edge = 'rising' if self.high_state else 'falling'
        return dict(time_s=time, voltage_V=value, edge=edge, ended=self.ended)
