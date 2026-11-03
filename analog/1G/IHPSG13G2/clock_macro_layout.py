"""Embed native clock-cell ICs through their physical Metal3 ports."""
import copy
import itertools

import klayout.db as db

from reference_clock_cell_layout import create_detector
from reference_clock_divider_layout import create_divider


def append_clock_macro(layout, pdk, name, kind):
    if kind not in ('detector', 'divider') or layout.cell(name) is not None:
        raise ValueError('invalid native clock macro kind or cell name')
    source, original, geometry = (create_detector if kind == 'detector' else create_divider)(pdk)
    if not db.Region(original.begin_shapes_rec(source.layer(50, 0))).is_empty():
        raise ValueError('native clock macro occupies reserved Metal4 access columns')
    cell = layout.create_cell(name)
    cell.copy_tree(original)
    info = copy.deepcopy(geometry)
    # Supply ports share x in the compact standalone macro. Give ground a
    # separate column before the parent row adds vertical Metal4 connections.
    ground = info['terminals']['VSS']
    old_x, y = ground['x_um'], ground['y_um']
    x = cell.bbox().left * layout.dbu - 1
    for shape in list(cell.each_shape(layout.layer(30, 25))):
        if shape.is_text() and shape.text.string == 'VSS':
            shape.delete()
    removed = 0
    for shape in list(cell.each_shape(layout.layer(30, 2))):
        if shape.bbox().center() == db.Point(round(old_x / layout.dbu), round(y / layout.dbu)):
            shape.delete()
            removed += 1
    if removed != 1:
        raise ValueError('ambiguous clock ground port marker')
    cell.shapes(layout.layer(30, 0)).insert(db.DBox(x - .2, y - .2, old_x + .2, y + .2))
    cell.shapes(layout.layer(30, 2)).insert(db.DBox(x - .1, y - .1, x + .1, y + .1))
    cell.shapes(layout.layer(30, 25)).insert(db.DText('VSS', db.DTrans(x, y)))
    ground['x_um'] = x
    for a, b in itertools.combinations(info['terminals'].values(), 2):
        if abs(a['x_um'] - b['x_um']) < .64:
            raise ValueError('clock macro pin columns are insufficiently separated')
    header = '.subckt ' + original.name + ' '
    if info['schematic'].count(header) != 1:
        raise ValueError('ambiguous native clock top definition')
    info['schematic'] = info['schematic'].replace(header, '.subckt ' + name + ' ', 1)
    info.update(metal4_columns_clear=True, native_clock_kind=kind,
                bounding_area_um2=cell.bbox().area() * layout.dbu ** 2)
    return cell, info
