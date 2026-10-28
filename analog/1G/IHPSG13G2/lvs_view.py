"""Scope device pin labels for flat LVS without changing any mask geometry."""
import hashlib
import json
from pathlib import Path
import klayout.db as db

PIN_LAYERS = {(8, 25), (10, 25), (30, 25), (50, 25), (67, 25), (126, 25), (134, 25)}
DEVICE_PINS = {'D', 'G', 'S', 'B', 'PLUS', 'MINUS', 'SUB'}


def local_pin(layout, cell, top, layer, shape):
    info = layout.get_info(layer)
    return cell.name != top and (info.layer, info.datatype) in PIN_LAYERS and shape.is_text()


def mask_signature(layout, top):
    """Include hierarchy, transforms, all shapes and every other annotation."""
    checksum = hashlib.sha256()
    checksum.update(str(layout.dbu).encode())
    for cell in sorted(layout.each_cell(), key=lambda c: c.name):
        checksum.update(('cell:' + cell.name + '\n').encode())
        for layer in sorted(layout.layer_indexes(), key=lambda i: layout.get_info(i).to_s()):
            shapes = sorted(s.to_s() for s in cell.each_shape(layer)
                            if not local_pin(layout, cell, top, layer, s))
            if shapes:
                checksum.update((layout.get_info(layer).to_s() + '\n' + '\n'.join(shapes) + '\n').encode())
        instances = sorted((layout.cell(i.cell_index).name, i.cplx_trans.to_s(),
                            i.na, i.nb, i.a.to_s(), i.b.to_s()) for i in cell.each_inst())
        checksum.update(json.dumps(instances).encode())
    return checksum.hexdigest()


def create_flat_view(source, target, top, extra_device_pins=()):
    source, target = Path(source).resolve(), Path(target).resolve()
    layout = db.Layout()
    layout.read(str(source))
    if [c.name for c in layout.top_cells()] != [top]:
        raise ValueError('flat LVS view requires one known top cell')
    before = mask_signature(layout, top)
    allowed = DEVICE_PINS | set(extra_device_pins)
    if any(not isinstance(pin, str) or not pin or any(c.isspace() for c in pin) for pin in allowed):
        raise ValueError('invalid explicit local device pin name')
    removed = []
    for cell in layout.each_cell():
        for layer in layout.layer_indexes():
            for shape in list(cell.each_shape(layer)):
                if local_pin(layout, cell, top, layer, shape):
                    if shape.text.string not in allowed:
                        raise ValueError('unknown local device pin label: ' + shape.text.string)
                    removed.append(dict(cell=cell.name, layer=layout.get_info(layer).to_s(), text=shape.to_s()))
                    shape.delete()
    if not removed:
        raise ValueError('no local device pin labels to scope')
    target.parent.mkdir(parents=True, exist_ok=True)
    layout.write(str(target))
    checked = db.Layout()
    checked.read(str(target))
    if mask_signature(checked, top) != before:
        raise ValueError('LVS view changed mask geometry, hierarchy or other annotations')
    if any(local_pin(checked, cell, top, layer, shape) for cell in checked.each_cell()
           for layer in checked.layer_indexes() for shape in cell.each_shape(layer)):
        raise ValueError('local device labels remain in the flat LVS view')
    return dict(mask_and_hierarchy_sha256=before, removed_local_pin_labels=removed,
                explicitly_allowed_device_pins=sorted(allowed),
                source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                view_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                scope='only child device pin text removed; top ports, device annotations, masks and hierarchy preserved')
