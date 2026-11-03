"""Native-first coupled PSP inverter, using the installed sg13g2_inv_1 CDL."""
import argparse
import csv
import json
import math
from pathlib import Path
import resource
import tempfile
import time

from run import ROOT, common, parameters


def cell(path):
    # Fail closed if the physical cell acquires an unsupported device, topology
    # or wrapper option. The reference retains CDL connections and parameters;
    # only the M prefix becomes X for ngspice's PDK subcircuit wrappers.
    lines = parameters.lines(path)
    starts = [i for i, line in enumerate(lines) if line.lower().startswith('.subckt sg13g2_inv_1 ')]
    if len(starts) != 1:
        raise ValueError('missing or ambiguous inverter cell')
    start = starts[0]
    if lines[start].lower().split() != ['.subckt', 'sg13g2_inv_1', 'y', 'a', 'vdd', 'vss']:
        raise ValueError('unsupported inverter ports')
    end = next(i for i in range(start + 1, len(lines)) if lines[i].lower().startswith('.ends'))
    devices = {}
    for line in lines[start + 1:end]:
        fields = line.lower().split(None, 6)
        if len(fields) != 7 or fields[5] not in ('sg13_lv_nmos', 'sg13_lv_pmos'):
            raise ValueError('unsupported inverter device')
        polarity = fields[5][8]
        rail = 'vdd' if polarity == 'p' else 'vss'
        if not fields[0].startswith('m') or fields[1:5] != ['y', 'a', rail, rail] or polarity in devices:
            raise ValueError('unsupported inverter topology')
        values = parameters.assignments(fields[6], {})
        if set(values) != {'m', 'w', 'l', 'ng'} or values['m'] != 1 or values['ng'] != 1:
            raise ValueError('unsupported inverter geometry')
        devices[polarity] = dict(width=values['w'], length=values['l'])
    if set(devices) != {'n', 'p'}:
        raise ValueError('incomplete CMOS inverter')
    translated = [lines[start], *['X' + line[1:] for line in lines[start + 1:end]], lines[end]]
    return devices, '\n'.join(translated) + '\n'


def trace(path, mode, reference=False):
    if path.stat().st_size > 8 * 1024 * 1024:
        raise ValueError('oversized inverter trace')
    expected = (['v-sweep', 'v(output)'] if mode == 'dc' else ['time', 'v(output)', 'v(input)']) if reference else (
        ['input_V', 'output_V'] if mode == 'dc' else ['time_s', 'output_V', 'input_V'])
    with path.open() as stream:
        reader = (line.split() for line in stream) if reference else csv.reader(stream)
        if next(reader, None) != expected:
            raise ValueError('incorrect inverter observations')
        rows = []
        for row in reader:
            if not row:
                continue
            values = list(map(float, row))
            if len(values) != len(expected) or not all(math.isfinite(v) for v in values):
                raise ValueError('invalid inverter sample')
            if rows and values[0] <= rows[-1][0]:
                raise ValueError('non-increasing inverter scale')
            rows.append(values)
            if len(rows) > 32000:
                raise ValueError('inverter point budget exceeded')
    if not rows:
        raise ValueError('empty inverter trace')
    return rows


def stimulus(t, supply):
    if t < .2e-9:
        return 0
    if t < .4e-9:
        return supply * (t - .2e-9) / .2e-9
    if t < 1.4e-9:
        return supply
    if t < 1.6e-9:
        return supply * (1.6e-9 - t) / .2e-9
    return 0


def check(rows, mode, supply):
    if mode == 'dc':
        if len(rows) != 21 or any(abs(row[0] - supply * i / 20) > 1e-12 for i, row in enumerate(rows)):
            raise ValueError('incomplete inverter DC sweep')
        if any(not -1e-8 <= row[1] <= supply + 1e-8 for row in rows):
            raise ValueError('inverter DC rail envelope')
        if any(b[1] > a[1] + 1e-8 for a, b in zip(rows, rows[1:])):
            raise ValueError('non-inverting DC transfer')
        if rows[0][1] < .99 * supply or rows[-1][1] > .01 * supply:
            raise ValueError('inverter does not restore logic levels')
        if rows[6][1] < .8 * supply or rows[14][1] > .2 * supply:
            raise ValueError('inverter transition is outside its input range')
        return dict(status='pass', points=len(rows))
    if len(rows) < 3000 or abs(rows[0][0]) > 1e-18 or abs(rows[-1][0] - 3e-9) > 1e-20:
        raise ValueError('incomplete inverter transient')
    if max(b[0] - a[0] for a, b in zip(rows, rows[1:])) > 1.01e-12:
        raise ValueError('inverter transient undersampled')
    if any(abs(row[2] - stimulus(row[0], supply)) > 1e-9 for row in rows):
        raise ValueError('incorrect inverter stimulus')
    # Terminal capacitance allows small feedthrough excursions beyond a rail.
    if any(not -.1 <= row[1] <= supply + .1 for row in rows):
        raise ValueError('inverter transient envelope')
    if rows[0][1] < .99 * supply or rows[-1][1] < .99 * supply:
        raise ValueError('inverter failed high-state recovery')
    low = min(rows, key=lambda row: abs(row[0] - 1.35e-9))[1]
    if low > .01 * supply:
        raise ValueError('inverter failed low-state recovery')
    transitions = []
    for a, b in zip(rows, rows[1:]):
        if a[1] < supply / 2 <= b[1] or a[1] > supply / 2 >= b[1]:
            crossing = a[0] + (supply / 2 - a[1]) * (b[0] - a[0]) / (b[1] - a[1])
            transitions.append(crossing)
    if len(transitions) != 2 or not .2e-9 < transitions[0] < 1.3e-9 or not 1.4e-9 < transitions[1] < 2.9e-9:
        raise ValueError('inverter missing or spurious transitions')
    return dict(status='pass', points=len(rows), falling_crossing_s=transitions[0], rising_crossing_s=transitions[1])


def compare(a, b, mode):
    if mode != 'dc':
        return common.compare(a, b)
    if len(a) != len(b) or any(abs(x[0] - y[0]) > 1e-12 for x, y in zip(a, b)):
        raise ValueError('different DC input samples')
    error = max(abs(x[1] - y[1]) for x, y in zip(a, b))
    if error > 2e-7:
        raise ValueError(f'inverter DC error {error} V')
    return dict(status='pass', maximum_error_V=error, tolerance_V=2e-7)


def deck(original_cell, corner, temperature, supply, load, mode):
    source = '0' if mode == 'dc' else f'PWL(0 0 .2n 0 .4n {supply} 1.4n {supply} 1.6n 0 3n 0)'
    analysis = f'dc VIN 0 {supply} {supply / 20:.17g}\nwrdata reference.dat v(output)' if mode == 'dc' else (
        'tran .125p 3n 0 .125p\nwrdata reference.dat v(output) v(input)')
    return f'''IHP inverter: original standard-cell CDL and PDK wrappers
.lib cornerMOSlv.lib {corner}
{original_cell}
VDD supply 0 {supply}
VIN input 0 {source}
XINV output input supply 0 sg13g2_inv_1
CLOAD output 0 {load:.17g}
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
    work = Path(tempfile.mkdtemp(prefix='inverter-', dir=args.work.resolve()))
    cdl = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/cdl/sg13g2_stdcell.cdl'
    report = dict(status='running', primary='cpphdl_osdi', reference='ngspice' if args.ngspice else None,
                  cases=[], work=str(work), full_chip_qualified=False,
                  scope='sg13g2_inv_1 schematic DC and loaded switching; no extracted clock-path claim')
    start = time.monotonic()
    deadline = start + 180

    def execute(command, directory, label):
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        wall = common.execute(command, directory, label, deadline)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        return dict(process_wall_s=wall, process_cpu_s=after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime)

    try:
        sources = [Path(__file__), Path(__file__).with_name('inverter_bench.cpp'), args.bench, cdl,
                   ROOT / 'basic/IHPSG13G2/inverter.hpp', ROOT / 'basic/IHPSG13G2/mos.hpp',
                   ROOT / 'basic/IHPSG13G2/parameters.py', ROOT / 'include/osdi.hpp',
                   ROOT / 'include/behavior.hpp', ROOT / 'include/behavior_devices.hpp',
                   ROOT / 'tests/IHPSG13G2/run.py', ROOT / 'tests/workloads/run.py']
        if args.ngspice:
            sources.append(args.ngspice)
        report['source_sha256'] = {str(p): common.digest(p) for p in sources}
        geometry, original_cell = cell(cdl)
        report['geometry'] = geometry
        for corner, temperature, supply, load in (
                ('mos_tt', 27, 1.2, 5e-15), ('mos_tt', 27, 1.2, 55e-15),
                ('mos_ss', 125, 1.08, 55e-15), ('mos_ff', -40, 1.32, 55e-15)):
            before = time.monotonic()
            bundles = {p: parameters.psp(args.pdk, corner, p, **geometry[p]) for p in ('n', 'p')}
            preparation = time.monotonic() - before
            for mode in ('dc', 'charge'):
                name = f'{corner}_{temperature}_{load * 1e15:g}fF_{mode}'
                directory = work / name
                directory.mkdir()
                for p, bundle in bundles.items():
                    parameters.write(bundle, directory / (p + '.params'))
                item = dict(name=name, status='running', parameter_preparation_wall_s=preparation if mode == 'dc' else 0,
                            model_sha256={p: bundle['input_sha256'] for p, bundle in bundles.items()})
                report['cases'].append(item)
                command = [args.bench, directory / 'n.params', directory / 'p.params', bundles['n']['binary'],
                           directory / 'native', str(temperature), str(supply), str(load), mode, '2.5e-13']
                item['native'] = execute(command, directory, 'native')
                native = trace(directory / 'native/cpp.csv', mode)
                item['native']['check'] = check(native, mode, supply)
                item['native']['solver'] = json.loads((directory / 'native/native.json').read_text())
                if mode == 'charge':
                    command[4], command[-1] = directory / 'refined', '1.25e-13'
                    item['native_refined'] = execute(command, directory, 'native_refined')
                    fine = trace(directory / 'refined/cpp.csv', mode)
                    item['native_refined']['check'] = check(fine, mode, supply)
                    item['native_refined']['solver'] = json.loads((directory / 'refined/native.json').read_text())
                    item['refinement'] = compare(native, fine, mode)
                    native = fine
                if args.ngspice:
                    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
                    (directory / '.spiceinit').write_text(f'osdi {bundles["n"]["binary"]}\nosdi {models.parent}/osdi/psp103_nqs.osdi\nsetcs sourcepath = ( {models} )\n')
                    (directory / 'bench.cir').write_text(deck(original_cell, corner, temperature, supply, load, mode))
                    item['reference'] = execute(['env', f'SPICE_USERINIT_DIR={directory}', args.ngspice, '-b', 'bench.cir'], directory, 'ngspice')
                    reference = trace(directory / 'reference.dat', mode, True)
                    item['reference']['check'] = check(reference, mode, supply)
                    item['comparison'] = compare(native, reference, mode)
                for bundle in bundles.values():
                    if any(common.digest(p) != h for p, h in bundle['input_sha256'].items()):
                        raise ValueError('PDK changed during inverter check')
                item['status'] = 'pass'
        if any(common.digest(p) != h for p, h in report['source_sha256'].items()):
            raise ValueError('source changed during inverter check')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        if report['cases'] and report['cases'][-1]['status'] == 'running':
            report['cases'][-1]['status'] = 'fail'
        raise
    finally:
        report['total_wall_s'] = time.monotonic() - start
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)
        table = ['Process wall and CPU include launch, model setup, solve and output.',
                 'Native uses the refined transient run; coarse refinement and PDK preprocessing are additional.',
                 'case                         C++ wall(s) CPU(s)  ngspice wall(s) CPU(s)  status']
        for item in report['cases']:
            row = [item['name']]
            for engine in (item.get('native_refined', item.get('native', {})), item.get('reference', {})):
                row.extend(f'{engine[key]:.6f}' if key in engine else 'n/a' for key in ('process_wall_s', 'process_cpu_s'))
            table.append(f'{row[0]:28} {row[1]:>10} {row[2]:>8} {row[3]:>16} {row[4]:>8}  {item["status"]}')
        (args.work / 'timing.txt').write_text('\n'.join(table) + '\n')
    print(f'PASS {len(report["cases"])} coupled IHP inverter cases; {report["total_wall_s"]:.3f} s total')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bench', 'pdk', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--ngspice', type=Path)
    args = parser.parse_args()
    args.bench = args.bench.resolve()
    if args.ngspice:
        args.ngspice = args.ngspice.resolve()
    run(args)
