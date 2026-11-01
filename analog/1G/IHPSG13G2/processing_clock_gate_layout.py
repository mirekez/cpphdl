"""Connect the exported clock-admission IC using actual SG13G2 devices."""
from collections import Counter, defaultdict
import math
from pathlib import Path
import re
import random

import klayout.db as db

from capacitor_layout import append_mim
from capacitor_series_layout import append_mim_series
from core_driver_process import contributions
from mos_layout import append_mos
from processing_clock_gate_process import BINDINGS


def placement_order(parts, dbu):
    """Reduce actual signal span before routing; supply rails span the row.

    Capacitors participate in placement beside the devices they load. The
    external clock/enable routes have higher weight because their capacitance
    consumes the upstream clock driver's already characterized load budget.
    Geometry and electrical qualification remain the acceptance criteria.
    """
    widths = [cell.bbox().width() * dbu + 10 for cell, _, _, _ in parts]
    nets = defaultdict(list)
    for index, (_, _, connections, _) in enumerate(parts):
        for net in set(connections.values()) - {'VDD', 'VSS'}:
            nets[net].append(index)

    def cost(order):
        locations, x = [0.] * len(order), 0.
        for index in order:
            locations[index] = x + widths[index] / 2
            x += widths[index]
        return sum((8 if net in ('CLOCK', 'ENABLE') else 2 if net == 'OUT' else 1)
                   * (max(locations[i] for i in indices) - min(locations[i] for i in indices))
                   for net, indices in nets.items())

    rng = random.Random(1302)
    order = list(range(len(parts)))
    initial = score = best_score = cost(order)
    best = list(order)
    for iteration in range(24000):
        a, b = sorted(rng.sample(range(len(order)), 2))
        trial = list(order)
        if iteration % 3:
            trial[a], trial[b] = trial[b], trial[a]
        else:
            trial[a:b + 1] = reversed(trial[a:b + 1])
        proposed = cost(trial)
        temperature = 100 * (1 - iteration / 24000) ** 3 + .01
        if proposed < score or rng.random() < math.exp(min(0, (score - proposed) / temperature)):
            order, score = trial, proposed
            if score < best_score:
                best, best_score = list(order), score
    return best, dict(initial_weighted_span_um=initial, placed_weighted_span_um=best_score,
                      component_order=[parts[index][0].name for index in best])


def circuit(source):
    """Preserve every exported branch; only sum parallel capacitors."""
    text = Path(source).read_text()
    ports = ['Vdd', 'Vss', 'CLOCK', 'ENABLE', 'OUT']
    if 'module OneGIHPProcessingClockGate(' + ', '.join(ports) + ');' not in text:
        raise ValueError('expected the exported processing-clock admission IC')
    names = ['Vss', 'Vdd', 'CLOCK', 'ENABLE', 'OUT'] + sorted(set(re.findall(r'\bcpphdl_n\d+\b', text)))
    mapping = {name: 'n' + str(i) for i, name in enumerate(names)}
    text = re.sub(r'\b(?:' + '|'.join(names) + r')\b', lambda m: mapping[m[0]], text)
    capacitor = re.compile(r'^\s*I\((n\d+), (n\d+)\) <\+ ([-+0-9.eE]+) \* '
                           r'ddt\(V\((n\d+), (n\d+)\)\); // Bench initial voltage = 0.0 V\.$', re.M)
    capacitors = defaultdict(float)
    degenerate = []
    for match in capacitor.finditer(text):
        a, b, value, va, vb = match.groups()
        if (a, b) != (va, vb) or not math.isfinite(float(value)) or float(value) <= 0:
            raise ValueError('invalid storage law')
        if a == b:
            degenerate.append(dict(node=a, capacitance_F=float(value)))
        else:
            capacitors[a, b] += float(value)
    devices, roles = contributions(capacitor.sub('', text), BINDINGS)
    if roles != Counter({row[0]: row[-1] for row in BINDINGS}) or len(devices) != 38:
        raise ValueError('changed clock-gate physical transistor inventory')
    if len(degenerate) != 1 or len(capacitors) != 19:
        raise ValueError('changed clock-gate storage inventory')
    rename = {'0': 'VSS', 'n0': 'VSS', 'n1': 'VDD', 'n2': 'CLOCK', 'n3': 'ENABLE', 'n4': 'OUT'}
    def net(name):
        return rename.get(name, name.upper())
    for device in devices:
        for key in ('a', 'b', 'gate', 'body'):
            device[key] = net(device[key])
    return devices, [(net(a), net(b), value) for (a, b), value in sorted(capacitors.items())], degenerate


def create_gate(pdk, source):
    devices, capacitors, degenerate = circuit(source)
    layout = db.Layout()
    layout.dbu = .001
    top = layout.create_cell('ProcessingClockGate')
    components, definitions, instances, pins = [], [], [], defaultdict(list)
    escapes = []
    cursor = 0.

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    def via(lower, upper, cut, x, y):
        for layer in (lower, upper):
            box(layer, x - .2, y - .2, x + .2, y + .2)
        box(cut, x - .095, y - .095, x + .095, y + .095)

    def place(cell, info, connections, kind):
        nonlocal cursor
        bounds = cell.bbox()
        dx = cursor + 5 - bounds.left * layout.dbu
        dy = 4 - bounds.bottom * layout.dbu
        top.insert(db.CellInstArray(cell.cell_index(), db.Trans(round(dx / layout.dbu), round(dy / layout.dbu))))
        terminals = info.get('terminals', {})
        if kind == 'mos':
            terminals = {name: dict(layer=10, x_um=p[0], y_um=p[1])
                         for name, p in info['terminals_um'].items()}
        for index, (name, net) in enumerate(connections.items()):
            pin = terminals[name]
            x, y = pin['x_um'] + dx, pin['y_um'] + dy
            if kind == 'mos':
                # Distinct horizontal Metal3 escapes reach dedicated Metal4
                # columns in this device's gutter. No route crosses a peer
                # device's escape columns on the same conductor layer.
                escape = cursor + 1 + index * .8
                via(10, 30, 29, x, y)
                box(30, min(escape, x) - .2, y - .2, max(escape, x) + .2, y + .2)
                via(30, 50, 49, escape, y)
            elif pin['layer'] == 67:
                # The external bottom plate of a series pair is the right
                # capacitor. Escaping left would short its internal junction.
                escape = dx + bounds.right * layout.dbu + 1.2
                box(67, x - .2, y - .2, escape + .2, y + .2)
                via(50, 67, 66, escape, y)
            elif pin['layer'] == 126:
                # TopMetal1 minimum width is 1.64 um. Drop down outside the
                # MIM plate so TopVia1 cannot short its two electrodes.
                escape = dx + bounds.right * layout.dbu + 3
                above = dy + bounds.top * layout.dbu + 3
                # Rise above both plates before crossing the second unit's
                # top electrode, which belongs to the series junction.
                box(126, x - 1, y - 1, x + 1, above + 1)
                box(126, x - 1, above - 1, escape + 1, above + 1)
                y = above
                box(125, escape - .21, y - .21, escape + .21, y + .21)
                box(67, escape - .4, y - .4, escape + .4, y + .4)
                via(50, 67, 66, escape, y)
            else:
                raise ValueError('unsupported physical terminal layer')
            pins[net].append((escape, y))
            escapes.append(dict(component=cell.name, terminal=name, net=net,
                                location_um=[escape, y], kind=kind))
        definition = info['schematic']
        extra_terminal = ''
        if kind == 'mos' and info['model'] == 'sg13_lv_nmos':
            # NMOS bodies share the physical p-substrate. Each guard contact
            # retains its own tap device, while its substrate side connects
            # to one internal chip net, not an isolated per-device body.
            definition = definition.replace(' D G S B\n', ' D G S B SUB\n')
            definition = re.sub(r'\bbody\b', 'SUB', definition)
            extra_terminal = ' PSUB'
        definitions.append(definition)
        instances.append('X' + str(len(instances)) + ' ' + ' '.join(connections.values()) + extra_terminal + ' ' + cell.name)
        components.append(dict(name=cell.name, kind=kind, geometry=info, connections=connections,
                               origin_um=[dx, dy]))
        cursor = math.ceil((dx + bounds.right * layout.dbu + 5) * 100) / 100

    parts = []
    for index, device in enumerate(devices):
        width = device['width_um']
        fingers = math.ceil(width / 10)
        cell, info = append_mos(layout, pdk, 'GateMOS' + str(index), width, device['length_um'],
            fingers, 'p' if device['model'].endswith('pmos') else 'n', 'lv')
        parts.append((cell, info, dict(D=device['a'], G=device['gate'], S=device['b'], B=device['body']), 'mos'))
    for index, (a, b, value) in enumerate(capacitors):
        make = append_mim_series if value < 3e-15 else append_mim
        cell, info = make(layout, pdk, 'GateStorage' + str(index), value)
        parts.append((cell, info, dict(PLUS=a, MINUS=b), 'capacitor'))
    order, placement = placement_order(parts, layout.dbu)
    for index in order:
        place(*parts[index])
    terminals = {}
    for index, (net, points) in enumerate(sorted(pins.items())):
        y = -2 - index
        xs = [point[0] for point in points]
        box(67, min(xs) - .2, y - .2, max(xs) + .2, y + .2)
        for x, endpoint in points:
            box(50, x - .2, y - .2, x + .2, endpoint + .2)
            via(50, 67, 66, x, y)
        if net in ('VDD', 'VSS', 'CLOCK', 'ENABLE', 'OUT'):
            x = min(xs)
            top.shapes(layout.layer(67, 2)).insert(db.Box(*(round(v / layout.dbu)
                for v in (x - .16, y - .16, x + .16, y + .16))))
            top.shapes(layout.layer(67, 25)).insert(db.Text(net, db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
            terminals[net] = dict(layer=67, x_um=x, y_um=y)
    schematic = '\n'.join(definitions) + '\n.SUBCKT ProcessingClockGate VDD VSS CLOCK ENABLE OUT\n'
    schematic += '\n'.join(instances) + '\n.ENDS ProcessingClockGate\n'
    return layout, top, dict(components=components, terminals=terminals, escapes=escapes, schematic=schematic,
        placement=placement,
        degenerate_capacitors=degenerate, width_um=top.bbox().width() * layout.dbu,
        height_um=top.bbox().height() * layout.dbu, bounding_area_um2=top.bbox().area() * layout.dbu ** 2,
        scope='connected exported clock-gate topology; requires DRC/LVS and extracted electrical qualification')
