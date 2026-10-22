#!/usr/bin/env python3
"""Assemble real IHP I/O/ESD cells, without substituting pads for a missing PHY."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import klayout.db as db

SOURCE = Path(__file__).resolve().parent
RAILS = ("vdd", "vss", "iovdd", "iovss")
LAYERS = {"Metal1": 8, "Metal2": 10, "Metal3": 30, "Metal4": 50,
          "Metal5": 67, "TopMetal1": 126, "TopMetal2": 134}


def pin_shapes(text, macro, pin):
    """Read the rectangular LEF pin ports; refuse a view we cannot interpret."""
    cell = re.search(r"^MACRO " + re.escape(macro) + r"\s*$(.*?)^END " + re.escape(macro) + r"\s*$", text, re.M | re.S)
    if cell is None:
        raise ValueError("missing LEF macro " + macro)
    found = re.search(r"^\s+PIN " + re.escape(pin) + r"\s*$(.*?)^\s+END " + re.escape(pin) + r"\s*$", cell[1], re.M | re.S)
    if found is None:
        raise ValueError(f"missing LEF pin {macro}.{pin}")
    layer, result = None, []
    for line in found[1].splitlines():
        match = re.search(r"LAYER (\w+)\s*;", line)
        if match:
            layer = match[1]
        rectangle = re.search(r"RECT ([-\d.]+) ([-\d.]+) ([-\d.]+) ([-\d.]+)\s*;", line)
        if rectangle:
            result.append((LAYERS[layer], tuple(map(float, rectangle.groups()))))
    if not result:
        raise ValueError(f"empty LEF pin {macro}.{pin}")
    return result


def pad_plan():
    analog = lambda port: [dict(name=f"mdi_{port}_{pair}_{polarity}", cell="sg13g2_IOPadAnalog")
                           for pair in range(4) for polarity in ("p", "n")]
    digital = [dict(name=name, cell="sg13g2_IOPadIn" if name != "active" else "sg13g2_IOPadOut4mA")
               for name in ("ref_clock", "reset", "enable", "active")]
    supply = [dict(name=f"{rail}_{index}", cell={"vdd": "sg13g2_IOPadVdd", "vss": "sg13g2_IOPadVss",
                                               "iovdd": "sg13g2_IOPadIOVdd", "iovss": "sg13g2_IOPadIOVss"}[rail])
              for index in range(3) for rail in RAILS]
    return {"south": analog("a"), "east": digital + supply[:4],
            "north": analog("b"), "west": supply[4:]}


def assemble(pdk, work):
    work.mkdir(parents=True, exist_ok=True)
    lib = pdk / "ihp-sg13g2/libs.ref/sg13g2_io"
    lef = (lib / "lef/sg13g2_io.lef").read_text()
    cdl = (lib / "cdl/sg13g2_io.cdl").read_text()
    layout = db.Layout()
    layout.read(str(lib / "gds/sg13g2_io.gds"))
    top = layout.create_cell("RepeaterPadframe")
    scale = 1 / layout.dbu
    ring = 1000
    inset = 140
    placements = []
    external = list(RAILS)
    schematic = []
    rail_regions = {rail: {layer: db.Region() for layer in LAYERS.values()} for rail in RAILS}
    footprints = []
    rail_terminals = {rail: [] for rail in RAILS}

    def place(name, cell_name, angle, x, y, nets):
        cell = layout.cell(cell_name)
        if cell is None or cell.is_empty():
            raise ValueError("missing physical I/O cell " + cell_name)
        transform = db.Trans(angle, False, round(x * scale), round(y * scale))
        top.insert(db.CellInstArray(cell.cell_index(), transform))
        footprint = cell.bbox().transformed(transform)
        # Use the LEF abutment box, since GDS label extents may exceed its boundary.
        width = 180 if cell_name == "sg13g2_Corner" else 80
        boundary = db.Box(0, 0, round(width * scale), round(180 * scale)).transformed(transform)
        footprints.append(db.Region(boundary))
        pins = re.search(r"^\.SUBCKT " + re.escape(cell_name) + r" ([^\n]+)", cdl, re.M)
        if pins is None:
            raise ValueError("missing I/O transistor schematic " + cell_name)
        pin_order = pins[1].split()
        if set(pin_order) != set(nets):
            raise ValueError("schematic pin mismatch " + name)
        schematic.append("X" + name + " " + " ".join(nets[pin] for pin in pin_order) + " " + cell_name)
        for pin, net in nets.items():
            shapes = pin_shapes(lef, cell_name, pin)
            # Label one conductive shape on each signal terminal. Abutted supply
            # rails receive the same net name and are also checked geometrically.
            layer, rectangle = shapes[0]
            box = db.Box(*[round(value * scale) for value in rectangle]).transformed(transform)
            top.shapes(layout.layer(layer, 25)).insert(db.Text(net, db.Trans(box.center())))
            if pin in RAILS:
                terminal = []
                for layer, rectangle in shapes:
                    box = db.Box(*[round(value * scale) for value in rectangle]).transformed(transform)
                    rail_regions[pin][layer].insert(box)
                    terminal.append((layer, box))
                rail_terminals[pin].append(terminal)
        placements.append(dict(name=name, cell=cell_name, rotation=angle * 90, x_um=x, y_um=y,
                               bbox_um=[value / scale for value in (footprint.left, footprint.bottom, footprint.right, footprint.top)],
                               nets=nets))

    for index, (angle, x, y) in enumerate(((0, 0, 0), (1, ring, 0), (2, ring, ring), (3, 0, ring))):
        place(f"corner{index}", "sg13g2_Corner", angle, inset + x, inset + y, dict(zip(RAILS, RAILS)))
    for side, pads in pad_plan().items():
        for index, pad in enumerate(pads):
            along = 180 + 80 * index
            angle, x, y = {"south": (0, along, 0), "east": (1, ring, along),
                           "north": (2, ring - along, ring), "west": (3, 0, ring - along)}[side]
            nets = dict(zip(RAILS, RAILS))
            if pad["cell"] == "sg13g2_IOPadAnalog":
                nets.update(pad=pad["name"], padres=pad["name"] + "_core")
                external.extend((nets["pad"], nets["padres"]))
            elif pad["cell"] in ("sg13g2_IOPadIn", "sg13g2_IOPadOut4mA"):
                terminal = "p2c" if pad["cell"] == "sg13g2_IOPadIn" else "c2p"
                nets.update(pad=pad["name"], **{terminal: pad["name"] + "_core"})
                external.extend((nets["pad"], nets[terminal]))
            place(pad["name"], pad["cell"], angle, inset + x, inset + y, nets)
    occupied = db.Region()
    for footprint in footprints:
        if not (occupied & footprint).is_empty():
            raise ValueError("pad/corner overlap")
        occupied += footprint
    expected = db.Region(db.Box(round(inset * scale), round(inset * scale), round((inset + ring) * scale), round((inset + ring) * scale)))
    expected -= db.Box(round((inset + 180) * scale), round((inset + 180) * scale), round((inset + ring - 180) * scale), round((inset + ring - 180) * scale))
    if not (occupied ^ expected).is_empty():
        raise ValueError("padframe has a gap")
    rail_checks = {}
    for rail, terminals in rail_terminals.items():
        # A cell's same-name LEF ports form one terminal, including connections
        # made inside the cell on other layers. Actual internal continuity is
        # verified separately by transistor LVS, not inferred from LEF rectangles.
        groups = list(range(len(terminals)))
        def find(index):
            while groups[index] != index:
                index = groups[index]
            return index
        for left in range(len(terminals)):
            for right in range(left):
                if any(layer_a == layer_b and box_a.touches(box_b)
                       for layer_a, box_a in terminals[left] for layer_b, box_b in terminals[right]):
                    groups[find(left)] = find(right)
        count = len({find(index) for index in range(len(terminals))})
        if count != 1:
            raise ValueError(f"{rail} abutment network is disconnected: {count} components")
        rail_checks[rail] = count
    for first in RAILS:
        for second in RAILS:
            if first >= second:
                continue
            for layer in LAYERS.values():
                if not (rail_regions[first][layer] & rail_regions[second][layer]).is_empty():
                    raise ValueError(f"supply short {first}/{second}")
    final = db.Layout()
    final.dbu = layout.dbu
    final.create_cell(top.name).copy_tree(top)
    gds = work / "repeater_padframe.gds"
    final.write(str(gds))
    (work / "padframe.cdl").write_text("* Real IHP padframe/ESD schematic; core terminals remain exposed.\n"
        + ".SUBCKT RepeaterPadframe " + " ".join(external) + "\n" + "\n".join(schematic) + "\n.ENDS\n")
    (work / "lvs.cir").write_text(f'.include "{lib / "cdl/sg13g2_io.cdl"}"\n.include "{work / "padframe.cdl"}"\n')
    report = dict(status="padframe_assembled", top=top.name, full_1000base_t_chip=False,
                  ring_um=ring, die_um=ring + 2 * inset, seal_ring_present=False,
                  cells=dict(Counter(item["cell"] for item in placements)), placements=placements,
                  ports=external, abutment_rail_components=rail_checks,
                  esd="PDK analog/input/output clamps and supply RC clamps; HBM/CDM qualification not performed",
                  gds_sha256=hashlib.sha256(gds.read_bytes()).hexdigest(),
                  source_sha256={str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in
                                 (lib / "gds/sg13g2_io.gds", lib / "lef/sg13g2_io.lef", lib / "cdl/sg13g2_io.cdl")})
    (work / "interface.json").write_text(json.dumps(external, indent=2) + "\n")
    (work / "padframe.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pdk", type=Path, default=SOURCE.parent / "IHPSG13G2")
    parser.add_argument("--work", type=Path, default=SOURCE / "results/padframe")
    args = parser.parse_args()
    print(json.dumps(assemble(args.pdk.resolve(), args.work.resolve()), indent=2))
