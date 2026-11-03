#!/usr/bin/env python3
"""Common checked handoff for receiver repairs that only change buffers and drive sizes."""
import gc
import json
import os
from pathlib import Path
import time

from buffer_equivalence import compare as buffer_compare
from receiver_final_repair import final_screen
from receiver_path_sizing import HERE, ROOT, digest, slack
from receiver_pin_stage import require_origin
from rebuffer_equivalence import compare as rebuffer_compare
from tool_process import SLOT_OWNER, execute, memory_slot


def run(args, script, sources, extra_environment):
    script = script.resolve()
    origin, work = args.origin.resolve(), args.output.resolve()
    proof = json.loads((origin / 'checks.json').read_text())
    require_origin(proof)
    hashes = dict(proof['source_sha256'])
    for name, value in proof['artifact_sha256'].items():
        if name in hashes and hashes[name] != value:
            raise ValueError('conflicting checkpoint-repair provenance')
        hashes[name] = value
    constraints, models = args.constraints.resolve(), args.models.resolve()
    tools, pdk = args.tools.resolve(), args.pdk.resolve()
    library = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    required = [origin / 'global.odb', origin / 'global.v', origin / 'margin_paths.rpt',
                constraints, models, tools / 'bin/openroad', tools / 'root/usr/bin/openroad',
                library / 'lef/sg13g2_tech.lef']
    required += [library / ('lib/sg13g2_stdcell_' + corner + '.lib')
                 for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C')]
    for path in required:
        if hashes.get(str(path)) != digest(path):
            raise ValueError('checkpoint-repair input differs from producer: ' + str(path))
    for path in (Path(__file__).resolve(), script, *sources, HERE / 'receiver_pin_stage.py',
                 HERE / 'receiver_final_repair.py', HERE / 'receiver_margin_route.py',
                 HERE / 'buffer_equivalence.py', HERE / 'rebuffer_equivalence.py',
                 HERE / 'sizing_equivalence.py', origin / 'checks.json'):
        value = digest(path)
        if str(path) in hashes and hashes[str(path)] != value:
            raise ValueError('preceding proof source changed: ' + str(path))
        hashes[str(path)] = value

    def verify():
        for name, expected in hashes.items():
            if digest(name) != expected:
                raise ValueError('checkpoint-repair provenance changed: ' + name)

    verify()
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='waiting_for_memory_slot', pid=os.getpid(), stages=[],
                  source_sha256=hashes, preceding_repair=str(origin), repair_script=str(script),
                  repair_options=extra_environment,
                  timing_qualified=False, physical_qualified=False, full_chip_qualified=False)

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    save()
    try:
        with memory_slot(['receiver-checkpoint-repair'], exclusive=True) as owned:
            verify()
            environment = dict(os.environ, REPEATER_WORK=str(work), REPEATER_PDK=str(pdk),
                RECEIVER_SEED=str(origin / 'global.odb'), RECEIVER_SDC=str(constraints),
                RECEIVER_SOURCE=str(HERE), REPEATER_SOURCE=str(ROOT / 'pdk/1G_repeater_IHPSG13G2'),
                OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            environment.update(extra_environment)
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            report['stage'] = 'global'
            save()
            started = time.monotonic()
            with (work / 'global.log').open('w') as log:
                result = execute(['prlimit', '--as=4294967296', '--core=0', '--',
                    tools / 'bin/openroad', '-no_init', '-exit', script],
                    env=environment, stdout=log, stderr=-2, timeout=3600)
            report['stages'].append(dict(name='global', returncode=result.returncode,
                                         seconds=time.monotonic() - started))
            if result.returncode:
                raise ValueError('native checkpoint repair failed; see global.log')
            for kind, mode in (('setup', 'max'), ('hold', 'min')):
                if abs(slack(work / ('before_' + kind + '.rpt'), mode) -
                       slack(origin / ('margin_' + kind + '.rpt'), mode)) > .001:
                    raise ValueError('reloaded enlarged-C timing differs by more than 1 ps')
            report['seed_connectivity'] = buffer_compare(origin / 'global.v', work / 'before.v')
            if report['seed_connectivity']['removed_buffers']:
                raise ValueError('checkpoint repair changed the input checkpoint')
            report['filler_connectivity'] = proof['filler_connectivity']
            report['buffer_connectivity'] = rebuffer_compare(work / 'before.v', work / 'repaired.v', models)
            gc.collect()
            report['placement_connectivity'] = buffer_compare(work / 'repaired.v', work / 'global.v')
            if report['placement_connectivity']['removed_buffers']:
                raise ValueError('checkpoint repair changed topology after proof')
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


