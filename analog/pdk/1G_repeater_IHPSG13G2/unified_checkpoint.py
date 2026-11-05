#!/usr/bin/env python3
"""Seal a saved floorplan after a placement run stops, before changing its recipe."""
import argparse
import json
from pathlib import Path

from unified_synthesis import digest


def seal(proof):
    proof = Path(proof).resolve()
    report = json.loads(proof.read_text())
    if report.get('status') not in ('fail', 'placed_unrouted'):
        raise ValueError('placement must have stopped before sealing its floorplan')
    if any(digest(p) != value for p, value in report['source_sha256'].items()):
        raise ValueError('placement inputs changed before checkpoint sealing')
    work = proof.parent
    inventory = json.loads((work / 'inventory.json').read_text())
    if (inventory.get('sram_macros') != 8 or inventory.get('fixed_logic_modules') != 0
            or inventory.get('movable_standard_cells', 0) <= 0):
        raise ValueError('incomplete physical inventory')
    # The saved database remains reusable when the placement recipe changes.
    # Design, constraints, libraries and database hashes must still match.
    recipe = {p: value for p, value in report['source_sha256'].items()
              if Path(p).name in ('unified_place.py', 'unified_place.tcl')}
    report.update(status='floorplanned_unplaced', recipe_sha256=recipe,
                  source_sha256={p: value for p, value in report['source_sha256'].items()
                                 if p not in recipe}, inventory=inventory,
                  artifact_sha256={str(work / name): digest(work / name) for name in
                                   ('floorplan.odb', 'inventory.json', 'constraints.sdc', 'route_tech.lef')})
    report.pop('error', None)
    target = work / 'floorplan-checks.json'
    if target.exists():
        raise ValueError('floorplan receipt already exists; preserve it')
    target.write_text(json.dumps(report, indent=2) + '\n')
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof', type=Path, required=True)
    print(seal(parser.parse_args().proof))
