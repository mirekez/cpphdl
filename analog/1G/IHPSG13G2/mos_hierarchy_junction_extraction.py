"""Account for nested HV MOS diffusion without changing the extracted circuit.

Build a geometry-only view with the selected MOS macros at the top level.
All layer polygons must match the original GDS before the existing junction
restorer can use it. This view is not a replacement layout or an LVS proof;
the caller must attest the original GDS, netlist and extraction coordinates.
"""
from pathlib import Path

from extract_analog import digest
from mos_junction_extraction import restore


def restore_hierarchy(gds, macros, coordinates, text, work):
    """Restore junctions for explicit cell-name/geometry/instance-count bindings.

    Each entry in ``macros`` contains ``geometry`` (the append_mos descriptor)
    and ``instances``. Every extracted HV MOS must belong to exactly one
    selected macro. Rotated, mirrored, missing and duplicated occurrences
    are rejected. All terminal nodes and distributed RC remain unchanged.
    """
    import klayout.db as db

    gds, work = Path(gds).resolve(), Path(work).resolve()
    if not macros or any(type(m['instances']) is not int or m['instances'] < 1 for m in macros.values()):
        raise ValueError('positive explicit MOS instance counts required')
    work.mkdir(parents=True, exist_ok=False)
    original = db.Layout()
    original.read(str(gds))
    if len(original.top_cells()) != 1:
        raise ValueError('one original geometry top required')
    view = db.Layout()
    view.dbu = original.dbu
    top = view.create_cell('JunctionGeometryView')
    layers = {i: view.layer(original.get_info(i)) for i in original.layer_indexes()}
    components, counts = [], dict.fromkeys(macros, 0)

    def visit(cell, transform):
        if cell.name in macros:
            if transform.angle or transform.is_mirror():
                raise ValueError('rotated or mirrored MOS macro unsupported')
            counts[cell.name] += 1
            if counts[cell.name] > macros[cell.name]['instances']:
                raise ValueError('extra MOS macro occurrence')
            name = 'JunctionMacro' + str(len(components))
            copied = view.create_cell(name)
            copied.copy_tree(cell)
            top.insert(db.CellInstArray(copied.cell_index(), transform))
            components.append(dict(name=name, kind='mos', geometry=macros[cell.name]['geometry'],
                origin_um=[transform.disp.x * original.dbu, transform.disp.y * original.dbu]))
            return
        for source_layer, target_layer in layers.items():
            for shape in cell.each_shape(source_layer):
                top.shapes(target_layer).insert(shape).transform(transform)
        for instance in cell.each_inst():
            for placement in instance.cell_inst.each_trans():
                visit(instance.cell, transform * placement)

    visit(original.top_cell(), db.Trans())
    if any(counts[name] != entry['instances'] for name, entry in macros.items()):
        raise ValueError('missing or nested-overlapping MOS macro selection')
    # Compare all drawing and marker polygons, not only active and gate layers.
    for source_layer, target_layer in layers.items():
        before = db.Region(original.top_cell().begin_shapes_rec(source_layer))
        after = db.Region(top.begin_shapes_rec(target_layer))
        if not (before ^ after).is_empty():
            raise ValueError('hierarchy view changed GDS polygons: ' + str(original.get_info(source_layer)))
    view_path = work / 'junction-view.gds'
    view.write(str(view_path))
    corrected, binding = restore(view_path, dict(components=components), coordinates, text)
    binding.update(hierarchy_instances=counts, geometry_view=str(view_path),
        geometry_view_layers_equal=len(layers), geometry_view_purpose=__doc__)
    binding['source_sha256'].update({str(p): digest(p) for p in (Path(__file__).resolve(), gds)})
    binding.setdefault('artifact_sha256', {})[str(view_path)] = digest(view_path)
    return corrected, binding
