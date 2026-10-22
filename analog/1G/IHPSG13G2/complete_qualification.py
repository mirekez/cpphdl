#!/usr/bin/env python3
"""Recheck a retained module after its missing DRC group completes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

from physical_module import ANALOG, changed_inputs, check_drc, check_lvs, check_timing

SOURCE = Path(__file__).resolve()


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify(physical_path, completion_path, pdk):
    physical_path, completion_path = physical_path.resolve(), completion_path.resolve()
    original = json.loads(physical_path.read_text())
    completion = json.loads(completion_path.read_text())
    work, top = physical_path.parent, original['top']
    errors = original.get('errors', [])
    if (original.get('status') != 'failed' or not errors
            or any(not error.startswith('drc: ') for error in errors)):
        raise ValueError('only DRC-stage failures can be completed by this receipt')
    stages = {stage['name']: stage for stage in original['stages']}
    if (not original.get('native_postroute_exact')
            or not {'fill', 'postroute-vectors', 'lvs'} <= stages.keys()
            or any(stage.get('returncode') != 0 for name, stage in stages.items() if name != 'drc')
            or not original.get('input_sha256') or not original.get('lvs')):
        raise ValueError('missing independent functional, fill or LVS evidence')
    if completion.get('status') != 'pass' or not completion.get('input_sha256') or not completion.get('artifact_sha256'):
        raise ValueError('missing completed DRC provenance')
    if changed_inputs(original['input_sha256'], original.get('retained_input_paths')):
        raise ValueError('consumed module inputs changed')
    if changed_inputs(completion['input_sha256']) or changed_inputs(completion['artifact_sha256']):
        raise ValueError('completed DRC inputs or artifacts changed')
    if completion['input_sha256'].get(str(physical_path)) != digest(physical_path):
        raise ValueError('DRC completion belongs to another physical report')
    gds = work / 'module_filled.gds'
    gds_hash = original['artifact_sha256'].get(gds.name)
    if not gds_hash or digest(gds) != gds_hash or completion['input_sha256'].get(str(gds)) != gds_hash:
        raise ValueError('DRC completion and LVS used different GDS data')
    command = completion.get('command', [])
    if f'input={gds}' not in command or f'topcell={top}' not in command:
        raise ValueError('DRC completion command has a different layout or top')
    for key in ('precheck_drc', 'disable_extra_rules', 'no_feol', 'no_beol', 'no_offgrid',
                'no_angle', 'no_forbidden', 'no_pin', 'no_recommended'):
        if f'{key}=false' not in command:
            raise ValueError('DRC completion disabled required checks: ' + key)
    artifacts = {str(work / name): value for name, value in original['artifact_sha256'].items()}
    if changed_inputs(artifacts):
        raise ValueError('retained physical module artifacts changed')
    if digest(work / 'cpp_trace.csv') != digest(work / 'postroute_trace.csv'):
        raise ValueError('routed functional trajectory differs from the native oracle')
    mapped = json.loads((work / 'mapped.json').read_text())['modules'][top]
    ports = {'VDD', 'VSS'}
    for name, port in mapped['ports'].items():
        width = len(port['bits'])
        ports.update([name] if width == 1 else [f'{name}[{i}]' for i in range(width)])
    rule_directory = pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/tech/drc'
    drc_path = Path(completion['drc']['report'])
    lvs_path = Path(original['lvs']['report'])
    evidence_paths = [physical_path, completion_path, lvs_path, work / 'drc.log', work / 'setup.rpt', work / 'hold.rpt',
                      work / 'electrical.rpt', work / 'cpp_trace.csv', work / 'postroute_trace.csv', SOURCE]
    before = {str(path): digest(path) for path in evidence_paths}
    if 'Completed running Antenna checks.' not in (work / 'drc.log').read_text():
        raise ValueError('antenna checking did not complete')
    inputs = {}
    for group in (original['input_sha256'], completion['input_sha256'], completion['artifact_sha256'], before):
        for name, expected in group.items():
            if name in inputs and inputs[name] != expected:
                raise ValueError('conflicting qualification provenance: ' + name)
            inputs[name] = expected
    drc = check_drc(drc_path, rule_directory)
    lvs = check_lvs(lvs_path, top, ports)
    timing = check_timing(work)
    if (changed_inputs(before) or changed_inputs(original['input_sha256'], original.get('retained_input_paths'))
            or changed_inputs(completion['input_sha256']) or changed_inputs(completion['artifact_sha256'])):
        raise ValueError('qualification inputs changed during rechecking')
    return dict(status='component_checks_pass', top=top, full_1000base_t_chip=False,
                scope='one routed module; post-fill geometry/connectivity and nominal routed-wire timing; fill coupling not extracted',
                physical_artifact_root=str(work), original_drc_failure=errors,
                original_physical_report=str(physical_path), drc_completion_report=str(completion_path),
                input_sha256=inputs,
                retained_input_paths=original.get('retained_input_paths', {}), artifact_sha256=artifacts,
                native_postroute_exact=True, timing=timing, drc=drc, lvs=lvs,
                source_gds=str(gds), gds_sha256=gds_hash)


def run(args):
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='qualified-', dir=root))
    result = dict(status='running', work=str(work))
    try:
        result = verify(args.physical_report, args.drc_completion_report, args.pdk)
        source = Path(result['source_gds'])
        pending = work / 'pending.gds'
        shutil.copyfile(source, pending)
        if digest(pending) != result['gds_sha256'] or digest(source) != result['gds_sha256']:
            raise ValueError('qualified GDS changed during publication')
        destination = work / (result['top'] + '.gds')
        pending.rename(destination)
        result.update(gds=str(destination), work=str(work))
        print('PASS component checks:', destination, flush=True)
    except BaseException as error:
        result.update(status='failed', error=str(error))
        raise
    finally:
        text = json.dumps(result, indent=2) + '\n'
        (work / 'qualification.json').write_text(text)
        (root / 'checks.json').write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--physical-report', type=Path, required=True)
    parser.add_argument('--drc-completion-report', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ANALOG / 'pdk/IHPSG13G2')
    parser.add_argument('--output', type=Path, required=True)
    run(parser.parse_args())
