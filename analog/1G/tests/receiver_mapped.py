#!/usr/bin/env python3
"""Map a validated receiver and repeat all frame, fault and enable checks."""
import argparse
from collections import Counter
import json
from pathlib import Path

from hardware import TestToolchain, configure_cleanup, execute
from receive_sample_recovery import digest

SOURCE = Path(__file__).resolve()


def run(args):
    work = args.work.resolve()
    proof = work / 'native_rtl_checks.json'
    evidence = json.loads(proof.read_text())
    if evidence.get('status') != 'pass' or evidence.get('module') != 'PCSReceiver' or not evidence.get('native_rtl_exact'):
        raise ValueError('receiver needs a passed native/RTL comparison first')
    hashes = {}
    for field in ('source_sha256', 'fixture_sha256', 'trace_sha256'):
        if not evidence.get(field):
            raise ValueError('missing receiver evidence: ' + field)
        hashes.update(evidence[field])
    if any(not Path(p).is_file() or digest(p) != h for p, h in hashes.items()):
        raise ValueError('receiver native/RTL evidence is stale')
    tools = args.tools.resolve() / 'bin'
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    rtl = [work / 'rtl/Predef_pkg.sv'] + sorted(p for p in (work / 'rtl').glob('*.sv') if p.name != 'Predef_pkg.sv')
    inputs = [SOURCE, proof, library, *rtl, args.verilator.resolve(),
              args.verilator.resolve().with_name('verilator_bin'), *[tools / n for n in ('yosys', 'abc', 'sv2v')]]
    hashes.update({str(p): digest(p) for p in inputs})
    toolchain = TestToolchain(args.cxx, args.verilator.resolve(), work)
    report = dict(status='running', module='PCSReceiver', source_sha256=hashes,
                  fixture_sha256=evidence['fixture_sha256'], scope='functional mapped-gate equivalence')

    def save():
        (work / 'hardware_checks.json').write_text(json.dumps(report, indent=2) + '\n')

    def command(name, argv, timeout):
        with (work / (name + '.log')).open('w') as log:
            execute(toolchain.command(argv), env=toolchain.env, stdout=log, stderr=-2,
                    check=True, timeout=timeout)

    def quote(path):
        if any(c in str(path) for c in '\n\\"'):
            raise ValueError('unsupported synthesis path')
        return '"' + str(path) + '"'

    save()
    try:
        with (work / 'design.v').open('w') as output:
            execute([str(tools / 'sv2v'), *map(str, rtl)], stdout=output, check=True, timeout=180)
        script = f'''read_liberty -lib {quote(library)}
read_verilog {quote(work / 'design.v')}
hierarchy -check -top PCSReceiver
synth -top PCSReceiver -flatten -noabc -noshare
dfflibmap -liberty {quote(library)}
abc -exe {quote(tools / 'abc')} -liberty {quote(library)}
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
clean
check -assert
write_verilog -noattr -noexpr {quote(work / 'mapped.v')}
write_json {quote(work / 'mapped.json')}
design -reset
read_liberty -ignore_miss_func -ignore_miss_dir {quote(library)}
write_verilog -noattr {quote(work / 'cells.v')}
'''
        (work / 'synth.ys').write_text(script)
        # Large flattened PCS mappings outgrow the generic unit runner's
        # ten-minute deadline; this stage gets its own bounded resource budget.
        command('synthesis', [tools / 'yosys', '-Q', '-T', '-s', work / 'synth.ys'], 3600)
        module = json.loads((work / 'mapped.json').read_text())['modules']['PCSReceiver']
        counts = Counter(cell['type'] for cell in module['cells'].values())
        if not counts or any(not name.startswith('sg13g2_') for name in counts):
            raise ValueError('receiver mapping contains non-IHP or blackbox cells')
        objects = work / 'mapped_obj'
        command('mapped-build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                '-j', '2', '--top-module', 'PCSReceiver', '--flatten', '--output-groups', '0',
                '--output-split', '10000', '--output-split-cfuncs', '500', '-Wno-fatal', '--Mdir', objects,
                '-CFLAGS', '-DVERILATOR -DMAPPED -std=c++17', work / 'mapped.v', work / 'cells.v',
                SOURCE.with_name('receiver.cpp')], 1800)
        for suffix, fixture, flags in (('_trace.csv', 'receiver-vectors.txt', []),
                                        ('_fault_trace.csv', 'receiver-fault-vectors.txt', ['--faults']),
                                        ('_enable_trace.csv', 'receiver-vectors.txt', ['--enable'])):
            command('mapped' + suffix.removesuffix('.csv'), [objects / 'VPCSReceiver', *flags,
                    work / fixture, work / ('mapped' + suffix)], 180)
            if digest(work / ('mapped' + suffix)) != digest(work / ('cpp' + suffix)):
                raise ValueError('mapped receiver trajectory differs: ' + suffix)
        if any(not Path(p).is_file() or digest(p) != h for p, h in hashes.items()):
            raise ValueError('receiver inputs changed during mapping/validation')
        report.update(status='pass', cells=dict(counts), mapped_sha256=digest(work / 'mapped.v'),
                      native_rtl_exact=True, native_mapped_exact=True)
        print('PASS IHP receiver: independent frame/fault/enable contracts and all native/mapped traces exact')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('work', 'pdk', 'tools', 'verilator'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
