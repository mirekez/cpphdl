"""Give capacitor-only TX macros an actual, LVS-visible substrate return."""
import re

import klayout.db as db

from mos_layout import device_library


def add_storage_substrate_tap(layout, top, info, pdk):
    if 'VSS' not in info['terminals']:
        # Output capacitors return to VDD; substrate still needs its own
        # physical VSS contact and pin, independently of their two plates.
        x = top.bbox().left * layout.dbu + 1
        y = top.bbox().bottom * layout.dbu - 3
        def square(half):
            return db.Box(*(round(v / layout.dbu) for v in (x - half, y - half, x + half, y + half)))
        top.shapes(layout.layer(67, 0)).insert(square(.42))
        top.shapes(layout.layer(67, 2)).insert(square(.16))
        top.shapes(layout.layer(67, 25)).insert(db.Text('VSS', db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
        declaration = re.compile(r'^\.SUBCKT ' + re.escape(top.name) + r' ([^\n]+)$', re.M)
        if len(declaration.findall(info['schematic'])) != 1:
            raise ValueError('ambiguous output-storage interface')
        info['schematic'] = declaration.sub(lambda m: m[0] + ' VSS', info['schematic'])
        info['terminals']['VSS'] = dict(layer=67, x_um=x, y_um=y)
    port = info['terminals']['VSS']
    if port['layer'] != 67 or 'substrate_tap' in info:
        raise ValueError('storage tap requires the original Metal5 ground port')
    library = device_library(pdk)
    declaration = library.layout().pcell_declaration('ptap1')
    if declaration is None:
        raise ValueError('physical substrate tap PCell unavailable')
    variant = layout.add_pcell_variant(library, declaration.id(), dict(w='2u', l='2u'))
    cell = layout.create_cell(top.name + 'SubstrateTap')
    cell.insert(db.CellInstArray(variant, db.Trans()))
    cell.flatten(True)
    # The circuit's VSS wire owns the contact. Retain the PDK's sub! device
    # recognition text on drawing layers: LVS requires it to recognize ptap1.
    # Remove pin-purpose labels; they must not add ports to this macro.
    for index in layout.layer_indexes():
        layer = layout.get_info(index)
        for shape in list(cell.shapes(index).each()):
            recognition = (shape.is_text() and shape.text.string == 'sub!' and
                           (layer.layer, layer.datatype) in ((63, 0), (40, 0)))
            if (shape.is_text() and not recognition) or layer.datatype in (2, 25):
                shape.delete()
    active = db.Region(cell.begin_shapes_rec(layout.layer(1, 0)))
    if active.area() != round(4 / layout.dbu ** 2):
        raise ValueError('storage tap active dimensions changed')
    metal = db.Region(cell.begin_shapes_rec(layout.layer(8, 0)))
    point = metal.bbox().center()
    x, ground_y = port['x_um'], port['y_um']
    y = top.bbox().bottom * layout.dbu - 5
    dx, dy = x - point.x * layout.dbu, y - point.y * layout.dbu
    top.insert(db.CellInstArray(cell.cell_index(), db.Trans(round(dx / layout.dbu), round(dy / layout.dbu))))

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    box(8, x - .2, y - .2, x + .2, ground_y + .2)
    for layer in (8, 10, 30, 50, 67):
        box(layer, x - .42, ground_y - .42, x + .42, ground_y + .42)
    for layer in (19, 29, 49, 66):
        for ox in (-.21, .21):
            # Align exactly with the row builder's existing two-cut landing;
            # a shifted array would leave illegal partial spacing at Via4.
            box(layer, x + ox - .095, ground_y - .095,
                x + ox + .095, ground_y + .095)
    end = '.ENDS ' + top.name + '\n'
    if info['schematic'].count(end) != 1 or re.search(r'\bStorageSubstrate\b', info['schematic']):
        raise ValueError('ambiguous storage macro schematic')
    info['schematic'] = info['schematic'].replace(end, 'RStorageSubstrate VSS PSUB ptap1 A=4p P=8u\n' + end)
    info['substrate_tap'] = dict(cell=cell.name, origin_um=[dx, dy], active_area_um2=4,
                               active_perimeter_um=8, net='VSS')
    info.update(width_um=top.bbox().width() * layout.dbu,
                height_um=top.bbox().height() * layout.dbu,
                bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
