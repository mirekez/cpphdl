#!/usr/bin/env python3
"""Extract real routed metal and reject clock/control opens or net shorts."""
import argparse
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / 'out/pdf-renderer'))
import klayout.db as db
from integrate_clock_control import LAYERS, TOP, connections, physical_name
from package_development_layout import digest

VIAS = [(8, 19, 10), (10, 29, 30), (30, 49, 50), (50, 66, 67),
        (67, 125, 126), (126, 133, 134)]


def extract(path):
    layout = db.Layout()
    layout.read(str(path))
    top = layout.cell(TOP)
    if top is None:
        raise ValueError('missing routed clock system')
    extractor = db.LayoutToNetlist(TOP, layout.dbu)
    regions = {}
    for number in sorted(set(LAYERS) | {v for _, v, _ in VIAS}):
        region = db.Region(top.begin_shapes_rec(layout.layer(number, 0)))
        extractor.register(region, 'layer_' + str(number))
        regions[number] = region
        extractor.connect(region)
    for lower, via, upper in VIAS:
        extractor.connect(regions[lower], regions[via])
        extractor.connect(regions[via], regions[upper])
    extractor.extract_netlist()
    result = {}
    reverse = {b: a for a, b in connections()}
    found = set()
    for inst in top.each_inst():
        if inst.cell.name not in ('ClockDeliveryMacro', 'ClockControlMacro'):
            continue
        found.add(inst.cell.name)
        children = list(inst.cell.each_inst())
        if len(children) != 1:
            raise ValueError('unexpected normalized macro hierarchy')
        child = children[0]
        transform = inst.trans * child.trans
        for number in LAYERS:
            for shape in child.cell.shapes(layout.layer(number, 25)).each():
                if not shape.is_text():
                    continue
                pin = shape.text.string
                position = shape.text.trans.disp
                point = transform * db.Point(position.x, position.y)
                net = extractor.probe_net(regions[number], point)
                if net is None:
                    raise ValueError('unconnected physical terminal: ' + pin)
                expected = pin if inst.cell.name == 'ClockDeliveryMacro' else reverse.get(pin, pin)
                result[inst.cell.name + '/' + pin] = dict(expected=expected,
                    cluster=net.cluster_id, layer=number, point_dbu=[point.x, point.y])
    if found != {'ClockDeliveryMacro', 'ClockControlMacro'} or len(result) != 62:
        raise ValueError('missing or duplicate physical macro ports')
    expected_clusters = {}
    cluster_names = {}
    for pin, record in result.items():
        name, cluster = record['expected'], record['cluster']
        if name in expected_clusters and expected_clusters[name] != cluster:
            raise ValueError('open inter-macro net: ' + name)
        if cluster in cluster_names and cluster_names[cluster] != name:
            raise ValueError('shorted inter-macro nets: ' + name + '/' + cluster_names[cluster])
        expected_clusters[name] = cluster
        cluster_names[cluster] = name
    return dict(status='metal_connectivity_pass', inter_macro_connections=len(connections()),
                distinct_interface_nets=len(expected_clusters), terminals=result,
                transistor_lvs_qualified=False, full_chip_qualified=False)


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    report = dict(status='running', full_chip_qualified=False)
    try:
        report['source_sha256'] = {str(p.resolve()):digest(p) for p in
                                  (args.gds, Path(__file__), HERE / 'integrate_clock_control.py')}
        report['positive'] = extract(args.gds)
        layout = db.Layout()
        layout.read(str(args.gds))
        top = layout.cell(TOP)
        # Cut the actual channel, independent of which macro faces north.
        boxes = sorted([inst.bbox() for inst in top.each_inst()
            if inst.cell.name in ('ClockDeliveryMacro', 'ClockControlMacro')], key=lambda b: b.bottom)
        if len(boxes) != 2 or boxes[1].bottom - boxes[0].top < 2000:
            raise ValueError('negative control requires a clear inter-macro channel')
        y = (boxes[0].top + boxes[1].bottom) // 2
        extent = top.bbox()
        cut = db.Region(db.Box(extent.left - 1, y - 500, extent.right + 1, y + 500))
        for inst in top.each_inst():
            if inst.cell.name in ('ClockDeliveryMacro', 'ClockControlMacro') and inst.bbox().overlaps(cut.bbox()):
                raise ValueError('negative-control cut overlaps a macro')
        removed = 0
        for number in LAYERS:
            layer = layout.layer(number, 0)
            original = db.Region(top.shapes(layer))
            removed += (original & cut).area()
            remaining = original - cut
            top.shapes(layer).clear()
            top.shapes(layer).insert(remaining)
        if not removed:
            raise ValueError('negative control removed no routed metal')
        opened = args.work / 'opened.gds'
        layout.write(str(opened))
        try:
            extract(opened)
        except ValueError as error:
            if 'open inter-macro net' not in str(error):
                raise
            report['open_control'] = str(error)
        else:
            raise ValueError('opened inter-macro routing incorrectly passed')
        report.update(status='pass', opened_routing_rejected=True)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (args.work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: 21 real inter-macro nets; opened routing rejected')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gds', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    run(parser.parse_args())
