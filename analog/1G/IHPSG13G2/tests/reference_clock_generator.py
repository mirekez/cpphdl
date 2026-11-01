#!/usr/bin/env python3
"""Check the process-sized PLL in C++, exported Verilog-A or IHP PSP."""
import argparse
import gzip
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parent.parent), str(ANALOG / '1G/tests')]
from reference_clock_process import process_deck
from reference_clock_contract import measure
from reference_clock_domain import read, digest
from hardware import configure_cleanup, execute, source_dependencies


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    for path in (SOURCE, SOURCE.with_name('reference_clock_contract.py'),
                 SOURCE.parent.parent / 'reference_clock_process.py',
                 SOURCE.parent.parent / 'core_driver_process.py', SOURCE.parent.parent / 'mos_geometry.py',
                 ANALOG / '1G/tests/reference_clock_domain.py', ANALOG / '1G/tests/hardware.py',
                 ANALOG / 'VerilogA/compare.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
                 args.bench.resolve()):
        hashes[str(path)] = digest(path)
    report = dict(status='running', flow=args.flow, work=str(work), corner=args.corner,
                  temperature_C=args.temperature, source_sha256=hashes,
                  scope='connected clock schematic with declared R/C; physical clock-tree load and extracted layout are not included')

    def save():
        data = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(data)
        (work / 'checks.json').write_text(data)

    def command(label, argv, env=None, timeout=3600):
        argv = list(map(str, argv))
        if args.memory_mib:
            argv = ['prlimit', '--as=' + str(args.memory_mib * 1024 * 1024), '--', *argv]
        with (work / (label + '.log')).open('w') as log:
            execute(argv, cwd=work, env=os.environ if env is None else env, stdout=log, stderr=-2,
                    check=True, timeout=timeout, memory_exclusive=False if args.memory_mib else None)

    save()
    try:
        command('bench', [args.bench.resolve(), work, 'run' if args.flow == 'cpp' else 'export',
                          args.ppm, args.load, int(args.refined)])
        if args.flow != 'cpp':
            hashes[str(args.ngspice.resolve())] = digest(args.ngspice.resolve())
            env = os.environ.copy()
            if args.flow == 'veriloga':
                hashes[str(args.openvaf.resolve())] = digest(args.openvaf.resolve())
                command('openvaf', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])
                deck = 'bench.cir'
            else:
                pdk = args.pdk.resolve()
                models = pdk / 'ihp-sg13g2/libs.tech/ngspice/models'
                libraries = list(models.rglob('*.lib')) + list(models.rglob('*.spice'))
                hashes.update({str(path): digest(path) for path in libraries})
                deck, binding = process_deck((work / 'design.va').read_text(), (work / 'bench.cir').read_text(),
                                             models, args.corner, args.temperature)
                (work / 'physical.cir').write_text(deck)
                (work / 'bindings.json').write_text(json.dumps(binding, indent=2) + '\n')
                report['binding'] = binding
                deck = 'physical.cir'
                env.update(PDK_ROOT=str(pdk), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(pdk / 'ihp-sg13g2/libs.tech/ngspice'))
            # Avoid oversubscribing the small circuit's PSP evaluations.
            env['OMP_NUM_THREADS'] = '1'
            save()
            command('ngspice', [args.ngspice.resolve(), '-b', deck], env, timeout=7200)
        trace = work / ('cpp/trace.csv' if args.flow == 'cpp' else 'spice.dat')
        rows = read(trace, 'cpp' if args.flow == 'cpp' else 'veriloga')
        report['electrical'] = measure(rows, args.ppm, args.refined)
        if args.flow == 'cpp':
            high, count = False, 0
            for row in rows:
                if not high and row[2] >= 2.31:
                    high, count = True, count + 1
                elif high and row[2] <= .99:
                    high = False
            actual = json.loads((work / 'clock_controller.json').read_text())
            if actual != {'count': count}:
                raise ValueError('C++ controller did not follow the delivered voltage clock')
            report['actual_controller_edges'] = count
        if any(not Path(path).is_file() or digest(Path(path)) != value for path, value in hashes.items()):
            raise ValueError('clock qualification inputs changed during simulation')
        report['trace_sha256'] = digest(trace)
        with trace.open('rb') as source, gzip.open(str(trace) + '.gz', 'wb') as destination:
            shutil.copyfileobj(source, destination)
        trace.unlink()
        report['status'] = report['electrical']['status']
        if report['status'] != 'pass':
            raise ValueError('clock electrical failures: ' + ', '.join(
                key for key, value in report['electrical']['checks'].items() if not value))
        print('PASS connected clock: ' + args.flow)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bench', 'work', 'ngspice', 'openvaf', 'pdk'):
        parser.add_argument('--' + name, type=Path, required=name in ('bench', 'work'))
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'psp'), required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--corner', default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--ppm', type=float, default=0)
    parser.add_argument('--load', type=int, choices=(0, 1), default=0)
    parser.add_argument('--refined', action='store_true')
    parser.add_argument('--memory-mib', type=int, choices=range(512, 4097), default=0,
                        help='bound each subprocess and permit concurrent bounded experiments')
    run(parser.parse_args())
