#!/usr/bin/env python3
"""Preserve executed repair recipes before improving them for the next run.

The saved geometry and design inputs remain checked. Only these orchestration
recipes move to immutable, content-addressed copies; RTL/PDK stay live inputs.
"""
import argparse
import json
from pathlib import Path

from unified_synthesis import digest

RECIPES = {'unified_net_repair.py', 'unified_net_repair.tcl',
           'unified_recover_placement.py', 'unified_placement_check.tcl'}


def archive(proof):
    proof = Path(proof).resolve()
    report = json.loads(proof.read_text())
    if report.get('status') != 'placed_unrouted':
        raise ValueError('validated placement required')
    hashes = {**report['source_sha256'], **report['artifact_sha256']}
    if any(digest(p) != h for p, h in hashes.items()):
        raise ValueError('inputs changed before recipe archival')
    source = Path(__file__).resolve().parent
    target = proof.parent / 'sealed-placement-checks.json'
    if target.exists():
        raise ValueError('preserve the existing sealed receipt')
    recipes = {}
    for name, value in list(report['source_sha256'].items()):
        path = Path(name)
        if path.parent.resolve() != source or path.name not in RECIPES:
            continue
        snapshot = proof.parent / 'recipes' / value / path.name
        snapshot.parent.mkdir(parents=True, exist_ok=True)
        if snapshot.exists() and digest(snapshot) != value:
            raise ValueError('recipe archive collision')
        snapshot.write_bytes(path.read_bytes())
        report['source_sha256'].pop(name)
        report['source_sha256'][str(snapshot)] = value
        recipes[name] = dict(sha256=value, snapshot=str(snapshot))
    if not recipes:
        raise ValueError('no repair recipes to archive')
    if any(digest(p) != h for p, h in hashes.items()):
        raise ValueError('inputs changed while archiving')
    report['executed_recipes'] = recipes
    report['source_sha256'][str(proof)] = digest(proof)
    target.write_text(json.dumps(report, indent=2) + '\n')
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof', type=Path, required=True)
    print(archive(parser.parse_args().proof))
