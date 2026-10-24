"""Connect actual 1.2 V IHP decap cells to the padframe's core supply rails.

The unloaded supply ring otherwise presents large metal area to the few VDD-
tied dummy gates in input level shifters. These are real supply capacitors,
with an independent transistor schematic, not antenna waivers or dummy labels.
"""
import hashlib
import re
import klayout.db as db


def append_decoupling(layout, top, pdk, core_size=640):
    library = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    lef_path = library / 'lef/sg13g2_stdcell.lef'
    lef = lef_path.read_text()
    description = re.search(r'^MACRO sg13g2_decap_8\s*$(.*?)^END sg13g2_decap_8\s*$', lef, re.M | re.S)
    if not description or not re.search(r'SIZE 3\.36 BY 3\.78\s*;', description[1]):
        raise ValueError('review changed IHP decap cell geometry')
    layout.read(str(library / 'gds/sg13g2_stdcell.gds'))
    master = layout.cell('sg13g2_decap_8')
    if master is None or master.is_empty():
        raise ValueError('missing actual IHP decap layout')
    block = layout.create_cell('RepeaterCoreDecoupling')
    scale = 1 / layout.dbu
    offset_x = core_size - 640
    count, start_x, start_y, contact_x = 32, 820. + offset_x, 350., 930. + offset_x

    def box(layer, x0, y0, x1, y1):
        block.shapes(layout.layer(layer, 0)).insert(db.Box(*[round(v * scale) for v in (x0, y0, x1, y1)]))

    for i in range(count):
        block.insert(db.CellInstArray(master.cell_index(), db.Trans(round((start_x + i * 3.36) * scale),
                                                                   round(start_y * scale))))
    # M1 row rails. Different upper layers reach the east pad's VDD(M3) and
    # VSS(M4) rails without crossing or shorting its alternating power stripes.
    for y, top_layer in ((start_y + 3.78, 30), (start_y, 50)):
        box(8, start_x, y - .22, contact_x + .22, y + .22)
        for metal in (10, 30):
            box(metal, contact_x - .19, y - .19, contact_x + .19, y + .19)
        for via in (19, 29):
            box(via, contact_x - .095, y - .095, contact_x + .095, y + .095)
        if top_layer == 50:
            box(50, contact_x - .19, y - .19, contact_x + .19, y + .19)
            box(49, contact_x - .095, y - .095, contact_x + .095, y + .095)
        box(top_layer, contact_x - .25, y - .25, 970.25 + offset_x, y + .25)
    top.insert(db.CellInstArray(block.cell_index(), db.Trans()))
    schematic = ['.SUBCKT RepeaterCoreDecoupling vdd vss']
    schematic.extend(f'Xdecap{i} vdd vss sg13g2_decap_8' for i in range(count))
    schematic.append('.ENDS')
    report = dict(cell='sg13g2_decap_8', count=count, supply='vdd', return_net='vss',
                  core_supply_V=1.2, origin_um=[start_x, start_y], row_pitch_um=3.36,
                  connection_layers=dict(vdd='Metal3', vss='Metal4'),
                  source_sha256={str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in
                                 (lef_path, library / 'gds/sg13g2_stdcell.gds', library / 'cdl/sg13g2_stdcell.cdl')})
    return report, '\n'.join(schematic) + '\n'
