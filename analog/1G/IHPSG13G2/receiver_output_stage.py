#!/usr/bin/env python3
"""Buffer slow receiver compound-gate outputs, then prove and screen the candidate."""
import argparse
import gc
import hashlib
import json
import os
from pathlib import Path
import re
import time

from buffer_equivalence import compare as buffer_compare
from receiver_final_repair import final_screen
from receiver_path_sizing import HERE, ROOT, slack
from receiver_pin_stage import require_origin
from receiver_route_probe import screen as require_unbuffer
from rebuffer_equivalence import compare as rebuffer_compare
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def digest(path):
    # The guard tests also run with CMake's Python 3.10. Keep hashing bounded
    # in memory without requiring hashlib.file_digest (Python 3.11).
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def select_outputs(report, limit=128):
    """Use only explicit output rows from slow data paths, never clock rows.

    Report units are ns and pF. A >=250 ps slew identifies gates whose output
    drive is a material part of the path delay even below the 500 ps limit.
    Keep critical-path order and deduplicate pins shared by several paths.
    """
    if isinstance(limit, bool) or not isinstance(limit, int) or not 1 <= limit <= 128:
        raise ValueError('output limit must be an integer from 1 to 128')
    pins = []
    for line in report.splitlines():
        fields = line.split()
        if len(fields) != 8 or not fields[0].isdigit():
            continue
        if not re.fullmatch(r'\(sg13g2_(xn?or2|o21ai|a221oi)_1\)', fields[7]):
            continue
        pin = fields[6]
        if not re.fullmatch(r'[A-Za-z0-9_]+/[XY]', pin):
            raise ValueError('unexpected output identifier: ' + pin)
        cap, slew = float(fields[1]), float(fields[2])
        if cap >= .010 and slew >= .250 and pin not in pins:
            pins.append(pin)
    return pins[:limit]


def unbuffer_origin(origin, measurement, proof):
    """Admit a measured global diagnostic for repair, never for qualification.

    The measurement must bind this exact producer receipt, ODB, SDC and
    netlist, and reproduce nominal timing before and after its RC change.
    Enlarged-C electrical failures are allowed here because this stage repairs
    them. Nominal endpoint/electrical checks are still required.
    """
    require_unbuffer(proof)
    measured = json.loads((measurement / 'checks.json').read_text())
    if (measured.get('status') != 'global_margin_measurement_complete' or
            measured.get('stage') != 'complete'):
        raise ValueError('completed global margin measurement required')
    connectivity = measured.get('connectivity', {})
    if (connectivity.get('status') != 'pass' or connectivity.get('top') != 'PCSReceiver' or
            connectivity.get('removed_buffers') != 0 or connectivity.get('removed_fillers') != 0):
        raise ValueError('measurement must preserve every cell and endpoint')
    hashes = {}
    for entries in (proof['source_sha256'], proof['artifact_sha256'],
                    measured['input_sha256'], measured['artifact_sha256']):
        for name, expected in entries.items():
            if name in hashes and hashes[name] != expected:
                raise ValueError('conflicting global diagnostic provenance')
            hashes[name] = expected
    required = [origin / 'checks.json', origin / 'candidate.odb', origin / 'candidate.sdc',
                origin / 'after.v', measurement / 'reloaded.v', measurement / 'margin_paths.rpt']
    required += [measurement / (name + '_' + kind + '.rpt')
                 for name in ('before', 'margin', 'restored') for kind in ('setup', 'hold')]
    for path in required:
        if hashes.get(str(path)) != digest(path):
            raise ValueError('unbound or changed global diagnostic input: ' + str(path))
    # Also require the measurement itself to bind this producer, not merely
    # merge unrelated receipts with matching timing numbers.
    if measured['input_sha256'].get(str(origin / 'checks.json')) != digest(origin / 'checks.json'):
        raise ValueError('measurement does not bind this diagnostic producer')
    for name in ('before', 'restored'):
        estimate = measured['estimates'][name]
        if estimate.get('electrical_pass') is not True:
            raise ValueError('nominal diagnostic electrical check failed')
        for kind, mode in (('setup', 'max'), ('hold', 'min')):
            value = slack(measurement / (name + '_' + kind + '.rpt'), mode)
            if (abs(value - proof['rounds'][-1][kind + '_ns']) > .001 or
                    abs(value - estimate[kind + '_ns']) > .001):
                raise ValueError('diagnostic nominal reload/restoration differs by more than 1 ps')
    hashes[str(measurement / 'checks.json')] = digest(measurement / 'checks.json')
    for name, expected in hashes.items():
        if digest(name) != expected:
            raise ValueError('global diagnostic provenance changed: ' + name)
    return hashes


def run(args):
    if not 1 <= args.rounds <= 4:
        raise ValueError('output rounds must be from 1 to 4')
    origin, work = args.origin.resolve(), args.output.resolve()
    proof = json.loads((origin / 'checks.json').read_text())
    measurement = getattr(args, 'margin_measurement', None)
    if measurement:
        measurement = measurement.resolve()
        hashes = unbuffer_origin(origin, measurement, proof)
        seed_db, seed_verilog = origin / 'candidate.odb', origin / 'after.v'
        timing_origin = measurement
    else:
        require_origin(proof)
        hashes = dict(proof['source_sha256'])
        for name, value in proof['artifact_sha256'].items():
            if name in hashes and hashes[name] != value:
                raise ValueError('conflicting output-stage provenance')
            hashes[name] = value
        seed_db, seed_verilog = origin / 'global.odb', origin / 'global.v'
        timing_origin = origin
    constraints, models = args.constraints.resolve(), args.models.resolve()
    tools, pdk = args.tools.resolve(), args.pdk.resolve()
    library = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    required = [seed_db, seed_verilog, timing_origin / 'margin_paths.rpt',
                constraints, models, tools / 'bin/openroad', tools / 'root/usr/bin/openroad',
                library / 'lef/sg13g2_tech.lef']
    required += [library / ('lib/sg13g2_stdcell_' + corner + '.lib')
                 for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C')]
    # Older global diagnostics did not record the tech LEF separately. Bind it
    # now, then require the native RC reload to reproduce measured timing.
    if measurement:
        path = library / 'lef/sg13g2_tech.lef'
        hashes.setdefault(str(path), digest(path))
        if constraints != origin / 'candidate.sdc':
            raise ValueError('diagnostic repair must use the saved candidate SDC')
    for path in required:
        if hashes.get(str(path)) != digest(path):
            raise ValueError('output-stage input differs from producer: ' + str(path))
    for path in (Path(__file__).resolve(), HERE / 'receiver_output_stage.tcl',
                 HERE / 'receiver_output_buffers.tcl', HERE / 'receiver_pin_stage.py',
                 HERE / 'receiver_final_repair.py', HERE / 'receiver_margin_route.py',
                 HERE / 'buffer_equivalence.py', HERE / 'rebuffer_equivalence.py',
                 HERE / 'sizing_equivalence.py', HERE / 'receiver_route_probe.py',
                 origin / 'checks.json'):
        value = digest(path)
        if str(path) in hashes and hashes[str(path)] != value:
            raise ValueError('preceding proof source changed: ' + str(path))
        hashes[str(path)] = value

    def verify():
        for name, expected in hashes.items():
            if digest(name) != expected:
                raise ValueError('output-stage provenance changed: ' + name)

    verify()
    pins = select_outputs((timing_origin / 'margin_paths.rpt').read_text(), args.limit)
    if not pins:
        raise ValueError('no loaded weak outputs in supplied critical paths')
    work.mkdir(parents=True, exist_ok=False)
    selection = work / 'output_pins.tcl'
    selection.write_text('set output_pins {' + ' '.join(pins) + '}\n')
    hashes[str(selection)] = digest(selection)
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(), stages=[],
                  source_sha256=hashes, selected_outputs=pins, preceding_repair=str(origin),
                  rounds_limit=args.rounds, outputs_per_round=args.limit,
                  origin_kind='measured_global_diagnostic' if measurement else 'global_repair',
                  timing_qualified=False, physical_qualified=False, full_chip_qualified=False)

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    save()
    try:
        with memory_slot(['receiver-output-repair'], exclusive=True) as owned:
            verify()
            environment = dict(os.environ, REPEATER_WORK=str(work), REPEATER_PDK=str(pdk),
                RECEIVER_SEED=str(seed_db), RECEIVER_SDC=str(constraints),
                RECEIVER_SOURCE=str(HERE), REPEATER_SOURCE=str(ROOT / 'pdk/1G_repeater_IHPSG13G2'),
                RECEIVER_PYTHON=str(pdk / '.cpphdl/venv/bin/python'),
                RECEIVER_OUTPUT_ROUNDS=str(args.rounds), RECEIVER_OUTPUT_LIMIT=str(args.limit),
                OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            report['stage'] = 'global'
            save()
            started = time.monotonic()
            with (work / 'global.log').open('w') as log:
                result = execute(['prlimit', '--as=4294967296', '--core=0', '--',
                    tools / 'bin/openroad', '-no_init', '-exit', HERE / 'receiver_output_stage.tcl'],
                    env=environment, stdout=log, stderr=-2, timeout=3600)
            report['stages'].append(dict(name='global', returncode=result.returncode,
                                         seconds=time.monotonic() - started))
            if result.returncode:
                raise ValueError('native output repair failed; see global.log')
            for kind, mode in (('setup', 'max'), ('hold', 'min')):
                if abs(slack(work / ('before_' + kind + '.rpt'), mode) -
                       slack(timing_origin / ('margin_' + kind + '.rpt'), mode)) > .001:
                    raise ValueError('reloaded enlarged-C timing differs by more than 1 ps')
            report['seed_connectivity'] = buffer_compare(seed_verilog, work / 'before.v')
            if (report['seed_connectivity']['removed_buffers'] or
                    report['seed_connectivity']['removed_fillers']):
                raise ValueError('output stage changed the input checkpoint')
            # The direct global path performs no filler operation: the exact
            # reload comparison above proves that fillers were preserved.
            report['filler_connectivity'] = (dict(report['seed_connectivity'],
                operation='preserved; no filler insertion or removal') if measurement
                else proof['filler_connectivity'])
            report['buffer_connectivity'] = rebuffer_compare(work / 'before.v', work / 'repaired.v', models)
            gc.collect()
            report['placement_connectivity'] = buffer_compare(work / 'repaired.v', work / 'global.v')
            if report['placement_connectivity']['removed_buffers']:
                raise ValueError('output stage changed topology after proof')
            report['global_electrical_pass'] = 'VIOLATED' not in (work / 'global_electrical.rpt').read_text().upper()
            report['global_estimates'] = dict(setup_ns=slack(work / 'global_setup.rpt', 'max'),
                                             hold_ns=slack(work / 'global_hold.rpt', 'min'))
            verify()
            report['margin_screen'] = final_screen(work)
            report.update(status='global_repair_candidate', stage='complete')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['artifact_sha256'] = {str(p): digest(p) for p in work.iterdir()
            if p.is_file() and p.name not in ('checks.json', 'checks.tmp')}
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('origin', 'constraints', 'models', 'output'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    parser.add_argument('--limit', type=int, default=128)
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--margin-measurement', type=Path,
                        help='independent RC measurement of an unbuffer diagnostic origin')
    parser.add_argument('--select-only', type=Path, help='emit selected pins from a fresh path report')
    parser.add_argument('--selection', type=Path, help='Tcl selection destination')
    args = parser.parse_args()
    if args.select_only:
        if not args.selection:
            parser.error('--select-only requires --selection')
        pins = select_outputs(args.select_only.read_text(), args.limit)
        args.selection.write_text('set output_pins {' + ' '.join(pins) + '}\n')
    else:
        if any(getattr(args, name) is None for name in ('origin', 'constraints', 'models', 'output')):
            parser.error('--origin, --constraints, --models and --output are required')
        run(args)
