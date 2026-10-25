#!/usr/bin/env python3
"""Map a freshly checked receive DSP hierarchy to IHP cells and recheck it."""
import argparse
from collections import Counter
import json
from pathlib import Path
import shutil
import tempfile

from hardware import TestToolchain, configure_cleanup, execute
from receive_sample_recovery import archive, digest
from receive_dsp_reference import generate
from receive_dsp_pcs_reference import generate as generate_pcs

SOURCE = Path(__file__).resolve()


def quote(path):
    if any(c in str(path) for c in ('"', '\\', '\n')):
        raise ValueError('unsupported synthesis path')
    return '"' + str(path) + '"'


def run(args):
    evidence = json.loads(args.functional.read_text())
    if evidence['status'] != 'pass' or not evidence['stages']['rtl']['exact_native']:
        raise ValueError('a passing native/RTL receipt is required')
    expected_module = 'ReceiveDSPPCS' if args.pcs else 'ReceiveDSP'
    if evidence['module'] != expected_module:
        raise ValueError('receipt belongs to the wrong module')
    rtl = list(map(Path, evidence['rtl_sha256']))
    if not any(p.name == expected_module + '.sv' for p in rtl):
        raise ValueError('receipt belongs to the wrong module')
    hashes = evidence['source_sha256'] | evidence['rtl_sha256']
    hashes[str(args.functional.resolve())] = digest(args.functional)
    hashes[str(SOURCE)] = digest(SOURCE)
    binary = args.tools.resolve() / 'bin'
    liberty = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    compiler = Path(shutil.which(args.cxx) or args.cxx).resolve()
    hashes.update({str(p): digest(p) for p in [liberty, binary / 'yosys', binary / 'abc', binary / 'sv2v',
                                             compiler, args.verilator.resolve(),
                                             args.verilator.resolve().with_name('verilator_bin')]})

    def unchanged():
        if any(not Path(p).is_file() or digest(Path(p)) != h for p, h in hashes.items()):
            raise ValueError('missing or changed qualification source/tool/RTL input')

    unchanged()
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    toolchain = TestToolchain(args.cxx, args.verilator.resolve(), work)
    report = dict(status='running', module=expected_module, work=str(work), source_sha256=hashes,
                  stages={}, scope='IHP mapped functional equivalence; no physical timing qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'hardware_checks.json').write_text(payload)
        (work / 'hardware_checks.json').write_text(payload)

    def command(label, argv, output=None):
        with (work / (label + '.log')).open('w') as log:
            execute(toolchain.command(argv), cwd=work, env=toolchain.env,
                    stdout=log if output is None else output, stderr=log,
                    check=True, timeout=3600)

    save()
    try:
        fixture = work / ('fixtures' if args.pcs else 'vectors.txt')
        (generate_pcs if args.pcs else generate)(fixture)
        fixture_hash = ({p.name: digest(p) for p in fixture.iterdir()} if args.pcs else digest(fixture))
        if fixture_hash != evidence['fixture_sha256']:
            raise ValueError('regenerated fixture differs from the native/RTL qualification')
        with (work / 'design.v').open('w') as output:
            command('lower', [binary / 'sv2v', *rtl], output)
        script = f'''read_liberty -lib {quote(liberty)}
read_verilog {quote(work / 'design.v')}
hierarchy -check -top {expected_module}
synth -top {expected_module} -flatten -noabc -noshare
dfflibmap -liberty {quote(liberty)}
abc -exe {quote(binary / 'abc')} -liberty {quote(liberty)}
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
clean
check -assert
stat -liberty {quote(liberty)}
write_verilog -noattr -noexpr {quote(work / 'mapped.v')}
write_json {quote(work / 'mapped.json')}
design -reset
read_liberty -ignore_miss_func -ignore_miss_dir {quote(liberty)}
write_verilog -noattr {quote(work / 'cells.v')}
'''
        (work / 'synth.ys').write_text(script)
        command('synthesis', [binary / 'yosys', '-Q', '-T', '-s', work / 'synth.ys'])
        mapped = json.loads((work / 'mapped.json').read_text())['modules'][expected_module]
        counts = Counter(cell['type'] for cell in mapped['cells'].values())
        if not counts or any(not cell.startswith('sg13g2_') for cell in counts):
            raise ValueError('mapping contains non-IHP cells')
        report.update(cells=counts, mapped_sha256=digest(work / 'mapped.v'))
        report['stages']['mapping'] = dict(status='pass', cells=sum(counts.values()))
        save()
        harness = SOURCE.with_name('receive_dsp_pcs.cpp' if args.pcs else 'receive_dsp.cpp')
        objects = work / 'mapped_obj'
        command('mapped_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                                '-j', '2', '--top-module', expected_module, '--flatten', '--output-groups', '0',
                                '--output-split', '10000', '--output-split-cfuncs', '500', '-Wno-fatal',
                                '--Mdir', objects, '-CFLAGS', '-DVERILATOR -std=c++17',
                                work / 'mapped.v', work / 'cells.v', harness,
                                '-MAKEFLAGS', 'OPT_FAST=-O0 OPT_SLOW=-O0'])
        command('mapped', [objects / ('V' + expected_module), fixture, work / 'mapped.csv'])
        if digest(work / 'mapped.csv') != evidence['traces']['native']['raw_sha256']:
            raise ValueError('native/mapped-gate trajectory mismatch')
        unchanged()
        report['stages']['mapped'] = dict(status='pass', independent_expectations=True, exact_native=True)
        report.update(status='pass', native_mapped_exact=True, trace=archive(work / 'mapped.csv'))
        shutil.rmtree(objects)
        if args.pcs:
            shutil.rmtree(fixture)
        else:
            fixture.unlink()
        # JSON is needed only for the cell inventory; keep the actual netlist.
        (work / 'mapped.json').unlink()
        print('PASS ' + expected_module + ': ' + str(sum(counts.values())) + ' IHP cells; exact native/gate trace')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('functional', 'tools', 'pdk', 'verilator', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--pcs', action='store_true')
    run(parser.parse_args())
