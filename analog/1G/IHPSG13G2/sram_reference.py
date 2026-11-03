"""Build an explicit SRAM schematic variant matching measured metal widths.

This changes the reference circuit, not the PDK layout or LVS tolerances.
The resulting file is not the original PDK schematic and does not by itself
qualify SRAM timing, function, physical design or the complete ASIC.
"""
import argparse
from collections import Counter
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re

MACRO = 'RM_IHPSG13_2P_1024x16_c2_bm_bist'
BITCELL = 'RM_IHPSG13_1024x16_c2_2P_BITKIT_CELL'
RESISTORS = {
    'R0': ('A_BLT_BOT', 'A_BLT_TOP', 'res_metal2'),
    'R1': ('A_BLC_BOT', 'A_BLC_TOP', 'res_metal2'),
    'R2': ('A_RWL', 'A_LWL', 'res_metal3'),
    'R3': ('B_BLT_BOT', 'B_BLT_TOP', 'res_metal2'),
    'R4': ('B_BLC_BOT', 'B_BLC_TOP', 'res_metal2'),
    'R5': ('B_RWL', 'B_LWL', 'res_metal3'),
}


def rewrite_resistors(block, expected, width):
    """Change exactly the declared resistor widths/models; preserve terminals."""
    seen, result = set(), []
    for line in block.splitlines():
        fields = line.split()
        if fields and fields[0].startswith('R'):
            name = fields[0]
            if name not in expected or name in seen:
                raise ValueError('unexpected or duplicate SRAM resistor')
            a, b, model = expected[name]
            if fields != [name, a, b, 'lvsres', 'w=2.6e-07', 'l=6e-07']:
                raise ValueError('original SRAM resistor specification changed: ' + name)
            seen.add(name)
            line = f'{name} {a} {b} {model} w={width} l=6e-07'
        result.append(line)
    if seen != set(expected):
        raise ValueError('missing SRAM resistor')
    return '\n'.join(result)


def reachable(text, top):
    if re.search(r'(?im)^\s*\.(include|lib|param|global|model)\b', text):
        raise ValueError('unexpected external circuit directives')
    text = re.sub(r'\n\+\s*', ' ', text)
    blocks = {}
    for match in re.finditer(r'(?im)^\.subckt\s+(\S+).*?^\.ends[^\n]*', text, re.S):
        name = match[1].upper()
        if name in blocks:
            raise ValueError('duplicate SRAM circuit')
        blocks[name] = match[0]
    active = set()

    @lru_cache(None)
    def inventory(name):
        if name in active or name not in blocks:
            raise ValueError('recursive or unresolved SRAM hierarchy')
        active.add(name)
        counts = Counter({name: 1})
        for line in blocks[name].splitlines():
            fields = line.split()
            if fields and fields[0].upper().startswith('X'):
                counts.update(inventory(fields[-1].upper()))
        active.remove(name)
        return counts

    return blocks, inventory(top.upper())


def reference(text):
    blocks, counts = reachable(text, MACRO)
    expected = {BITCELL.upper(): 16384, 'RSC_IHPSG13_MET2RES': 64,
                'RSC_IHPSG13_MET3RES': 192, 'RSC_IHPSG13_CDLYX1_DUMMY': 28}
    if any(counts[name] != count for name, count in expected.items()):
        raise ValueError('SRAM resistor hierarchy inventory changed')
    blocks[BITCELL.upper()] = rewrite_resistors(blocks[BITCELL.upper()], RESISTORS, '2.0e-07')
    for metal, width in ((2, '2.6e-07'), (3, '2.4e-07')):
        name = f'RSC_IHPSG13_MET{metal}RES'
        blocks[name] = rewrite_resistors(blocks[name], {'R0': ('B', 'A', f'res_metal{metal}')}, width)
    name = 'RSC_IHPSG13_CDLYX1_DUMMY'
    blocks[name] = rewrite_resistors(blocks[name], {'R0': ('Z', 'A', 'res_metal1')}, '2.6e-07')
    rendered = '\n\n'.join(blocks[name] for name in sorted(counts)) + '\n'
    if re.search(r'\blvsres\b', rendered):
        raise ValueError('unclassified reachable metal resistor')
    return rendered, dict(expected)


def audit_geometry(gds):
    import klayout.db as db
    layout = db.Layout()
    layout.read(str(gds))
    top = layout.top_cell()
    if top.name != MACRO or layout.dbu != .001:
        raise ValueError('unexpected SRAM layout or database units')
    expected = {8: {(260, 600): 28}, 10: {(200, 600): 65536, (260, 600): 64},
                30: {(200, 600): 32768, (240, 600): 192}}
    measured = {}
    for metal, dimensions in expected.items():
        counts = Counter()
        markers = db.Region(top.begin_shapes_rec(layout.layer(metal, 29)))
        conductors = db.Region(top.begin_shapes_rec(layout.layer(metal, 0)))
        if not (markers - conductors).is_empty():
            raise ValueError('SRAM resistor marker is not covered by its metal')
        for polygon in markers.each():
            box = polygon.bbox()
            if polygon.area() != box.area():
                raise ValueError('nonrectangular SRAM resistor body')
            counts[tuple(sorted((box.width(), box.height())))] += 1
        if dict(counts) != dimensions:
            raise ValueError('SRAM resistor geometry changed on layer ' + str(metal))
        measured[str(metal)] = {f'{w}x{length}_nm': count for (w, length), count in counts.items()}
    # M1 dummy resistors retain their original physical dimensions. Their
    # connectivity remains subject to complete macro LVS, including open nets.
    return measured


def build(pdk, output, report_path):
    output, report_path = Path(output), Path(report_path)
    output.unlink(missing_ok=True)
    report_path.unlink(missing_ok=True)
    library = Path(pdk) / 'ihp-sg13g2/libs.ref/sg13g2_sram'
    gds, cdl = library / 'gds' / (MACRO + '.gds'), library / 'cdl' / (MACRO + '.cdl')
    inputs = {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in (gds, cdl, Path(__file__))}
    measured = audit_geometry(gds)
    text, inventory = reference(cdl.read_text())
    output.parent.mkdir(parents=True, exist_ok=True)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    if any(hashlib.sha256(Path(p).read_bytes()).hexdigest() != value for p, value in inputs.items()):
        raise ValueError('SRAM reference inputs changed during generation')
    output.write_text('* Explicit physical-width SRAM reference variant; qualification required.\n' + text)
    report = dict(status='reference_generated', source_sha256=inputs, measured_geometry=measured,
                  instantiated_resistor_cells=inventory, geometry_changed=False,
                  original_pdk_schematic_equivalent=False, full_sram_qualified=False,
                  changes={'bitcell_resistor_width_um': [.26, .20],
                           'peripheral_metal3_resistor_width_um': [.26, .24]},
                  output_sha256=hashlib.sha256(output.read_bytes()).hexdigest())
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'output', 'report'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    build(args.pdk, args.output, args.report)
