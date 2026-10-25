#!/usr/bin/env python3
"""Check the four-lane DSP and its PCS connection in C++ and generated RTL."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile
import re

from hardware import TestToolchain, configure_cleanup, execute, source_dependencies
from receive_sample_recovery import archive, digest
from receive_dsp_reference import generate
from receive_dsp_pcs_reference import generate as generate_pcs

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    stem = 'receive_dsp_pcs' if args.pcs else 'receive_dsp'
    module = 'ReceiveDSPPCS' if args.pcs else 'ReceiveDSP'
    fixture = SOURCE.with_name(stem + '.cpp')
    entry = ANALOG / ('1G/' + stem + '.cpp')
    compiler = Path(shutil.which(args.cxx) or args.cxx).absolute()
    tools = [args.native.resolve(), args.converter.resolve(), args.verilator.resolve(), compiler]
    verilator_bin = args.verilator.resolve().with_name('verilator_bin')
    if verilator_bin.is_file():
        tools.append(verilator_bin)
    helpers = [SOURCE, SOURCE.with_name('hardware.py'), SOURCE.with_name('receive_dsp_reference.py'),
               SOURCE.with_name('filter_pipeline_reference.py'), SOURCE.with_name('receive_sample_recovery.py'),
               SOURCE.with_name('receive_dsp_pcs_reference.py'), SOURCE.with_name('transmit_reference.py'),
               ANALOG.parent / 'tests/TestToolchain.py',
               ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    if args.pcs:
        helpers += [SOURCE.with_name('pcs_data.csv'), SOURCE.with_name('pcs_controls.csv')]
    hashes = source_dependencies([fixture, entry], ANALOG.parent, str(compiler))
    hashes.update({str(p): digest(p) for p in tools + helpers})
    toolchain = TestToolchain(compiler, args.verilator.resolve(), work)
    report = dict(status='running', module=module, work=str(work), source_sha256=hashes, stages={},
                  scope=('four recovered-symbol DSP lanes' + (' with pair acquisition and PCS' if args.pcs else '') +
                         '; supervised training; no analog, autonomous startup or physical timing qualification'))

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (root / 'checks.json').write_text(payload)

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            execute(toolchain.command(argv), cwd=work, env=toolchain.env, stdout=log, stderr=-2,
                    check=True, timeout=3600)

    save()
    try:
        vectors = work / ('fixtures' if args.pcs else 'vectors.txt')
        report['fixture'] = (generate_pcs if args.pcs else generate)(vectors)
        report['fixture_sha256'] = ({p.name: digest(p) for p in vectors.iterdir()} if args.pcs else digest(vectors))
        def check_coverage(path):
            output = path.read_text()
            if args.pcs:
                rows = re.findall(r'^case (\d+) master=(\d+) frames=(\d+) bytes=(\d+) clocks=(\d+) PASS$', output, re.M)
                actual = [tuple(map(int, row)) for row in rows]
                expected = [(i, i // 2, 2, 1598, 2 * row['symbols'] + 2) for i, row in enumerate(report['fixture'])]
                if actual != expected or len(output.splitlines()) != 4:
                    raise ValueError('missing DSP/PCS role/channel/frame coverage: ' + output)
            elif output != ('PASS {clocks} clocks, {valid_observations} four-lane observations\n'
                            .format(**report['fixture'])):
                raise ValueError('DSP coverage does not match the independent oracle')
        command('native', [args.native.resolve(), vectors, work / 'native.csv'])
        check_coverage(work / 'native.log')
        report['stages']['native'] = dict(status='pass', independent_expectations=True)
        save()
        command('convert', [args.converter.resolve(), '--generated-dir', work / 'rtl',
                           '--primary_clock', 'clk', '250000000', entry, '--',
                           '-I' + str(ANALOG.parent / 'include')])
        if any(s in (work / 'convert.log').read_text() for s in ('MISSED CALL', 'Missing register strobe')):
            raise ValueError('converter reported incomplete state scheduling')
        rtl = [work / 'rtl/Predef_pkg.sv'] + sorted(
            p for p in (work / 'rtl').glob('*.sv') if p.name != 'Predef_pkg.sv')
        objects = work / 'rtl_obj'
        command('rtl_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                             '-j', '2', '--top-module', module, '-Wno-fatal', '--Mdir', objects,
                             '-CFLAGS', '-DVERILATOR -std=c++17', *rtl, fixture,
                             '-MAKEFLAGS', 'OPT_FAST=-O0 OPT_SLOW=-O0'])
        command('rtl', [objects / ('V' + module), vectors, work / 'rtl.csv'])
        check_coverage(work / 'rtl.log')
        if (work / 'rtl.log').read_text() != (work / 'native.log').read_text() or digest(work / 'native.csv') != digest(work / 'rtl.csv'):
            raise ValueError('native/RTL trace or coverage mismatch')
        report['stages']['rtl'] = dict(status='pass', independent_expectations=True, exact_native=True)
        report['traces'] = {flow: archive(work / (flow + '.csv')) for flow in ('native', 'rtl')}
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('qualification input changed during run')
        report.update(status='pass', rtl_sha256={str(p): digest(p) for p in rtl})
        # Retain compact evidence; fixtures are deterministic and objects are disposable.
        shutil.rmtree(objects)
        if args.pcs:
            shutil.rmtree(vectors)
        else:
            vectors.unlink()
        print('PASS ' + module + ': independent C++/RTL checks and exact trace comparison')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native', 'converter', 'verilator', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--pcs', action='store_true', help='check the composed DSP/pair-acquisition/PCS receiver')
    run(parser.parse_args())
