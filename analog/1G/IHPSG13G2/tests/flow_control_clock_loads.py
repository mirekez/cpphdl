#!/usr/bin/env python3
"""Exercise clock branch insertion against OpenROAD and actual IHP cells."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

SOURCE = Path(__file__).resolve()


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    helper = SOURCE.parents[1] / 'flow_control_clock_loads.tcl'
    lines = ['module ClockSplitFixture(input clock, data, reset, output [19:0] leaves, output q, inverted, signal_out);',
             'wire mixed_clock, buffer_clock, reset_branch, data_branch, long_signal;']
    for group, (net, source) in enumerate([('mixed_clock', 'clock'), ('buffer_clock', 'clock'),
                                         ('reset_branch', 'reset'), ('data_branch', 'data')]):
        lines.append(f'sg13g2_buf_8 parent{group}(.A({source}), .X({net}));')
        for index in range(5):
            leaf = 5 * group + index
            lines.append(f'sg13g2_buf_8 leaf{leaf}(.A({net}), .X(leaves[{leaf}]));')
    lines += ['sg13g2_inv_1 clock_load(.A(buffer_clock), .Y(inverted));',
              'sg13g2_buf_4 signal_source(.A(data), .X(long_signal));',
              'sg13g2_buf_8 signal_sink(.A(long_signal), .X(signal_out));',
              'sg13g2_dfrbpq_1 state(.CLK(mixed_clock), .D(data_branch), .RESET_B(reset_branch), .Q(q));',
              'endmodule']
    fixture = work / 'fixture.v'
    fixture.write_text('\n'.join(lines) + '\n')
    environment = dict(os.environ, CLOCK_SPLIT_WORK=str(work), CLOCK_SPLIT_PDK=str(args.pdk.resolve()),
                       CLOCK_SPLIT_HELPER=str(helper),
                       SIGNAL_SPLIT_HELPER=str(helper.with_name('flow_control_signal_loads.tcl')))
    files = [SOURCE, SOURCE.with_suffix('.tcl'), helper, helper.with_name('flow_control_signal_loads.tcl'),
             fixture, args.openroad.resolve()]
    library = args.pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    files += [library / name for name in ('lef/sg13g2_tech.lef', 'lef/sg13g2_stdcell.lef',
                                         'lib/sg13g2_stdcell_slow_1p08V_125C.lib')]
    hashes = {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    report = dict(status='running', work=str(work), source_sha256=hashes,
                  scope='physical connectivity regression; does not qualify routed timing')
    try:
        with (work / 'openroad.log').open('w') as log:
            subprocess.run(['prlimit', '--as=536870912', '--', str(args.openroad.resolve()),
                            '-no_init', '-exit', str(SOURCE.with_suffix('.tcl'))], env=environment,
                           stdout=log, stderr=-2, timeout=60, check=True)
        output = (work / 'openroad.log').read_text()
        result = 'PASS extracted-load repair: twelve clock loads, preserved inversion/reset/data, split signal wire'
        if result not in output or '[ERROR' in output:
            raise ValueError('clock branch connectivity regression failed')
        for path, expected in hashes.items():
            if hashlib.sha256(Path(path).read_bytes()).hexdigest() != expected:
                raise ValueError('test input changed during execution')
        report.update(status='pass', result=result)
        print(result)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'openroad', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
