#!/usr/bin/env python3
"""Check compact SAR banks after IHP cell mapping and measure actual cell area."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path[:0] = [str(SOURCE.parent), str(ANALOG / '1G/IHPSG13G2')]
from hardware import configure_cleanup, execute
from mapped_area import library_areas, lef_areas


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def quote(path):
    if any(c in str(path) for c in ('"', '\\', '\n')):
        raise ValueError('unsupported synthesis path')
    return '"' + str(path) + '"'


def run(args):
    proof = json.loads(args.proof.read_text())
    if proof.get('status') != 'pass' or set(proof['cases']) != {'12', '13', '16'}:
        raise ValueError('complete independently checked native/RTL bank proof required')
    hashes = dict(proof['source_sha256']) | dict(proof['rtl_sha256'])
    paths = [SOURCE, SOURCE.with_name('adc_bank.cpp'), args.proof.resolve(), args.verilator.resolve(),
             ANALOG / '1G/IHPSG13G2/mapped_area.py']
    binary = args.tools.resolve() / 'bin'
    paths += [binary / name for name in ('yosys', 'abc', 'sv2v')]
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    liberty = library / 'lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    lef = library / 'lef/sg13g2_stdcell.lef'
    paths += [liberty, lef]
    hashes.update({str(p): digest(p) for p in paths})
    if any(digest(p) != h for p, h in hashes.items()):
        raise ValueError('stale compact SAR bank proof')
    areas, footprints = library_areas(liberty.read_text()), lef_areas(lef.read_text())
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    top = 'InterleavedSARBankController'
    original = Path(proof['work'])
    rtl = [original / 'rtl' / name for name in ('Predef_pkg.sv', 'ADCSARController.sv', top + '.sv')]
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        scope='native/RTL/IHP mapped functional proof and Liberty/LEF cell area; no routed 250 MHz timing or physical ADC qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)

    def command(case, label, argv, output=None):
        with (output or case / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=case, stdout=log, stderr=-2, check=True, timeout=1200)

    save()
    try:
        for count in (13, 16):
            case = work / str(count)
            case.mkdir()
            command(case, 'sv2v', [binary / 'sv2v', *rtl], case / 'design.v')
            script = f'''read_liberty -lib {quote(liberty)}
read_verilog {quote(case / 'design.v')}
chparam -set Slices {count} {top}
hierarchy -check -top {top}
synth -top {top} -flatten -noabc -noshare
dfflibmap -liberty {quote(liberty)}
abc -exe {quote(binary / 'abc')} -liberty {quote(liberty)}
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
clean
check -assert
write_verilog -noattr -noexpr {quote(case / 'mapped.v')}
write_json {quote(case / 'mapped.json')}
design -reset
read_liberty -ignore_miss_func -ignore_miss_dir {quote(liberty)}
write_verilog -noattr {quote(case / 'cells.v')}
'''
            (case / 'synth.ys').write_text(script)
            command(case, 'synthesis', [binary / 'yosys', '-Q', '-T', '-s', case / 'synth.ys'])
            module = json.loads((case / 'mapped.json').read_text())['modules'][top]
            cells = Counter(cell['type'] for cell in module['cells'].values())
            if not cells or any(not name.startswith('sg13g2_') for name in cells):
                raise ValueError('SAR bank mapping retained a non-IHP cell')
            for name in cells:
                if name not in areas or name not in footprints:
                    raise ValueError('used IHP cell has no positive Liberty/LEF area')
            for name, width in (('comparisons_in', count), ('trial_out', count * 8), ('acquire_out', count), ('convert_out', count)):
                if len(module['ports'][name]['bits']) != width:
                    raise ValueError('mapped SAR bank lost an array port bit')
            mapped, changes = re.subn(r'\bmodule\s+' + top + r'\s*\(', 'module ' + top + '_mapped(',
                                      (case / 'mapped.v').read_text(), count=1)
            if changes != 1:
                raise ValueError('missing mapped SAR bank top')
            (case / 'mapped_flat.v').write_text(mapped)
            ports = ['clk', 'reset', 'enable_in', 'sample_enable_in', 'comparisons_in', 'trial_out',
                     'acquire_out', 'convert_out', 'data_out', 'slice_out', 'valid_out', 'fault_out']
            connections = ['.' + name + '(' + ('{' + ', '.join(f'{name}[{i}]' for i in range(count)) + '}'
                if name in ('trial_out', 'acquire_out', 'convert_out') else name) + ')' for name in ports]
            wrapper = f'''module {top}(
input wire clk, reset, enable_in, sample_enable_in,
input wire [{count - 1}:0] comparisons_in,
output wire [7:0] trial_out[{count}],
output wire acquire_out[{count}], convert_out[{count}],
output wire [7:0] data_out,
output wire [3:0] slice_out,
output wire valid_out, fault_out);
{top}_mapped implementation(
''' + ',\n'.join(connections) + ');\nendmodule\n'
            (case / 'mapped_interface.sv').write_text(wrapper)
            object_dir = case / 'obj'
            command(case, 'compile', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                '--flatten', '--output-groups', '0', '-j', '1', '--top-module', top, '-Wno-fatal', '--Mdir', object_dir,
                '-CFLAGS', f'-DVERILATOR -DBANK_SLICES={count} -std=c++17', case / 'mapped_flat.v',
                case / 'mapped_interface.sv', case / 'cells.v', SOURCE.with_name('adc_bank.cpp'),
                '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx])
            command(case, 'mapped', [object_dir / ('V' + top), count, case / 'mapped_trace.csv'])
            expected = original / f'cpp_{count}.csv'
            if digest(expected) != proof['cases'][str(count)]['native_trace_sha256'] or expected.read_bytes() != (case / 'mapped_trace.csv').read_bytes():
                raise ValueError('IHP mapped SAR bank trajectory differs from independently checked native output')
            report['cases'][str(count)] = dict(status='pass', cell_count=sum(cells.values()), cells=dict(cells),
                cell_area_um2=sum(footprints[name] * number for name, number in cells.items()),
                area_basis='LEF cell footprint',
                liberty_area_um2=sum(areas[name] * number for name, number in cells.items()),
                liberty_lef_differences={name: dict(liberty_um2=areas[name], lef_um2=footprints[name])
                    for name in cells if abs(areas[name] - footprints[name]) > 1e-6},
                mapped_sha256=digest(case / 'mapped.v'), interface_sha256=digest(case / 'mapped_interface.sv'),
                trace_sha256=digest(case / 'mapped_trace.csv'))
            save()
            print('PASS IHP mapped bank', count, report['cases'][str(count)]['cell_area_um2'], 'um2', flush=True)
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('SAR bank mapping inputs changed')
        report['cell_area_reduction_percent'] = 100 * (1 - report['cases']['13']['cell_area_um2'] / report['cases']['16']['cell_area_um2'])
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'tools', 'pdk', 'verilator', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
