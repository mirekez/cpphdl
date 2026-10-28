"""Stream routed frame SRAMs using their actual PDK GDS, not LEF abstracts."""
from pathlib import Path
import klayout.db as db

MACRO = 'RM_IHPSG13_2P_1024x16_c2_bm_bist'
TOP = 'FlowControlledRepeater'


def stream(pdk, work):
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
    layout.read(str(reference / ('sg13g2_sram/gds/' + MACRO + '.gds')))
    result = db.Layout()
    result.dbu = layout.dbu
    final = result.create_cell(TOP)
    final.copy_tree(top)
    empty = [cell.name for cell in result.each_cell() if cell.is_empty()]
    if empty:
        raise ValueError('missing physical GDS masters: ' + ', '.join(empty))
    result.write(str(work / 'flow_control_unfilled.gds'))
    return dict(top=TOP, sram_instances=8, physical_cells=result.cells(), dbu_um=result.dbu,
        bbox_um=[v * result.dbu for v in (final.bbox().left, final.bbox().bottom,
                                        final.bbox().right, final.bbox().top)])
