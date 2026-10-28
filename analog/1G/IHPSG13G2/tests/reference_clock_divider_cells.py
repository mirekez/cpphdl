#!/usr/bin/env python3
"""Apply the original storage edge oracle to the actual PDK divider circuit."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'tests'), str(ANALOG / 'VerilogA'),
               str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from clock_cell_library import library_path
from reference_clock_divider_cells import library, divider
from cmos_storage import contract, read
from trace_storage import archive_trace
from tool_process import configure_cleanup, execute


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def bench(cells, models, corner, temperature, loaded):
    clock, data, previous = [(0, 0)], [(0, 0)], 0
    for half in range(1, 192):
        time = half * 2e-9
        clock.append((time, previous))
        previous = 1.2 if half & 1 else 0
        clock.append((time + .1e-9, previous))
    previous = 0
    for index in range(192):
        time = index * 2e-9 + .8e-9
        data.append((time, previous))
        previous = 1.2 if (index * 73 + index // 3) % 17 < 8 else 0
        data.append((time + .1e-9, previous))

    def pwl(points):
        return 'pwl(' + ' '.join(f'{time:.17g} {value}' for time, value in points) + ')'

    return ('* Compact IHP divider under original independent storage oracle\n'
        f'.lib "{models / "models/cornerMOSlv.lib"}" {corner}\n' + cells + divider() +
        'Xdivider VDD 0 CLOCK CLEAR Q Q_N IHPClockDivider\n'
        'VDD_SOURCE VSUP 0 1.2\nRSUP VSUP VDD 10\nCSUP VDD 0 2p\n'
        'RCLK CLK_SOURCE CLOCK 50\nRCLEAR CLEAR_SOURCE CLEAR 50\n'
        'VCLEAR CLEAR_SOURCE 0 pwl(0 1.2 12n 1.2 12.1n 0 139n 0 139.1n 1.2 145n 1.2 145.1n 0)\n' +
        ('CLOAD Q 0 100f\nRLOAD Q 0 10000\n' if loaded else '') +
        f'VCLOCK CLK_SOURCE 0 {pwl(clock)}\nVDATA D 0 {pwl(data)}\n.temp {temperature}\n'
        '.options klu\n.save v(Q) v(Q_N) v(D) v(CLK_SOURCE) v(CLEAR_SOURCE) v(VDD)\n'
        '.control\nset wr_singlescale\nset wr_vecnames\nset numdgt=17\n'
        'tran 10p 384n 0 10p uic\n'
        'wrdata spice.dat v(Q) v(Q_N) v(D) v(CLK_SOURCE) v(CLEAR_SOURCE) v(VDD)\n'
        'quit\n.endc\n.end\n')


def run(args):
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, SOURCE.parents[1] / 'reference_clock_divider_cells.py',
        SOURCE.parents[1] / 'clock_cell_library.py', library_path(args.pdk), args.ngspice.resolve(),
        ANALOG / 'tests/cmos_storage.py', ANALOG / 'VerilogA/compare.py',
        ANALOG / 'VerilogA/trace_storage.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
        models / '.spiceinit']
    paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = {str(p): digest(p) for p in paths}
    temperature = dict(mos_tt=27, mos_ss=125, mos_ff=-40)[args.corner]
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), corner=args.corner, temperature_C=temperature,
        source_sha256=hashes, cases={}, scope='250MHz PDK divider schematic under original storage oracle; no extracted wiring, PLL or full-chip qualification')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    try:
        save()
        cells = library(library_path(args.pdk).read_text())
        for loaded in (False, True):
            name = 'loaded' if loaded else 'unloaded'
            case = work / name
            case.mkdir()
            deck = case / 'bench.cir'
            deck.write_text(bench(cells, models, args.corner, temperature, loaded))
            with (case / 'ngspice.log').open('w') as log:
                execute(['prlimit', '--as=1073741824', '--', str(args.ngspice.resolve()), '-b', 'bench.cir'],
                    cwd=case, env=dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                        SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1'),
                    stdout=log, stderr=-2, check=True, timeout=600, memory_exclusive=False)
            result = contract(read(case, 'veriloga'), 'divider')
            archive, result['trace_storage'] = archive_trace(case / 'spice.dat')
            result['artifact_sha256'] = {str(p): digest(p) for p in (archive, deck)}
            report['cases'][name] = result
            save()
            print(name + ': pass', flush=True)
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('divider process inputs changed')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), required=True)
    run(parser.parse_args())
