"""Bound an EDA child's resident memory without limiting sparse address maps."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import time


def run(args):
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        raise ValueError('missing EDA command')
    started = time.monotonic()
    process = subprocess.Popen(command)
    peak = 0
    reason = None
    try:
        while process.poll() is None:
            try:
                pages = int(Path(f'/proc/{process.pid}/statm').read_text().split()[1])
                resident = pages * os.sysconf('SC_PAGE_SIZE')
                peak = max(peak, resident)
                if resident > args.memory_mib * 1024 ** 2:
                    reason = 'resident_memory_limit'
            except FileNotFoundError:
                pass
            if time.monotonic() - started > args.seconds:
                reason = 'time_limit'
            if reason:
                process.kill()
                break
            time.sleep(.25)
        code = process.wait()
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    report = dict(returncode=code, stopped_for=reason, peak_resident_mib=peak / 1024 ** 2,
                  seconds=time.monotonic() - started)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report), flush=True)
    return code if code >= 0 else 128 - code

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--memory-mib', type=int, default=4200)
    parser.add_argument('--seconds', type=int, default=1800)
    parser.add_argument('--report', type=Path)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    raise SystemExit(run(parser.parse_args()))
