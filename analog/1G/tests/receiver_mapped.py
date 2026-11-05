#!/usr/bin/env python3
"""Map a validated receiver and repeat all frame, fault and enable checks."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import shutil
import sys
import time

from hardware import TestToolchain, configure_cleanup, execute
from receive_sample_recovery import digest

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1] / 'IHPSG13G2'))
import receiver_allocator


def timing_mapping(delay_ps, load_ff):
    if delay_ps is None:
        return None
    for name, value in (('delay target', delay_ps), ('output load', load_ff)):
        if isinstance(value, bool) or not math.isfinite(value) or value <= 0:
            raise ValueError('ABC ' + name + ' must be finite and positive')
    return dict(delay_target_ps=delay_ps, output_load_ff=load_ff,
                driving_cell='sg13g2_buf_4', scope='mapping estimates; physical STA remains mandatory')


def write_abc_constraints(work, mapping):
    if mapping is None:
        return ''
    (work / 'abc.constr').write_text('set_driving_cell sg13g2_buf_4\n'
                                    f'set_load {mapping["output_load_ff"]:g}\n')
    # Yosys's ABC -constr parser retains quotes as filename characters.
    # Run synthesis in work and pass a fixed local filename without quotes.
    return f' -D {mapping["delay_target_ps"]:g} -constr abc.constr'


def synthesis_scripts(work, library, tools, abc_options):
    def quote(path):
        if any(c in str(path) for c in '\n\\"'):
            raise ValueError('unsupported synthesis path')
        return '"' + str(path) + '"'

    # Keep standard Yosys synth semantics, but release frontend memory before
    # technology lowering and retain each completed intermediate database.
    return [
        ('synthesis-coarse', f'''read_liberty -lib {quote(library)}
read_verilog {quote(work / 'design.v')}
synth -top PCSReceiver -flatten -noabc -noshare -run begin:fine
write_ilang {quote(work / 'coarse.rtlil')}
''', 'coarse.rtlil'),
        # These are the installed Yosys synth fine-stage commands. Separate
        # optimization from expansion so its temporary storage is released.
        ('synthesis-fine', f'''read_ilang {quote(work / 'coarse.rtlil')}
opt -fast -full
memory_map
opt -full
write_ilang {quote(work / 'fine.rtlil')}
''', 'fine.rtlil'),
        # Recursive expansion uses depth-first traversal instead of retaining
        # every breadth-first expansion frontier at once. No logic is omitted.
        ('synthesis-lowering', f'''read_ilang {quote(work / 'fine.rtlil')}
techmap -recursive
opt -fast
hierarchy -check
stat
check -assert
write_ilang {quote(work / 'lowered.rtlil')}
''', 'lowered.rtlil'),
        ('synthesis-technology', f'''read_ilang {quote(work / 'lowered.rtlil')}
dfflibmap -liberty {quote(library)}
write_ilang {quote(work / 'technology.rtlil')}
''', 'technology.rtlil'),
        ('synthesis-mapping', f'''read_ilang {quote(work / 'technology.rtlil')}
abc -exe {quote(tools / 'abc')} -liberty {quote(library)}{abc_options}
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
clean
check -assert
write_verilog -noattr -noexpr {quote(work / 'mapped.v')}
write_json {quote(work / 'mapped.json')}
design -reset
read_liberty -ignore_miss_func -ignore_miss_dir {quote(library)}
write_verilog -noattr {quote(work / 'cells.v')}
''', 'mapped.v'),
    ]


def coarse_checkpoint(origin, script):
    """Reuse only the exact coarse program and its complete original inputs."""
    origin = origin.resolve()
    proof = json.loads((origin / 'hardware_checks.json').read_text())
    if proof.get('module') != 'PCSReceiver' or not proof.get('source_sha256'):
        raise ValueError('coarse checkpoint lacks receiver provenance')
    path = origin / 'coarse.rtlil'
    if proof.get('synthesis_checkpoints', {}).get('coarse.rtlil') != digest(path):
        raise ValueError('coarse checkpoint changed')
    if (origin / 'synthesis-coarse.ys').read_text() != script:
        raise ValueError('coarse synthesis program changed')
    for name, value in proof['source_sha256'].items():
        # The driver may gain resourcing/recovery code. Its old bytes remain
        # bound, while the exact coarse program above must remain unchanged.
        source = origin / 'receiver_mapped.py' if Path(name) == SOURCE else Path(name)
        if not source.is_file() or digest(source) != value:
            raise ValueError('coarse checkpoint input changed: ' + name)
    return path


def run(args):
    mapping = timing_mapping(getattr(args, 'abc_delay_ps', None), getattr(args, 'abc_load_ff', 15.0))
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
    allocator_env, allocator, allocator_files = receiver_allocator.configuration(getattr(args, 'allocator', None))
    for path in [Path(receiver_allocator.__file__), Path('/usr/bin/time'), *allocator_files]:
        hashes[str(path)] = digest(path)
    resume = getattr(args, 'resume_coarse', None)
    coarse_script = synthesis_scripts(work, library, tools, '')[0][1]
    if resume:
        coarse = coarse_checkpoint(resume, coarse_script)
        for path in (resume / name for name in ('hardware_checks.json', 'receiver_mapped.py',
                                               'synthesis-coarse.ys', 'coarse.rtlil')):
            hashes[str(path.resolve())] = digest(path)
    toolchain = TestToolchain(args.cxx, args.verilator.resolve(), work)
    report = dict(status='running', module='PCSReceiver', source_sha256=hashes,
                  fixture_sha256=evidence['fixture_sha256'], scope='functional mapped-gate equivalence',
                  timing_mapping=mapping, stages=[], synthesis_checkpoints={}, allocator=allocator,
                  synthesis_address_space_bytes=4 * 1024**3,
                  fine_optimization_address_space_bytes=1024**3)
    if resume:
        report['resumed_coarse'] = str(resume.resolve())

    def save():
        temporary = work / 'hardware_checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'hardware_checks.json')

    def command(name, argv, timeout, cwd=None):
        started = time.monotonic()
        report['stage'] = name
        save()
        try:
            environment = toolchain.env
            synthesis = name.startswith('synthesis-')
            fine = name == 'synthesis-fine'
            if synthesis:
                environment = receiver_allocator.environment(environment, allocator_env)
                # Optimization of the compact RTLIL uses its own smaller
                # budget; gate expansion still owns the shared heavy slot.
                limit = (1 if fine else 4) * 1024**3
                argv = ['/usr/bin/time', '-v', '-o', work / (name + '.resources'),
                        'prlimit', '--as=' + str(limit), '--core=0', '--', *argv]
            with (work / (name + '.log')).open('w') as log:
                execute(toolchain.command(argv), env=environment, stdout=log, stderr=-2,
                        check=True, timeout=timeout, cwd=cwd,
                        memory_exclusive=(not fine) if synthesis else None)
        finally:
            report['stages'].append(dict(name=name, seconds=time.monotonic() - started))
            save()

    save()
    try:
        abc_options = write_abc_constraints(work, mapping)
        if mapping:
            constraints = work / 'abc.constr'
            hashes[str(constraints)] = digest(constraints)
        with (work / 'design.v').open('w') as output:
            execute([str(tools / 'sv2v'), *map(str, rtl)], stdout=output, check=True, timeout=180)
        for name, script, checkpoint in synthesis_scripts(work, library, tools, abc_options):
            path = work / (name + '.ys')
            path.write_text(script)
            hashes[str(path)] = digest(path)
            save()
            # Bound each distinct workload separately. A completed frontend
            # survives a later technology/mapping timeout for diagnosis/reuse.
            if name == 'synthesis-coarse' and resume:
                destination = work / checkpoint
                if not destination.exists() or not destination.samefile(coarse):
                    shutil.copyfile(coarse, destination)
            else:
                command(name, [tools / 'yosys', '-Q', '-T', '-s', path], 3600, cwd=work)
            path = work / checkpoint
            report['synthesis_checkpoints'][checkpoint] = digest(path)
            save()
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
    parser.add_argument('--allocator', type=Path, help='explicit local libjemalloc.so.2 for bounded synthesis')
    parser.add_argument('--resume-coarse', type=Path,
                        help='retained coarse checkpoint, receipt, program and original driver source')
    parser.add_argument('--abc-delay-ps', type=float,
                        help='explicit ABC delay target in ps; enables constrained timing mapping')
    parser.add_argument('--abc-load-ff', type=float, default=15.0,
                        help='ABC primary-output load in fF when timing mapping is enabled (default 15)')
    run(parser.parse_args())
