"""Produce actual controller words and check them without an analog dependency."""
import argparse
import json
from pathlib import Path

from hardware import configure_cleanup, execute, source_dependencies
from transmit_driver import controls, digest

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    report_path = root / 'checks.json'
    report_path.unlink(missing_ok=True)
    trace = root / 'controls.csv'
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx)
    hashes.update({str(path): digest(path) for path in (SOURCE, SOURCE.with_name('transmit_driver.py'))})
    binary = {str(args.bench.resolve()): digest(args.bench.resolve())}
    result = dict(status='running', flow='control_cpp', module='PAM5DriverControl',
                  source_sha256=hashes, binary_sha256=binary, controls=str(trace),
                  scope='native controller history, unary gates and faults against an independent oracle; no analog qualification')
    try:
        execute([str(args.bench.resolve()), str(trace)], check=True, timeout=30)
        words = controls(trace)
        levels = {3 * (2 - word['code']) + 2 - word['previous'] for word in words if word['valid']}
        if (levels != set(range(-8, 9)) or not any(word['fault'] for word in words)
                or not any(word['reset'] for word in words) or not any(not word['enable'] for word in words)):
            raise ValueError('controller fixture misses weighted levels, faults, reset or disable')
        if any(digest(Path(path)) != expected for path, expected in (hashes | binary).items()):
            raise ValueError('controller fixture sources or binary changed')
        result.update(status='pass', clocks=len(words), weighted_levels=sorted(levels), controls_sha256=digest(trace))
        print('PASS controller-only electrical stimulus:', len(words), 'clocks, every weighted level, reset/disable/fault')
    except BaseException as error:
        result.update(status='fail', error=str(error))
        raise
    finally:
        report_path.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
