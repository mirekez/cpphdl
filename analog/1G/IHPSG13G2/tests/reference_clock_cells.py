#!/usr/bin/env python3
"""Check the PDK PFD candidate against the unchanged C++/Verilog-A PFD contract."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'tests'),
               str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from reference_clock_cells import definitions, detector, library_path
from phase_frequency_detector import contract, read
from tool_process import configure_cleanup, execute


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def waveform(period, delay, missing=False):
    points = [(0, 0)]
    time = 16e-9 + delay
    while not missing and time + period / 2 + .1e-9 < 384e-9:
        points += [(time, 0), (time + .1e-9, 1.2),
                   (time + period / 2, 1.2), (time + period / 2 + .1e-9, 0)]
        time += period
    return 'pwl(' + ' '.join(f'{t:.17g} {v}' for t, v in points) + ')'


def bench(cells, models, corner, temperature, profile, load):
    period = 24e-9 if profile == 'slow' else 4e-9 if profile == 'fast' else 8e-9
    phase = 1e-9 if profile == 'ref_leads' else -1e-9 if profile in ('fb_leads', 'fb_wrapped') else 0
    release = 151 if profile == 'fb_wrapped' else 157
    return ('* PDK PFD realization; shared independent electrical contract\n'
        f'.lib "{models / "models/cornerMOSlv.lib"}" {corner}\n' + cells + detector() +
        'Xdetector VDD 0 REF FEEDBACK CLEAR UP UP_N DOWN DOWN_N RESET_B IHPPhaseDetector\n'
        'VDD_SOURCE VSUP 0 1.2\nRSUP VSUP VDD 10\nCSUP VDD 0 2p\n'
        'RREF REF_SRC REF 50\nRFB FB_SRC FEEDBACK 50\nRCLEAR CLEAR_SRC CLEAR 50\n' +
        (f'CUP UP 0 {load:.17g}\nCDOWN DOWN 0 {load:.17g}\n' if load else '') +
        f'VREF REF_SRC 0 {waveform(8e-9, 0)}\n'
        f'VFB FB_SRC 0 {waveform(period, phase, profile == "missing")}\n'
        f'VCLEAR CLEAR_SRC 0 pwl(0 1.2 12n 1.2 12.1n 0 137n 0 137.1n 1.2 {release}n 1.2 {release + .1}n 0)\n'
        f'.temp {temperature}\n.options klu\n'
        '.save v(UP) v(DOWN) v(UP_N) v(DOWN_N) v(REF) v(FEEDBACK) v(RESET_B) v(VDD)\n'
        '.control\nset num_threads=1\nset wr_singlescale\nset wr_vecnames\nset numdgt=17\n'
        'tran 10p 384n 0 10p uic\nlet RESET = v(VDD)-v(RESET_B)\n'
        'wrdata spice.dat v(UP) v(DOWN) v(UP_N) v(DOWN_N) v(REF) v(FEEDBACK) RESET v(VDD)\n'
        'quit\n.endc\n.end\n')


def run(args):
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    cells = library_path(args.pdk)
    paths = [SOURCE, SOURCE.parents[1] / 'reference_clock_cells.py', SOURCE.parents[1] / 'clock_cell_library.py', cells, args.ngspice.resolve(),
             ANALOG / 'tests/phase_frequency_detector.py', ANALOG / 'VerilogA/compare.py',
             ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', models / '.spiceinit']
    paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = {str(p): digest(p) for p in paths}
    temperature = dict(mos_tt=27, mos_ss=125, mos_ff=-40)[args.corner]
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', corner=args.corner, temperature_C=temperature, work=str(work),
        source_sha256=hashes, cases={},
        scope='PDK schematic phase detector under shared generic PFD bounds; excludes divider, extracted wiring, PLL, PVT voltage variation and chip qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    try:
        save()
        cell_text = definitions(cells.read_text())
        for load in (0, 20e-15):
            for profile in ('same', 'ref_leads', 'fb_leads', 'fb_wrapped', 'slow', 'fast', 'missing'):
                name = profile + ('_loaded' if load else '_unloaded')
                case = work / name
                case.mkdir()
                (case / 'bench.cir').write_text(bench(cell_text, models, args.corner, temperature, profile, load))
                with (case / 'ngspice.log').open('w') as log:
                    execute(['prlimit', '--as=1073741824', '--', str(args.ngspice.resolve()), '-b', 'bench.cir'],
                        cwd=case, env=dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                            SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1'),
                        stdout=log, stderr=-2, check=True, timeout=600, memory_exclusive=False)
                result = contract(read(case / 'spice.dat', 'veriloga'), profile)
                result['load_F'] = load
                raw = case / 'spice.dat'
                original = digest(raw)
                archive = case / 'spice.dat.gz'
                with raw.open('rb') as source, gzip.open(archive, 'wb', compresslevel=1) as output:
                    shutil.copyfileobj(source, output)
                check = hashlib.sha256()
                with gzip.open(archive, 'rb') as source:
                    for block in iter(lambda: source.read(1024 * 1024), b''):
                        check.update(block)
                if check.hexdigest() != original:
                    raise ValueError('PFD trace archive changed samples')
                raw.unlink()
                result['artifact_sha256'] = {str(p): digest(p) for p in (archive, case / 'bench.cir')}
                result['uncompressed_sha256'] = original
                report['cases'][name] = result
                save()
                print(name + ': pass', flush=True)
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('PFD process inputs changed')
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
