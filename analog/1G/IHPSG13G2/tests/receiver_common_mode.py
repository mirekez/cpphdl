#!/usr/bin/env python3
"""Study receiver DC bias before connecting a differently biased MDI network.

Hold-node initial voltages constrain the stored-charge state during the DC
study. Other initial guesses are nodesets. This does not qualify conversion,
startup or the complete conditioned receiver.
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parent))
from receiver_charge_sizing import digest
from hardware import configure_cleanup, execute


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='bias-', dir=root))
    receipt = json.loads(args.receipt.read_text())
    for path, value in receipt['source_sha256'].items():
        if digest(path) != value:
            raise ValueError('stale receiver reference')
    hashes = receipt['source_sha256'] | {str(SOURCE): digest(SOURCE), str(args.receipt.resolve()): digest(args.receipt)}
    report = dict(status='running', source_sha256=hashes, work=str(work), cases={},
                  scope='DC input common-mode study at zero differential input and prescribed hold voltages; no composed MDI, conversion or startup qualification')
    try:
        for code in (4, 8):
            origin = Path(receipt['run_directory']) / str(code)
            original = (origin / 'physical.cir').read_text()
            expected = receipt['cases'][str(code)]['artifact_sha256'][str(origin / 'physical.cir')]
            if digest(origin / 'physical.cir') != expected:
                raise ValueError('changed receiver circuit')
            observations = dict((int(i), node) for i, node in re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
            held = {observations[7 + 2 * i] for i in range(16)}
            head = original.split('.control\n')[0]
            lines = []
            inputs = [line.split()[0] for line in head.splitlines() if line.startswith('Vsource') and ' PWL(' in line][:2]
            if len(inputs) != 2:
                raise ValueError('missing differential source pair')
            for line in head.splitlines():
                if line.startswith('.ic '):
                    match = re.fullmatch(r'\.ic V\((n\d+)\)=(\S+)', line)
                    if match is None:
                        raise ValueError('changed initial-condition syntax')
                    if match[1] not in held:
                        line = '.nodeset' + line[3:]
                if line.startswith('C'):
                    line = re.sub(r' IC=\S+$', '', line)
                if line.split() and line.split()[0] in inputs:
                    line = ' '.join(line.split()[:3]) + ' DC 1.65'
                lines.append(line)
            lines += ['.control', 'set num_threads=1', 'set wr_singlescale', 'set wr_vecnames', 'set numdgt=17']
            for common in (1.65, 1.91, 2.13):
                lines += [f'alter {name} dc = {common}' for name in inputs]
                lines += ['op', f'wrdata cm{common}.dat ' + ' '.join(f'v({observations[i]})' for i in range(7))]
            lines += ['quit', '.endc', '.end']
            case = work / str(code)
            case.mkdir()
            (case / 'physical.cir').write_text('\n'.join(lines) + '\n')
            models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
            environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                               SPICE_USERINIT_DIR=str(models), OMP_THREAD_LIMIT='1')
            with (case / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'physical.cir'], cwd=case, env=environment,
                        stdout=log, stderr=-2, check=True, timeout=300)
            log = (case / 'ngspice.log').read_text()
            if 'ngspice-47 done' not in log or re.search(r'\b(error|fatal|failed|aborted|unknown)\b', log, re.I):
                raise ValueError('receiver bias operating point failed')
            result = {}
            for common in (1.65, 1.91, 2.13):
                text = (case / f'cm{common}.dat').read_text().splitlines()
                values = list(map(float, text[1].split()))
                if len(text) != 2 or len(values) != 8:
                    raise ValueError('incomplete bias observations')
                result[str(common)] = dict(zip(('PLUS', 'MINUS', 'GAIN_INPUT', 'GAIN_OUTPUT', 'VDD', 'VLOGIC', 'GAIN_BIAS'), values[1:]))
            report['cases'][str(code)] = result
            print(code, result, flush=True)
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('receiver bias inputs changed')
        report['status'] = 'diagnostic_complete'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (root / 'checks.json').write_text(payload)


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('work', 'receipt', 'pdk', 'ngspice'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
