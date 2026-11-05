#!/usr/bin/env python3
"""Physically join the oscillator and admission gate from the published layout.

The topology follows 1G/IHPSG13G2/processing_clock_delivery.hpp. Existing device
geometry is preserved; actual Metal4/Metal5 wires and Via4 cuts join VDD, VSS
and RAW. Controller, pads and receiver loads remain external to this macro.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import sys

import klayout.db as db

from package_development_layout import ROOT, digest, hierarchy_digest, read_block

PORTS = ['VDD', 'VSS', 'CLEAR', 'RAW', 'ADMIT', 'OUT'] + [
    f'{kind}_N{bit}' for bit in range(8) for kind in ('COARSE', 'FINE')]


def terminals(layout, cell):
    result = {}
    layer = layout.find_layer(67, 25)
    if layer is None:
        raise ValueError('missing Metal5 terminal labels')
    for shape in cell.shapes(layer).each():
        if shape.is_text():
            name = shape.text.string
            if name in result:
                raise ValueError('ambiguous physical terminal: ' + name)
            displacement = shape.text.trans.disp
            # A KLayout Vector ignores translation when transformed. Ports are
            # positions, so represent them as Points before placing a macro.
            result[name] = db.Point(displacement.x, displacement.y)
    for name, point in result.items():
        area = db.Region(cell.begin_shapes_rec_overlapping(layout.layer(67, 0), db.Box(point, point).enlarged(1)))
        if (area & db.Region(db.Box(point, point).enlarged(1))).is_empty():
            raise ValueError('terminal label has no metal: ' + name)
    return result


def namespace_circuit(text, prefix, top):
    names = re.findall(r'^\.SUBCKT (\S+) ', text, re.M | re.I)
    if len(set(names)) != len(names) or top not in names:
        raise ValueError('ambiguous source circuit')
    # The PDK extracts one continuous chip substrate. Standalone macros keep
    # PSUB internal; composition must explicitly share it, without inventing
    # a metal short to VSS or changing any transistor/bulk parameters.
    root = re.search(r'^\.SUBCKT ' + re.escape(top) + r' ([^\n]+)\n(.*?)^\.ENDS', text, re.M | re.S)
    if root is None or 'PSUB' in root[1].split() or not re.search(r'\bPSUB\b', root[2]):
        raise ValueError('expected an internal process substrate')
    text = text[:root.start(1)] + root[1] + ' PSUB' + text[root.end(1):]
    for name in sorted(names, key=len, reverse=True):
        text = re.sub(r'(?<!\w)' + re.escape(name) + r'(?!\w)', prefix + name, text)
    return text


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    report = json.loads(args.provenance.read_text())
    records = {b['name']: b for b in report['blocks']}
    layout = db.Layout()
    layout.dbu = .001
    top = layout.create_cell('ProcessingClockDelivery')
    info, cells, pins, circuits = {}, {}, {}, []
    hashes = {str(args.provenance.resolve()): digest(args.provenance), str(Path(__file__)): digest(Path(__file__)),
              str(ROOT / '1G/IHPSG13G2/processing_clock_delivery.hpp'): digest(ROOT / '1G/IHPSG13G2/processing_clock_delivery.hpp')}
    for kind, name, filename in (('osc', 'processing_oscillator', 'oscillator.cir'),
                                 ('gate', 'processing_gate', 'gate.cir')):
        block = records[name]
        source = Path(block['source'])
        if not source.is_absolute():
            source = ROOT / source
        if digest(source) != block['source_sha256']:
            raise ValueError('source geometry changed: ' + name)
        circuit = source.with_name(filename)
        proof_path = source.with_name('checks.json')
        proof = json.loads(proof_path.read_text())
        recorded = proof.get('artifact_sha256', {})
        for path in (source, circuit):
            candidates = [value for key, value in recorded.items() if Path(key).name == path.name]
            if candidates != [digest(path)]:
                raise ValueError('source schematic/geometry not bound to prior proof: ' + str(path))
            hashes[str(path)] = digest(path)
        hashes[str(proof_path)] = digest(proof_path)
        original, master = read_block(source, block['top'])
        geometry = hierarchy_digest(master)
        if geometry != block['geometry_sha256']:
            raise ValueError('published and original block geometry differ')
        pins[kind] = terminals(original, master)
        for cell in original.each_cell():
            cell.name = kind + '_' + cell.name
        child = layout.create_cell(master.name)
        child.copy_tree(master)
        cells[kind] = child
        info[kind] = dict(cell=child.name, geometry_sha256=geometry)
        circuits.append(namespace_circuit(circuit.read_text(), kind + '_', block['top']))
        original.destroy()
    # Face the gate toward the oscillator, aligning RAW's physical terminals.
    # A 20 um gap separates footprints; supplies leave the left edge and clock
    # traverses the gap on Metal4, crossing Metal5 buses without shorting them.
    separation = 20000
    clock_a, clock_b = pins['osc']['OUT'], pins['gate']['CLOCK']
    transform = db.Trans(2, False, clock_a.x + clock_b.x,
                         cells['osc'].bbox().bottom + cells['gate'].bbox().bottom - separation)
    top.insert(db.CellInstArray(cells['osc'].cell_index(), db.Trans()))
    top.insert(db.CellInstArray(cells['gate'].cell_index(), transform))
    gate_pins = {name: transform * point for name, point in pins['gate'].items()}
    routes = []

    def wire(layer, points, width=400):
        top.shapes(layout.layer(layer, 0)).insert(db.Path(points, width, width // 2, width // 2))

    def via(point):
        top.shapes(layout.layer(66, 0)).insert(db.Box(point.x - 95, point.y - 95, point.x + 95, point.y + 95))
        for layer in (50, 67):
            top.shapes(layout.layer(layer, 0)).insert(db.Box(point.x - 200, point.y - 200, point.x + 200, point.y + 200))

    end = gate_pins['CLOCK']
    if end.x != clock_a.x or end.y >= clock_a.y:
        raise ValueError('clock terminals are not aligned across the macro gap')
    wire(50, [clock_a, end])
    via(clock_a)
    via(end)
    routes.append(dict(net='RAW', layer=50, points_dbu=[[p.x, p.y] for p in (clock_a, end)]))
    left = min(cells['osc'].bbox().left, cells['gate'].bbox().transformed(transform).left)
    for index, net in enumerate(('VDD', 'VSS')):
        a, b = pins['osc'][net], gate_pins[net]
        x = left - 5000 - index * 2000
        aa, bb = db.Point(x, a.y), db.Point(x, b.y)
        wire(67, [a, aa])
        wire(67, [b, bb])
        wire(50, [aa, bb])
        via(aa)
        via(bb)
        routes.append(dict(net=net, layers=[67, 50, 67], points_dbu=[[p.x, p.y] for p in (a, aa, bb, b)]))
    ports = {p: pins['osc'][p] for p in pins['osc'] if p != 'OUT'}
    ports.update(RAW=clock_a, ADMIT=gate_pins['ENABLE'], OUT=gate_pins['OUT'])
    if set(ports) != set(PORTS):
        raise ValueError('physical delivery interface differs from C++ class')
    for name, point in ports.items():
        top.shapes(layout.layer(67, 25)).insert(db.Text(name, db.Trans(point)))
        top.shapes(layout.layer(67, 2)).insert(db.Box(point.x - 160, point.y - 160, point.x + 160, point.y + 160))
    oscillator_order = ['VDD', 'VSS', 'CLEAR', 'RAW'] + PORTS[6:]
    circuits += ['.SUBCKT ProcessingClockDelivery ' + ' '.join(PORTS),
                 'Xosc ' + ' '.join(oscillator_order) + ' PSUB osc_ProcessingClockOscillator',
                 'Xgate VDD VSS RAW ADMIT OUT PSUB gate_ProcessingClockGate', '.ENDS ProcessingClockDelivery', '']
    gds = args.work / 'delivery.gds'
    layout.write(str(gds))
    (args.work / 'delivery.cir').write_text('\n'.join(circuits))
    (args.work / 'interface.json').write_text(json.dumps(PORTS, indent=2) + '\n')
    # Remove the inter-macro conductor, preserving both complete source
    # macros and all endpoint metal/vias. Existing source vias can coincide
    # with landings, so deleting just a new via would not reliably open RAW.
    found = [s for s in top.shapes(layout.layer(50, 0)).each()
             if s.is_path() and list(s.path.each_point()) == [clock_a, end]]
    if len(found) != 1:
        raise ValueError('ambiguous inter-macro clock conductor')
    found[0].delete()
    layout.write(str(args.work / 'delivery_open.gds'))
    saved, saved_top = read_block(gds, top.name)
    for kind, entry in info.items():
        if hierarchy_digest(saved.cell(entry['cell'])) != entry['geometry_sha256']:
            raise ValueError('integration changed component geometry')
    if any(digest(Path(path)) != value for path, value in hashes.items()):
        raise ValueError('integration inputs changed')
    result = dict(status='routed_candidate', generated_utc=datetime.now(timezone.utc).isoformat(),
        top=top.name, ports=PORTS, components=info, connections=routes,
        raw_route_um=abs(end.y - clock_a.y) * layout.dbu,
        source_sha256=hashes, component_geometry_preserved=True,
        full_chip_qualified=False, electrical_qualified=False, drc_qualified=False, lvs_qualified=False,
        scope='Two physically connected clock macros; controller, pad and receiver integration remain external.',
        artifact_sha256={p.name: digest(p) for p in (gds, args.work / 'delivery.cir', args.work / 'delivery_open.gds')})
    (args.work / 'integration.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(status=result['status'], raw_route_um=result['raw_route_um'], work=str(args.work))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--provenance', type=Path, default=ROOT / '1G/IHPSG13G2/results/development_provenance.json')
    parser.add_argument('--work', type=Path, default=ROOT / 'build/clock-integration/physical-delivery')
    run(parser.parse_args())
