#!/usr/bin/env python3
"""Revalidate completed placement output after a later export/reporting failure."""
import argparse
import json
import os
from pathlib import Path

from tool_process import configure_cleanup, execute
from unified_synthesis import digest


def run(args):
    proof = args.proof.resolve()
    work = proof.parent
    original = json.loads(proof.read_text())
    if original.get('status') != 'fail':
        raise ValueError('a stopped failed run is required')
    hashes = dict(original['source_sha256'])
    if any(digest(p) != h for p, h in hashes.items()):
        raise ValueError('inputs changed before recovery')
    artifacts = {str(work / n): digest(work / n) for n in
                 ('placed.odb', 'placed.def', 'placed.v', 'constraints.sdc', 'route_tech.lef')}
    for name, suffix in [('placed.v', b'endmodule'), ('placed.def', b'END DESIGN')]:
        with (work / name).open('rb') as stream:
            stream.seek(max(0, stream.seek(0, 2) - 256))
            if not stream.read().rstrip().endswith(suffix):
                raise ValueError('incomplete export: ' + name)
    source = Path(__file__).resolve().parent
    executable = args.tools.resolve() / 'bin/openroad'
    for path in (Path(__file__), source / 'unified_placement_check.tcl', source / 'tool_process.py',
                 source / 'unified_synthesis.py', executable, proof):
        hashes[str(path)] = digest(path)
    target = work / 'placement-checks.json'
    if target.exists():
        raise ValueError('preserve the existing placement receipt')
    with (work / 'placement-validation.log').open('w') as log:
        execute(['prlimit', '--as=' + str(args.memory_mib * 1024 * 1024), '--', str(executable),
                 '-no_init', '-exit', str(source / 'unified_placement_check.tcl')], cwd=work,
                env=dict(os.environ, UNIFIED_WORK=str(work)), stdout=log, stderr=-2,
                check=True, timeout=300, memory_exclusive=True)
    if any(digest(p) != h for p, h in {**hashes, **artifacts}.items()):
        raise ValueError('checkpoint changed during validation')
    artifacts[str(work / 'inventory.json')] = digest(work / 'inventory.json')
    artifacts[str(work / 'placement-validation.log')] = digest(work / 'placement-validation.log')
    report = dict(status='placed_unrouted', full_chip_qualified=False, timing_analysis='not_run',
                  scope='Saved database independently checked for legal common placement after a later failure.',
                  inventory=json.loads((work / 'inventory.json').read_text()),
                  source_sha256=hashes, artifact_sha256=artifacts)
    target.write_text(json.dumps(report, indent=2) + '\n')
    print(str(target))


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    parser.add_argument('--memory-mib', type=int, default=3072)
    run(parser.parse_args())
