"""Stream routed frame SRAMs using their actual PDK GDS, not LEF abstracts."""
from pathlib import Path
import klayout.db as db

MACRO = 'RM_IHPSG13_2P_1024x16_c2_bm_bist'
TOP = 'FlowControlledRepeater'
SRAM_LEGACY_PIN_PORTS = {
    'RSC_IHPSG13_CINVX2': {'A', 'Z', 'VDD!', 'VSS!'},
    'RSC_IHPSG13_CINVX8': {'A', 'Z', 'VDD!', 'VSS!'},
    'RSC_IHPSG13_FILLCAP4': {'VDD!', 'VSS!'},
    'RSC_IHPSG13_FILLCAP8': {'VDD!', 'VSS!'},
}


def copy_sram_pin_labels(layout):
    """Expose verified legacy SRAM pin texts to the PDK's Metal1 label layer.

    Preserve the original pin texts and every conductor. Check all four cells
    before inserting labels, including on repeated calls. This does not connect
    separate conductors or qualify the SRAM's transistor-level LVS.
    """
    pin_layer = layout.find_layer(8, 2)
    conductor_layer = layout.find_layer(8, 0)
    label_layer = layout.find_layer(8, 25)
    if pin_layer is None or conductor_layer is None:
        raise ValueError('missing SRAM Metal1 pins or conductors')
    pending = []
    cells = {}
    for name, expected in SRAM_LEGACY_PIN_PORTS.items():
        cell = layout.cell(name)
        if cell is None or cell.is_ghost_cell():
            raise ValueError('missing legacy SRAM cell: ' + name)
        pins = [shape.text for shape in cell.shapes(pin_layer).each() if shape.is_text()]
        if len(pins) != len(expected) or {text.string for text in pins} != expected:
            raise ValueError('unexpected legacy SRAM pin interface: ' + name)
        iterator = cell.begin_shapes_rec(conductor_layer)
        iterator.shape_flags = db.Shapes.SAll & ~db.Shapes.STexts
        conductor = db.Region(iterator)
        labels = ([] if label_layer is None else
                  [shape.text for shape in cell.shapes(label_layer).each() if shape.is_text()])
        if any(label not in pins for label in labels):
            raise ValueError('conflicting SRAM pin label: ' + name)
        for text in pins:
            point = db.Point(text.x, text.y)
            if not any(polygon.inside(point) for polygon in conductor.each()):
                raise ValueError('legacy SRAM pin misses Metal1: ' + name + ':' + text.string)
            related = [label for label in labels if label.string == text.string or
                       (label.x == text.x and label.y == text.y)]
            if related:
                if len(related) != 1 or related[0] != text:
                    raise ValueError('conflicting SRAM pin label: ' + name + ':' + text.string)
            else:
                pending.append((cell, text))
        cells[name] = sorted(expected)
    if pending:
        destination = layout.layer(8, 25)
        for cell, text in pending:
            cell.shapes(destination).insert(text)
    return dict(cells=cells, copied_labels=len(pending),
                source_layer=[8, 2], destination_layer=[8, 25])


def library_empty_cells(path, master):
    """Identify defined empty descendants in the supplied physical library."""
    library = db.Layout()
    library.read(str(path))
    root = library.cell(master)
    if root is None or root.is_empty() or root.bbox().empty():
        raise ValueError('missing physical library master: ' + master)
    cells = [root, *(library.cell(i) for i in root.called_cells())]
    ghosts = [cell.name for cell in cells if cell.is_ghost_cell()]
    if ghosts:
        raise ValueError('undefined physical library masters: ' + ', '.join(ghosts))
    return {cell.name for cell in cells if cell.is_empty()}


def validate_physical_cells(layout, library_placeholders):
    missing = [cell.name for cell in layout.each_cell()
               if cell.is_ghost_cell() or
               (cell.is_empty() and cell.name not in library_placeholders)]
    if missing:
        raise ValueError('missing physical GDS masters: ' + ', '.join(missing))


def check_sram_placement_interface(original, replacement):
    """The routed LEF placement must remain valid for the verified variant."""
    signatures = []
    for path in (original, replacement):
        layout = db.Layout()
        layout.read(str(path))
        root = layout.cell(MACRO)
        if root is None or root.is_ghost_cell() or root.is_empty():
            raise ValueError('missing physical SRAM interface')
        labels = sorted((str(layout.get_info(layer)), str(shape.text))
                        for layer in layout.layer_indexes()
                        for shape in root.shapes(layer).each() if shape.is_text())
        signatures.append((layout.dbu, str(root.bbox()), labels))
    if signatures[0] != signatures[1]:
        raise ValueError('verified SRAM changed the routed placement/pin interface')


def stream(pdk, work, sram):
    reference = pdk / 'ihp-sg13g2/libs.ref'
    technology = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    tech = db.Technology()
    tech.load(str(technology / 'sg13g2.lyt'))
    options = tech.load_layout_options
    config = options.lefdef_config
    config.lef_files = [str(work / 'route_tech.lef'),
        str(reference / 'sg13g2_stdcell/lef/sg13g2_stdcell.lef'),
        str(reference / ('sg13g2_sram/lef/' + MACRO + '.lef'))]
    config.map_file = str(technology / 'sg13g2.map')
    config.produce_cell_outlines = False
    config.produce_placement_blockages = False
    config.produce_regions = False
    config.produce_obstructions = False
    config.produce_blockages = False
    options.lefdef_config = config
    layout = db.Layout()
    layout.read(str(work / 'core.def'), options)
    top = layout.cell(TOP)
    if top is None:
        raise ValueError('routed DEF has the wrong SRAM top')
    if sum(i.cell.name == MACRO for i in top.each_inst()) != 8:
        raise ValueError('routed DEF lost frame SRAM banks')
    for cell in layout.each_cell():
        if cell.cell_index() != top.cell_index() and not cell.name.startswith('VIA_'):
            cell.clear()
    layout.read(str(reference / 'sg13g2_stdcell/gds/sg13g2_stdcell.gds'))
    sram_gds = sram['gds']
    check_sram_placement_interface(reference / ('sg13g2_sram/gds/' + MACRO + '.gds'), sram_gds)
    # The PDK SRAM hierarchy contains explicitly defined empty helper cells.
    # Derive these from the original GDS, never from a possibly missing DEF master.
    placeholders = library_empty_cells(sram_gds, MACRO)
    layout.read(str(sram_gds))
    result = db.Layout()
    result.dbu = layout.dbu
    final = result.create_cell(TOP)
    final.copy_tree(top)
    validate_physical_cells(result, placeholders)
    result.write(str(work / 'flow_control_unfilled.gds'))
    return dict(top=TOP, sram_instances=8, physical_cells=result.cells(), dbu_um=result.dbu,
        library_empty_cells=sorted(placeholders), sram_gds=str(sram_gds),
        sram_reference=str(sram['reference']),
        bbox_um=[v * result.dbu for v in (final.bbox().left, final.bbox().bottom,
                                        final.bbox().right, final.bbox().top)])
