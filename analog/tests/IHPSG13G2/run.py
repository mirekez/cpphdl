"""Bounded native-first checks of IHP PSP equations, corners and terminal charge."""
import argparse
import csv
import importlib.util
import json
import math
from pathlib import Path
import resource
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'basic/IHPSG13G2'))
import parameters
spec = importlib.util.spec_from_file_location('workload_common', ROOT / 'tests/workloads/run.py')
common = importlib.util.module_from_spec(spec)
spec.loader.exec_module(common)
DC_VOLTAGE_TOLERANCE = 2e-7
DC_CURRENT_TOLERANCE = 2e-10


def trace(path, mode, reference=False):
    if path.stat().st_size > 8*1024*1024: raise ValueError('oversized trace')
    with path.open() as stream:
        reader = (line.split() for line in stream) if reference else csv.reader(stream)
        header = next(reader)
        expected = (['v-sweep', 'v(drain)', 'current'] if mode == 'dc' else ['time', 'v(drain)', 'v(gate)']) if reference else (
            ['gate_V', 'drain_V', 'current_A'] if mode == 'dc' else ['time_s', 'drain_V', 'gate_V'])
        if header != expected: raise ValueError('incorrect observation names')
        rows = []
        for row in reader:
            if not row: continue
            values = list(map(float, row))
            if len(values) != 3 or not all(math.isfinite(x) for x in values): raise ValueError('invalid trace values')
            if rows and values[0] <= rows[-1][0]: raise ValueError('non-increasing trace scale')
            rows.append(values)
            if len(rows) > 6000: raise ValueError('point budget exceeded')
    if not rows: raise ValueError('empty trace')
    return rows


def check(rows, mode, polarity):
    # Same independent circuit-purpose contract is applied to both engines.
    if mode == 'dc':
        if len(rows) != 5 or any(abs(row[0] - i*.3) > 1e-12 for i, row in enumerate(rows)):
            raise ValueError('incomplete DC sweep')
        for gate, drain, current in rows:
            expected = (drain if polarity == 'p' else 1.2 - drain) / 1000
            if not 0 <= drain <= 1.20000001 or current < -1e-12 or abs(current - expected) > 1e-12:
                raise ValueError('load KCL or supply envelope violated')
        if any(b[1] >= a[1] for a, b in zip(rows, rows[1:])): raise ValueError('MOS transfer is not inverting')
        cutoff, enabled = (rows[-1], rows[0]) if polarity == 'p' else (rows[0], rows[-1])
        if cutoff[2] > 2e-6 or enabled[2] < 1e-6 or enabled[2] <= cutoff[2]*10:
            raise ValueError('cutoff or current delivery failed')
    else:
        if len(rows) < 2000 or abs(rows[0][0]) > 1e-18 or abs(rows[-1][0]-2e-9) > 1e-20:
            raise ValueError('incomplete charge excursion')
        if max(b[0]-a[0] for a, b in zip(rows, rows[1:])) > 1.01e-12:
            raise ValueError('insufficient transient resolution')
        def gate(t):
            if t < .2e-9: return 0
            if t < .4e-9: return 1.2*(t-.2e-9)/.2e-9
            if t < 1e-9: return 1.2
            if t < 1.2e-9: return 1.2*(1.2e-9-t)/.2e-9
            return 0
        if any(abs(row[2]-gate(row[0])) > 1e-9 or not -.02 <= row[1] <= 1.22 for row in rows):
            raise ValueError('stimulus or drain envelope failed')
        high = min(rows, key=lambda row: abs(row[0]-.8e-9))[1]
        if rows[0][1] - high < .001 or abs(rows[-1][1]-rows[0][1]) > 1e-6:
            raise ValueError('switching or charge recovery failed')
    return dict(status='pass', points=len(rows))


def compare(a, b, mode):
    if mode == 'dc':
        errors = [max(abs(x[col]-y[col]) for x, y in zip(a, b)) for col in (1, 2)]
        if errors[0] > DC_VOLTAGE_TOLERANCE or errors[1] > DC_CURRENT_TOLERANCE:
            raise ValueError('DC voltage/current comparison failed: ' + repr(errors))
        return dict(status='pass', maximum_voltage_error_V=errors[0], maximum_current_error_A=errors[1],
                    voltage_tolerance_V=DC_VOLTAGE_TOLERANCE, current_tolerance_A=DC_CURRENT_TOLERANCE)
    return common.compare(a, b)


def deck(bundle, mode, temperature):
    p, family = bundle['polarity'], bundle['voltage']
    source, load = ('supply', '0') if p == 'p' else ('0', 'supply')
    stimulus = '0' if mode == 'dc' else 'PWL(0 0 .2n 0 .4n 1.2 1n 1.2 1.2n 0 2n 0)'
    analysis = ('dc VG 0 1.2 .3\nlet current = ' + ('v(drain)' if p == 'p' else '(1.2-v(drain))') +
                '/1000\nwrdata reference.dat v(drain) current') if mode == 'dc' else (
                'tran .5p 2n 0 .5p\nwrdata reference.dat v(drain) v(gate)')
    return f'''IHP native process comparison; reference uses original PDK wrapper
.lib cornerMOS{family}.lib {bundle['corner']}
VDD supply 0 1.2
VG gate 0 {stimulus}
RLOAD {load} drain 1000
XD drain gate {source} {source} sg13_{family}_{p}mos w={bundle['width']:.17g} l={bundle['length']:.17g}
.temp {temperature}
.options reltol=1e-7 abstol=1e-13 vntol=1e-10 method=gear maxord=2 trtol=.1
.control
set wr_singlescale
set wr_vecnames
set numdgt=17
{analysis}
quit
.endc
.end
'''


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='process-', dir=args.work.resolve()))
    report = dict(status='running', primary='cpphdl_osdi', reference='ngspice' if args.ngspice else None,
                  cases=[], work=str(work), all_IHP_workloads_qualified=False, phase_timeout_s=20,
                  scope='Single-finger schematic LV/HV PSP load sweeps and intrinsic-charge excursions; no full 1G claim')
    deadline = time.monotonic() + 180
    total_start = time.monotonic()
    def execute(command, directory, label):
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        wall = common.execute(command, directory, label, deadline)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        return dict(process_wall_s=wall, process_cpu_s=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime)
    try:
        report['source_sha256'] = {str(p): common.digest(p) for p in (
            Path(__file__), args.bench, ROOT/'include/osdi.hpp', ROOT/'basic/IHPSG13G2/parameters.py',
            ROOT/'include/behavior.hpp', ROOT/'include/behavior_devices.hpp', ROOT/'basic/IHPSG13G2/mos.hpp',
            ROOT/'tests/workloads/run.py',
            Path(__file__).with_name('process_bench.cpp'))}
        if args.ngspice: report['source_sha256'][str(args.ngspice)] = common.digest(args.ngspice)
        for family in ('lv', 'hv'):
            for polarity in ('n', 'p'):
                for corner, temperature in (('mos_tt', 27), ('mos_ss', 125), ('mos_ff', -40)):
                    preparation_start = time.monotonic()
                    bundle = parameters.psp(args.pdk, corner, polarity, length=.13e-6 if family == 'lv' else .45e-6, voltage=family)
                    preparation_wall = time.monotonic() - preparation_start
                    for mode in ('dc', 'charge') if corner == 'mos_tt' else ('dc',):
                        name = f'{family}_{polarity}_{corner}_{temperature}_{mode}'
                        directory = work / name
                        directory.mkdir()
                        parameters.write(bundle, directory/'model.params')
                        (directory/'model.json').write_text(json.dumps(bundle, indent=2)+'\n')
                        item = dict(name=name, model_sha256=bundle['input_sha256'], status='running',
                                    parameter_preparation_wall_s=preparation_wall if mode == 'dc' else 0)
                        report['cases'].append(item)
                        command = [args.bench, directory/'model.params', bundle['binary'], directory/'native', str(temperature), polarity, mode, '1e-12']
                        item['native'] = execute(command, directory, 'native')
                        native = trace(directory/'native/cpp.csv', mode)
                        item['native']['check'] = check(native, mode, polarity)
                        item['native']['solver'] = json.loads((directory/'native/native.json').read_text())
                        if mode != 'dc':
                            command[3], command[-1] = directory/'refined', '5e-13'
                            item['native_refined'] = execute(command, directory, 'native_refined')
                            fine = trace(directory/'refined/cpp.csv', mode)
                            item['native_refined']['check'] = check(fine, mode, polarity)
                            item['refinement'] = compare(native, fine, mode)
                            native = fine
                        # Reference cannot run until native acceptance/refinement passed.
                        if args.ngspice:
                            models = args.pdk.resolve()/'ihp-sg13g2/libs.tech/ngspice/models'
                            (directory/'.spiceinit').write_text(f'osdi {bundle["binary"]}\nosdi {models.parent}/osdi/psp103_nqs.osdi\nsetcs sourcepath = ( {models} )\n')
                            (directory/'bench.cir').write_text(deck(bundle, mode, temperature))
                            item['reference'] = execute(['env', f'SPICE_USERINIT_DIR={directory}', args.ngspice, '-b', 'bench.cir'], directory, 'ngspice')
                            reference = trace(directory/'reference.dat', mode, True)
                            item['reference']['check'] = check(reference, mode, polarity)
                            item['comparison'] = compare(native, reference, mode)
                        if any(common.digest(p) != h for p, h in bundle['input_sha256'].items()):
                            raise ValueError('PDK inputs changed during run')
                        item['status'] = 'pass'
        if any(common.digest(p) != h for p, h in report['source_sha256'].items()):
            raise ValueError('source inputs changed during run')
        report['status'] = 'pass'
    except BaseException as error:
        if report['cases'] and report['cases'][-1]['status'] == 'running':
            report['cases'][-1]['status'] = 'fail'
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['total_wall_s'] = time.monotonic()-total_start
        payload = json.dumps(report, indent=2)+'\n'
        (work/'checks.json').write_text(payload)
        (args.work/'checks.json').write_text(payload)
        table = ['Times include process launch/model setup/solve/output/wait; CPU is child user+system time.',
                 'C++ PDK-card preprocessing is separate (see parameter_preparation_wall_s in checks.json); ngspice parses its PDK wrapper inside its process.',
                 'C++ column uses the refined run for transients; coarse refinement cost is additional.',
                 'case                              C++ wall(s) C++ CPU(s) ngspice wall(s) ngspice CPU(s) status']
        for item in report['cases']:
            native = item.get('native_refined', item.get('native', {}))
            ref = item.get('reference', {})
            def duration(record, key):
                return f'{record[key]:.6f}' if key in record else 'n/a'
            table.append(f'{item["name"]:33} {duration(native, "process_wall_s"):>11} {duration(native, "process_cpu_s"):>10}'
                         f' {duration(ref, "process_wall_s"):>15} {duration(ref, "process_cpu_s"):>14} {item["status"]}')
        (args.work/'timing.txt').write_text('\n'.join(table)+'\n')
    print(f'PASS {len(report["cases"])} bounded IHP process cases; {report["total_wall_s"]:.3f} s total')
    print(args.work/'timing.txt')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bench', 'pdk', 'work'): parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--ngspice', type=Path)
    args = parser.parse_args()
    args.bench = args.bench.resolve()
    if args.ngspice: args.ngspice = args.ngspice.resolve()
    run(args)
