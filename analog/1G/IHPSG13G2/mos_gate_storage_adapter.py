"""Expose the existing HV storage bank through row-compatible plate ports.

The adapter adds real metal and vias. Its nonlinear MOS charge remains
explicit; PLUS/MINUS names do not turn it into an ideal capacitor.
"""
import klayout.db as db

from mos_gate_storage_layout import append_hv_storage_bank


def append_hv_storage_adapter(layout, pdk, name, copies=220):
    bank, info = append_hv_storage_bank(layout, pdk, name + 'Bank', copies)
    top = layout.create_cell(name)
    top.insert(db.CellInstArray(bank.cell_index(), db.Trans()))
    gate, ground = info['terminals']['G'], info['terminals']['SUB']
    if gate['layer'] != 126 or ground['layer'] != 134:
        raise ValueError('HV storage bank terminal layers changed')
    gate_x = bank.bbox().left * layout.dbu - 10
    ground_x = bank.bbox().right * layout.dbu + 10

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    # Keep the bank's wide upper-metal trunks. The new landing lies beyond
    # the bank outline, away from its device gates and internal supply cuts.
    for layer, terminal, x in ((126, gate, gate_x), (134, ground, ground_x)):
        y = terminal['y_um']
        box(layer, min(x, terminal['x_um']) - 4, y - 4,
            max(x, terminal['x_um']) + 4, y + 4)
    # A row routes its Metal5 plate toward the right gutter. Put this
    # landing on that side so the escape cannot cross the bank's MOS gates.
    x = ground_x
    y = ground['y_um']
    for layer in (67, 126, 134):
        box(layer, x - 3, y - 3, x + 3, y + 3)
    for i in range(6):
        for j in range(6):
            cx, cy = x + (i - 2.5) * .84, y + (j - 2.5) * .84
            box(125, cx - .21, cy - .21, cx + .21, cy + .21)
    # 0.90 um cuts require at least 1.06 um edge-to-edge spacing.
    for dx in (-2, 0, 2):
        for dy in (-2, 0, 2):
            box(133, x + dx - .45, y + dy - .45, x + dx + .45, y + dy + .45)
    terminals = {}
    for net, layer, x, y in (('PLUS', 126, gate_x, gate['y_um']),
                             ('MINUS', 67, ground_x, ground['y_um'])):
        pin = db.Box(*(round(v / layout.dbu) for v in (x - .5, y - .5, x + .5, y + .5)))
        top.shapes(layout.layer(layer, 2)).insert(pin)
        top.shapes(layout.layer(layer, 25)).insert(db.Text(net,
            db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
        terminals[net] = dict(layer=layer, x_um=x, y_um=y)
    schematic = (f'.subckt {name} PLUS MINUS\nXstorage PLUS MINUS {bank.name}\n'
                 f'.ends {name}\n' + info['schematic'])
    return top, dict(storage_model='hv_gate', bank=info, terminals=terminals,
        schematic=schematic, bounding_area_um2=top.bbox().area() * layout.dbu ** 2,
        scope='physical upper-metal adapter around the unchanged HV gate-storage bank; requires fresh geometry/extraction/electrical qualification')
