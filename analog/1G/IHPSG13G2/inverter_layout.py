"""Route the two-transistor C++ IC into a small IHP CMOS layout.

The exported topology is authoritative. Geometry is an explicit engineering
choice, not an inference from the square-law beta or threshold.
"""
import math
from pathlib import Path
import re

import klayout.db as db

from mos_layout import append_lv_nmos, append_lv_pmos


def inverter_source(path):
    model = Path(path).read_text()
    marker, suffix = '    analog begin\n', '    end\nendmodule\n'
    if ('module cmos_channel(Vdd, Vss, A, Y);' not in model
            or model.count(marker) != 1 or not model.endswith(suffix)):
        raise ValueError('expected the exported C++ channel-model inverter')
    pattern = re.compile(r'I\(Y, (Vdd|Vss)\) <\+ cpphdl_channel\(V\(A, (Vdd|Vss)\), '
        r'V\(Y, (Vdd|Vss)\), ([-+0-9.eE]+), ([-+0-9.eE]+), ([-+0-9.eE]+)\);')
    rails = []
    for line in model.split(marker)[1].removesuffix(suffix).splitlines():
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        match = pattern.fullmatch(line)
        if not match:
            raise ValueError('unimplemented inverter law or changed terminals: ' + line)
        rail, gate_reference, drain_reference, beta, threshold, sign = match.groups()
        expected_sign = -1 if rail == 'Vdd' else 1
        if (rail != gate_reference or rail != drain_reference or not all(
                math.isclose(float(x), y, rel_tol=1e-12) for x, y in
                ((beta, .001), (threshold, .35), (sign, expected_sign)))):
            raise ValueError('changed C++ inverter device parameters')
        rails.append(rail)
    if sorted(rails) != ['Vdd', 'Vss']:
        raise ValueError('inverter requires exactly one pull-up and one pull-down')
    return dict(source=str(Path(path).resolve()), type='cmos<channel_model>',
                ports=['Vdd', 'Vss', 'A', 'Y'], channels=2)


def create_inverter(pdk, source):
    intent = inverter_source(source)
    layout = db.Layout()
    layout.dbu = .001
    n, nc = append_lv_nmos(layout, pdk, 'InverterN', 2, .13, 1)
    p, pc = append_lv_pmos(layout, pdk, 'InverterP', 4, .13, 1)
    top = layout.create_cell('PhysicalInverter')
    # Keep a 2 um gap between complete guard/well extents. Floorplan size
    # follows the actual generated devices rather than a fixed macro template.
    shift = round((nc['bbox_um'][2] - pc['bbox_um'][0] + 2) / layout.dbu)
    top.insert(db.CellInstArray(n.cell_index(), db.Trans()))
    top.insert(db.CellInstArray(p.cell_index(), db.Trans(shift, 0)))
    offset = shift * layout.dbu
    m2, m3, via2 = [layout.layer(a, 0) for a in (10, 30, 29)]

    def box(layer, x0, y0, x1, y1):
        top.shapes(layer).insert(db.Box(*(round(v / layout.dbu) for v in (x0, y0, x1, y1))))

    def via(x, y):
        for layer in (m2, m3):
            box(layer, x - .2, y - .2, x + .2, y + .2)
        box(via2, x - .095, y - .095, x + .095, y + .095)

    terminals = {}

    def pin(name, layer, x, y):
        box(layout.layer(layer, 2), x - .16, y - .16, x + .16, y + .16)
        top.shapes(layout.layer(layer, 25)).insert(db.Text(name, db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
        terminals[name] = dict(layer=layer, location_um=[x, y])

    # Join gate and drain buses with actual Metal2. Unequal device widths put
    # their drain ports at different heights, so the drain route has a jog.
    for terminal, label in (('G', 'A'), ('D', 'Y')):
        nx, ny = nc['terminals_um'][terminal]
        px, py = pc['terminals_um'][terminal]
        px += offset
        middle = (nc['bbox_um'][2] + pc['bbox_um'][0] + offset) / 2
        box(m2, nx - .2, ny - .2, middle + .2, ny + .2)
        box(m2, middle - .2, min(ny, py) - .2, middle + .2, max(ny, py) + .2)
        box(m2, middle - .2, py - .2, px + .2, py + .2)
        pin(label, 10, middle, py)
    # Connect each source and its real well/substrate contact to the rail.
    for contract, dx, side, label in ((nc, 0, -1, 'VSS'), (pc, offset, 1, 'VDD')):
        x = dx + contract['bbox_um'][0 if side < 0 else 2] + side
        ys = []
        for terminal in ('S', 'B'):
            tx, ty = contract['terminals_um'][terminal]
            tx += dx
            box(m2, min(x, tx) - .2, ty - .2, max(x, tx) + .2, ty + .2)
            via(x, ty)
            ys.append(ty)
        box(m3, x - .2, min(ys) - .2, x + .2, max(ys) + .2)
        if abs(ys[1] - ys[0]) < .61:
            # A narrow device puts its source and body branches closer than
            # the Metal2 notch rule. They serve the same rail: join the gap.
            bx = dx + contract['terminals_um']['B'][0]
            box(m2, min(x, bx) - .2, min(ys) - .2, max(x, bx) + .2, max(ys) + .2)
        pin(label, 30, x, sum(ys) / 2)
    schematic = (nc['schematic'] + '\n' + pc['schematic'] + '\n'
        '.SUBCKT PhysicalInverter A Y VDD VSS\n'
        'XN Y A VSS VSS InverterN\n'
        'XP Y A VDD VDD InverterP\n'
        '.ENDS PhysicalInverter\n')
    return layout, top, dict(intent=intent, devices=[nc, pc], terminals=terminals,
        schematic=schematic, bounding_area_um2=top.bbox().area() * layout.dbu ** 2,
        scope='routed C++ inverter topology and explicit process dimensions; no RC, power integrity or chip signoff')


def process_experiment(model, original, models, corner, temperature, supply):
    """Bind the actual exported test circuit to the same two device geometries.

    PSP intrinsic charge is retained. Body contacts have ideal bias here;
    physical tap and wire resistance require later extraction qualification.
    """
    if (corner not in ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs')
            or temperature not in (-40, 27, 125) or supply not in (1.08, 1.2, 1.32)):
        raise ValueError('unsupported inverter process experiment')
    marker, suffix = '    analog begin\n', '    end\nendmodule\n'
    if model.count(marker) != 1 or not model.endswith(suffix):
        raise ValueError('changed inverter experiment contribution block')
    observations = dict(re.findall(r'^let observation([0-9]+) = v\((n[0-9]+)\)$', original, re.M))
    if set(observations) != {'0', '1', '2'}:
        raise ValueError('changed inverter experiment observations')
    a, y, vdd = [observations[str(i)] for i in range(3)]
    capacitors = re.findall(r'^Cdevice\d+ (\S+) (\S+) ([-+0-9.eE]+) IC=([-+0-9.eE]+)$', original, re.M)
    expected_capacitance = {vdd: 20e-12, y: .16e-12}
    if (len(capacitors) != 2 or {c[0] for c in capacitors} != set(expected_capacitance)
            or any(minus != '0' or not math.isclose(float(value), expected_capacitance[plus], rel_tol=1e-12)
                   or not math.isclose(float(initial), 1.2) for plus, minus, value, initial in capacitors)):
        raise ValueError('changed inverter output load, decoupling or precharge')
    node = lambda value: '0' if value == 'n0' else value
    channel = re.compile(r'I\((n\d+), (n\d+)\) <\+ cpphdl_channel\(V\((n\d+), (n\d+)\), '
        r'V\((n\d+), (n\d+)\), ([-+0-9.eE]+), ([-+0-9.eE]+), ([-+0-9.eE]+)\);')
    resistor = re.compile(r'I\((n\d+), (n\d+)\) <\+ V\((n\d+), (n\d+)\) / ([-+0-9.eE]+);')
    devices, signs, resistances = [], [], []
    for line in model.split(marker)[1].removesuffix(suffix).splitlines():
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        match = resistor.fullmatch(line)
        if match:
            left, right, va, vb, value = match.groups()
            if (left, right) != (va, vb) or float(value) not in (10, 50):
                raise ValueError('changed inverter host resistance')
            devices.append(f'Rhost{len(devices)} {node(left)} {node(right)} {value}')
            resistances.append(float(value))
            continue
        match = channel.fullmatch(line)
        if not match:
            raise ValueError('unimplemented inverter experiment law: ' + line)
        drain, source, gate, gs, vd, ds, beta, threshold, sign = match.groups()
        polarity = float(sign)
        expected_source = vdd if polarity == -1 else 'n0'
        if ((drain, gate, source, gs, vd, ds) != (y, a, expected_source, expected_source, y, expected_source)
                or polarity not in (-1, 1) or not math.isclose(float(beta), .001)
                or not math.isclose(float(threshold), .35)):
            raise ValueError('changed inverter channel terminals or parameters')
        kind, width = ('pmos', 4) if polarity == -1 else ('nmos', 2)
        devices.append(f'Xdevice{kind} {node(y)} {node(a)} {node(source)} {node(source)} '
                       f'sg13_lv_{kind} w={width}u l=.13u ng=1')
        signs.append(polarity)
    if sorted(signs) != [-1, 1] or sorted(resistances) != [10, 50] or original.count('\nCdevice') != 2:
        raise ValueError('changed complete inverter experiment inventory')
    lines = original.splitlines()
    result = [lines[0], f'.lib "{models / "cornerMOSlv.lib"}" {corner}', f'.temp {temperature}']
    ratio = supply / 1.2
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line.startswith('Vsource'):
            wave = re.fullmatch(r'(Vsource\d+ \S+ \S+ )PWL\((.+)\)', line)
            if wave:
                values = wave[2].split()
                if len(values) % 2:
                    raise ValueError('changed inverter PWL format')
                values[1::2] = [f'{float(v) * ratio:.17g}' for v in values[1::2]]
                line = wave[1] + 'PWL(' + ' '.join(values) + ')'
            else:
                dc = re.fullmatch(r'(Vsource\d+ \S+ \S+ )DC ([-+0-9.eE]+)', line)
                if not dc:
                    raise ValueError('changed inverter source syntax')
                line = dc[1] + f'DC {float(dc[2]) * ratio:.17g}'
        if line.startswith('.ic '):
            line = re.sub(r'(=)([-+0-9.eE]+)', lambda m: '=' + f'{float(m[2]) * ratio:.17g}', line)
        if line.startswith('Cdevice'):
            line = re.sub(r'(IC=)([-+0-9.eE]+)', lambda m: m[1] + f'{float(m[2]) * ratio:.17g}', line)
        if line.startswith('tran ') and line.endswith(' uic'):
            # Initialize PSP intrinsic charge consistently with the retained
            # external capacitor and node voltages at the selected supply.
            line = line.removesuffix(' uic')
        if line == '.control':
            result += devices
        result.append(line)
        if line == '.control':
            result.append('set num_threads=1')
    deck = '\n'.join(result) + '\n'
    if any(token in deck for token in ('CircuitModel', 'design.osdi', 'cpphdl_channel')):
        raise ValueError('generic inverter law remains in PSP experiment')
    return deck
