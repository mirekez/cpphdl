"""Exercise compact-model derivative mapping, rollback and fail-closed setup."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import run


def main(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='abi-', dir=args.work.resolve()))
    for family in ('lv', 'hv'):
        for polarity in ('n', 'p'):
            bundle = run.parameters.psp(args.pdk, polarity=polarity, voltage=family,
                                        length=.13e-6 if family == 'lv' else .45e-6)
            path = work / f'{family}-{polarity}.params'
            run.parameters.write(bundle, path)
            command = [str(args.bench.resolve()), str(path), bundle['binary'], str(work/f'{family}-{polarity}'), '27', polarity, 'contract', '1e-12']
            result = subprocess.run(['prlimit', '--as=536870912', '--cpu=10', '--', *command],
                                    capture_output=True, text=True, timeout=15)
            if result.returncode: raise ValueError(result.stdout + result.stderr)
    original = path.read_text()
    for mutation, diagnostic in ((original+'instance unknown_parameter 1\n', 'E_OSDI_PARAMETER'),
                                 (original+'instance nf 1.5\n', 'E_OSDI_PARAMETER'),
                                 (original+'model unfinished', 'truncated process bundle'),
                                 (original.replace('module psp103va', 'module missing'), 'E_OSDI_MODEL')):
        if 'nf 1.5' in mutation: mutation = mutation.replace('instance nf 1\n', '')
        path.write_text(mutation)
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        if result.returncode == 0 or diagnostic not in result.stderr:
            raise ValueError('missing rejection: ' + diagnostic + ': ' + result.stderr)
    print('PASS LV/HV N/P current/charge Jacobians, rollback, invalid parameters and missing model')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bench', 'pdk', 'work'): parser.add_argument('--'+name, type=Path, required=True)
    main(parser.parse_args())
