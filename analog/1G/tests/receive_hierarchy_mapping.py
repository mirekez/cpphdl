#!/usr/bin/env python3
"""Map the composed receiver without duplicating lane logic in synthesis memory."""
import argparse
from collections import Counter
import json
from pathlib import Path
import shutil
import tempfile
import sys

from hardware import TestToolchain, configure_cleanup, execute
from receive_sample_recovery import archive, digest
from receive_dsp_reference import generate
from receive_dsp_pcs_reference import generate as generate_pcs

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1] / 'IHPSG13G2'))
from mapped_area import library_areas


def leaf_cells(modules, top, areas):
    """Count instantiated physical leaves, rejecting unknown or recursive cells."""
    completed, visiting = {}, set()
    def count(name):
        if name in areas and name.startswith('sg13g2_'):
            return Counter({name: 1})
        if name in completed:
            return completed[name]
        if name in visiting or name not in modules or modules[name].get('attributes', {}).get('blackbox'):
            raise ValueError('unmapped, blackbox or recursive cell: ' + name)
        visiting.add(name)
        total = Counter()
        for cell in modules[name]['cells'].values():
            total.update(count(cell['type']))
        visiting.remove(name)
        completed[name] = total
        return total
    return count(top)


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
    area_helper = SOURCE.parents[1] / 'IHPSG13G2/mapped_area.py'
    hashes[str(area_helper)] = digest(area_helper)
    binary = args.tools.resolve() / 'bin'
    liberty = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    compiler = Path(shutil.which(args.cxx) or args.cxx).resolve()
    hashes.update({str(p): digest(p) for p in [liberty, binary / 'yosys', binary / 'abc', binary / 'sv2v',
                                             compiler, args.verilator.resolve(),
                                             args.verilator.resolve().with_name('verilator_bin')]})
    previous = None
    if args.resume_mapping:
        previous = json.loads(args.resume_mapping.read_text())
        prior_work = Path(previous['work'])
        if (previous.get('module') != expected_module or
                previous.get('stages', {}).get('mapping', {}).get('status') != 'pass' or
                digest(prior_work / 'mapped.v') != previous.get('mapped_sha256')):
            raise ValueError('resume requires a complete, matching mapped netlist')
        for path, expected in previous['source_sha256'].items():
            original = Path(path)
            # A stopped build may preserve its exact runner before the runner
            # itself is improved. All circuit, fixture and tool inputs must
            # still match the current files; only the runner can be archived.
            if original.resolve() == SOURCE and digest(original) != expected:
                original = prior_work / 'mapping_runner.py'
            if not original.is_file() or digest(original) != expected:
                raise ValueError('stale mapping input: ' + path)
            hashes[str(original)] = expected
        hashes[str(SOURCE)] = digest(SOURCE)
        hashes[str(args.resume_mapping.resolve())] = digest(args.resume_mapping)
        hashes[str(prior_work / 'mapped.v')] = digest(prior_work / 'mapped.v')

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
        if previous is None:
            with (work / 'design.v').open('w') as output:
                command('lower', [binary / 'sv2v', *rtl], output)
            script = f'''read_liberty -lib {quote(liberty)}
read_verilog {quote(work / 'design.v')}
hierarchy -check -top {expected_module}
synth -top {expected_module} -noabc -noshare
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
        else:
            shutil.copyfile(prior_work / 'mapped.v', work / 'mapped.v')
            # Re-read the actual netlist for an independent leaf inventory;
            # regenerate simulation cells from the verified Liberty input.
            script = f'''read_liberty -lib {quote(liberty)}
read_verilog {quote(work / 'mapped.v')}
hierarchy -check -top {expected_module}
check -assert
stat -liberty {quote(liberty)}
write_json {quote(work / 'mapped.json')}
design -reset
read_liberty -ignore_miss_func -ignore_miss_dir {quote(liberty)}
write_verilog -noattr {quote(work / 'cells.v')}
'''
            report['resumed_mapping'] = str(args.resume_mapping.resolve())
        (work / 'synth.ys').write_text(script)
        command('synthesis', [binary / 'yosys', '-Q', '-T', '-s', work / 'synth.ys'])
        mapped = json.loads((work / 'mapped.json').read_text())['modules']
        areas = library_areas(liberty.read_text())
        counts = leaf_cells(mapped, expected_module, areas)
        if not counts:
            raise ValueError('mapping contains no physical cells')
        report.update(cells=counts, mapped_sha256=digest(work / 'mapped.v'),
                      synthesis_hierarchy=True,
                      mapped_cell_area_um2=sum(areas[name] * number for name, number in counts.items()))
        report['stages']['mapping'] = dict(status='pass', cells=sum(counts.values()))
        save()
        harness = SOURCE.with_name('receive_dsp_pcs.cpp' if args.pcs else 'receive_dsp.cpp')
        objects = work / 'mapped_obj'
        # Compile one model for the repeated filter lane and a separate PCS
        # model. The netlist is unchanged and every leaf still simulates.
        boundaries = ['ReceiveFilter'] + (['PCSReceiver'] if args.pcs else [])
        configuration = work / 'hierarchy.vlt'
        configuration.write_text('`verilator_config\n' + ''.join(
            'hier_block -module "' + name + '"\n' for name in boundaries))
        command('mapped_build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
                                '-j', '2', '--top-module', expected_module, '--hierarchical', '--output-groups', '0',
                                '--output-split', '10000', '--output-split-cfuncs', '500', '-Wno-fatal',
                                '--Mdir', objects, '-CFLAGS', '-DVERILATOR -std=c++17',
                                configuration, work / 'mapped.v', work / 'cells.v', harness,
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
    parser.add_argument('--resume-mapping', type=Path,
                        help='reuse a hash-checked mapping receipt after a stopped simulation build')
    run(parser.parse_args())
