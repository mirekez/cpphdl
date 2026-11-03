#!/usr/bin/env python3
"""Independently reload fresh routed RC before further receiver sizing trials."""
import argparse
import json
import os
from pathlib import Path
import sys

from receiver_path_sizing import HERE, ROOT, digest, script, slack
sys.path.insert(0, str(ROOT / 'pdk/1G_repeater_IHPSG13G2'))
from tool_process import SLOT_OWNER, configure_cleanup, execute, memory_slot


def admit(proof, origin=None):
    """Accept early wires only for diagnostics, explicitly bound to their origin."""
    early = proof.get('status') == 'early_route_diagnostic_complete'
    endpoints = proof.get('connectivity' if early else 'routed_connectivity', {})
    if (proof.get('stage') != 'complete' or endpoints.get('status') != 'pass'
            or endpoints.get('top') != 'PCSReceiver'):
        raise ValueError('completed receiver route and connectivity evidence required')
    if not early:
        if origin is not None or proof.get('status') not in ('routed_timing_fail', 'routed_timing_candidate_pass'):
            raise ValueError('completed full route required without a probe origin')
        return {}, 'full_route_diagnostic'
    if origin is None or proof.get('detail_end_iteration') != 1:
        raise ValueError('early-route diagnostic requires its explicit probe origin')
    if any(proof.get(key) is not False for key in ('timing_qualified', 'physical_qualified', 'full_chip_qualified')):
        raise ValueError('early-route receipt must not claim qualification')
    origin = origin.resolve()
    path = origin / 'checks.json'
    if proof['source_sha256'].get(str(path)) != digest(path):
        raise ValueError('probe origin receipt is not bound')
    source = json.loads(path.read_text())
    from receiver_route_probe import screen
    screen(source)
    for name in ('candidate.odb', 'candidate.sdc', 'after.v'):
        path = origin / name
        value = digest(path)
        if (proof['source_sha256'].get(str(path)) != value or
                source['artifact_sha256'].get(str(path)) != value):
            raise ValueError('early route belongs to another candidate')
    # The probe preserves signal endpoints; its origin supplies the exact
    # library models used for same-function sizing and later replay.
    cells = Path(source['cell_models'])
    if source['source_sha256'].get(str(cells)) != digest(cells):
        raise ValueError('unbound probe cell models')
    return source['source_sha256'], 'early_route_diagnostic'


def prepare(args):
    route, work = args.route.resolve(), args.output.resolve()
    proof = json.loads((route / 'checks.json').read_text())
    origin_hashes, route_scope = admit(proof, getattr(args, 'probe_origin', None))
    hashes = dict(proof['source_sha256'])
    for group in (origin_hashes, proof['artifact_sha256']):
        for name, value in group.items():
            if name in hashes and hashes[name] != value:
                raise ValueError('conflicting route provenance')
            hashes[name] = value

    def verify():
        for name, value in hashes.items():
            if digest(name) != value:
                raise ValueError('changed receiver route input: ' + name)

    verify()
    for name in ('core.odb', 'core.spef', 'postroute.v', 'paths.rpt'):
        if proof['artifact_sha256'].get(str(route / name)) != digest(route / name):
            raise ValueError('unbound routed seed: ' + name)
    for p in (args.constraints.resolve(), args.cells.resolve(),
              args.tools.resolve() / 'bin/openroad', args.tools.resolve() / 'root/usr/bin/openroad'):
        if hashes.get(str(p)) != digest(p):
            raise ValueError('seed input differs from routed evidence: ' + str(p))
    for corner in ('slow_1p08V_125C', 'fast_1p32V_m40C'):
        p = args.pdk.resolve() / ('ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_' + corner + '.lib')
        if hashes.get(str(p)) != digest(p):
            raise ValueError('seed timing library differs from routed evidence')
    work.mkdir(parents=True, exist_ok=False)
    for target, source in [('candidate.odb', 'core.odb'), ('after.v', 'postroute.v'),
                           ('after_paths.rpt', 'paths.rpt'), ('core.spef', 'core.spef')]:
        os.link(route / source, work / target)
        hashes[str(work / target)] = digest(work / target)
    os.link(args.cells.resolve(), work / 'cells.v')
    hashes[str(work / 'cells.v')] = digest(work / 'cells.v')
    for p in (Path(__file__).resolve(), HERE / 'receiver_path_sizing.py',
              HERE / 'receiver_route_probe.py', route / 'checks.json'):
        hashes[str(p)] = digest(p)
    report = dict(status='running', source_sha256=hashes, stage='independent_recheck', route_scope=route_scope,
                  timing_qualified=False, physical_qualified=False, full_chip_qualified=False)
    def save():
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    save()
    try:
        with memory_slot(['receiver-routed-seed'], exclusive=True) as owned:
            verify()
            recheck = work / 'recheck'
            recheck.mkdir()
            source = recheck / 'run.tcl'
            source.write_text(script())
            environment = dict(os.environ, REPEATER_WORK=str(recheck),
                RECEIVER_SEED=str(work / 'candidate.odb'), RECEIVER_SPEF=str(work / 'core.spef'),
                RECEIVER_SDC=str(args.constraints.resolve()), REPEATER_PDK=str(args.pdk.resolve()),
                OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2')
            if owned:
                environment[SLOT_OWNER] = str(os.getpid())
            with (recheck / 'run.log').open('w') as log:
                result = execute(['prlimit', '--as=4294967296', '--core=0', '--',
                    args.tools.resolve() / 'bin/openroad', '-no_init', '-exit', source],
                    env=environment, stdout=log, stderr=-2, timeout=600)
            if result.returncode:
                raise ValueError('fresh receiver timing reload failed')
            for kind, mode in (('setup', 'max'), ('hold', 'min')):
                value = slack(recheck / ('before_' + kind + '.rpt'), mode)
                if abs(value - proof[kind + '_ns']) > .001:
                    raise ValueError('fresh receiver timing differs by more than 1 ps')
                report[kind + '_ns'] = value
            verify()
            report.update(status='diagnostic_complete', stage='complete', fresh_process_timing_agrees=True,
                          artifact_sha256={str(p): digest(p) for p in work.rglob('*')
                                           if p.is_file() and p.name != 'checks.json'})
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('route', 'cells', 'constraints', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    parser.add_argument('--probe-origin', type=Path,
                        help='explicit global candidate for an early-route diagnostic; never qualifies routing')
    prepare(parser.parse_args())
