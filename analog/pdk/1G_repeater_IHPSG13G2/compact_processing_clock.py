#!/usr/bin/env python3
"""Place oscillator and admission devices together, with connected compact rows."""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import re
import sys

from integrate_processing_clock import PORTS, run as reference_assembly
from package_development_layout import ROOT, digest, hierarchy_digest, read_block

DEVICE = ROOT / '1G/IHPSG13G2'
sys.path.insert(0, str(DEVICE))
from analog_row_layout import create_row
from compact_analog_rows import compact_rows


def run(args):
    args.work.mkdir(parents=True, exist_ok=False)
    baseline = args.work / 'reference'
    reference_assembly(argparse.Namespace(work=baseline, provenance=args.provenance))
    reference = json.loads((baseline / 'integration.json').read_text())
    records = {b['name']: b for b in json.loads(args.provenance.read_text())['blocks']}
    primitive_hashes, source_layouts, collections = {}, [], []
    for kind, name, filename in (('osc', 'processing_oscillator', 'oscillator.cir'),
                                 ('gate', 'processing_gate', 'gate.cir')):
        path = Path(records[name]['source'])
        if not path.is_absolute():
            path = ROOT / path
        proof = json.loads(path.with_name('checks.json').read_text())
        layout, top = read_block(path, records[name]['top'])
        declarations = re.findall(r'^\.SUBCKT (\S+) ', path.with_name(filename).read_text(), re.M)

        def scoped(text):
            for identifier in sorted(declarations, key=len, reverse=True):
                text = re.sub(r'(?<!\w)' + re.escape(identifier) + r'(?!\w)', kind + '_' + identifier, text)
            return text

        components = deepcopy(proof['geometry']['components'])
        for part in components:
            primitive_hashes[kind + '_' + part['name']] = hierarchy_digest(layout.cell(part['name']))
            part['name'] = kind + '_' + part['name']
            part['geometry']['schematic'] = scoped(part['geometry']['schematic'])
            bindings = {'VDD': 'VDD', 'VSS': 'VSS'}
            if kind == 'osc':
                bindings.update({port: port for port in PORTS if port not in ('RAW', 'ADMIT', 'OUT')})
                bindings['OUT'] = 'RAW'
            else:
                bindings.update(CLOCK='RAW', ENABLE='ADMIT', OUT='OUT')
            part['connections'] = {pin: bindings.get(net, kind + '_' + net)
                                   for pin, net in part['connections'].items()}
        for cell in layout.each_cell():
            cell.name = kind + '_' + cell.name
        source_layouts.append(layout)
        collections.append((layout, components))

    def build_parts(layout):
        result = []
        for source, components in collections:
            for part in components:
                cell = layout.create_cell(part['name'])
                cell.copy_tree(source.cell(part['name']))
                result.append((cell, part['geometry'], part['connections'], part['kind']))
        return result

    print('Placing all clock devices in one connected placement domain', flush=True)
    layout, top, info = create_row('ProcessingClockDelivery', PORTS, build_parts,
                                  net_weights={'RAW': 64, 'ADMIT': 8})
    print('Rerouting compact rows using their local net sets', flush=True)
    layout, top, info = compact_rows(layout, top, info, args.width, keep_together_nets=('RAW',))
    output = args.work / 'compact.gds'
    layout.write(str(output))
    # Independent expected topology comes from the original component circuits
    # and the class-level connections, not from the new placer/router's buses.
    (args.work / 'compact.cir').write_bytes((baseline / 'delivery.cir').read_bytes())
    (args.work / 'interface.json').write_text(json.dumps(PORTS, indent=2) + '\n')
    saved, _ = read_block(output, top.name)
    for name, expected in primitive_hashes.items():
        if hierarchy_digest(saved.cell(name)) != expected:
            raise ValueError('compaction modified a primitive: ' + name)
    before, old = read_block(baseline / 'delivery.gds', top.name)
    area_before = old.dbbox().area()
    source_hashes = dict(reference['source_sha256'])
    for path in (Path(__file__), DEVICE / 'compact_analog_rows.py', DEVICE / 'analog_row_layout.py',
                 DEVICE / 'processing_clock_gate_layout.py'):
        source_hashes[str(path)] = digest(path)
    for name, expected in source_hashes.items():
        if digest(Path(name)) != expected:
            raise ValueError('compaction input changed')
    report = dict(status='compact_routed_candidate', top=top.name, width_um=info['width_um'],
        height_um=info['height_um'], area_um2=info['bounding_area_um2'],
        original_connected_area_um2=area_before,
        area_reduction_percent=100 * (1 - info['bounding_area_um2'] / area_before),
        primitive_geometry_preserved=True, primitives=len(primitive_hashes),
        full_chip_qualified=False, lvs_qualified=False, drc_qualified=False, electrical_qualified=False,
        source_sha256=source_hashes, geometry=info,
        artifact_sha256={p.name: digest(p) for p in (output, args.work / 'compact.cir')})
    (args.work / 'compaction.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('status', 'width_um', 'height_um', 'area_reduction_percent', 'primitives')}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--provenance', type=Path, default=ROOT / '1G/IHPSG13G2/results/development_provenance.json')
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--width', type=float, default=600)
    run(parser.parse_args())
