#!/usr/bin/env python3
"""Replace DEF abstracts with real IHP GDS cells; reject missing physical cells."""
import argparse
import json
from pathlib import Path

import klayout.db as db


def stream(pdk, work, top_name="RepeaterCore"):
    technology = pdk / "ihp-sg13g2/libs.tech/klayout/tech"
    cells = pdk / "ihp-sg13g2/libs.ref/sg13g2_stdcell"
    tech = db.Technology()
    tech.load(str(technology / "sg13g2.lyt"))
    options = tech.load_layout_options
    config = options.lefdef_config
    config.lef_files = [str(work / "route_tech.lef"), str(cells / "lef/sg13g2_stdcell.lef")]
    config.map_file = str(technology / "sg13g2.map")
    config.produce_cell_outlines = False
    config.produce_placement_blockages = False
    config.produce_regions = False
    config.produce_obstructions = False
    config.produce_blockages = False
    options.lefdef_config = config
    layout = db.Layout()
    layout.read(str(work / "core.def"), options)
    top = layout.cell(top_name)
    if top is None:
        raise ValueError("DEF did not contain " + top_name)
    # LEF geometry is only an abstract. Keep routed vias but replace every cell.
    for cell in layout.each_cell():
        if cell.cell_index() != top.cell_index() and not cell.name.startswith("VIA_"):
            cell.clear()
    layout.read(str(cells / "gds/sg13g2_stdcell.gds"))
    result = db.Layout()
    result.dbu = layout.dbu
    final = result.create_cell(top_name)
    final.copy_tree(top)
    empty = [cell.name for cell in result.each_cell() if cell.is_empty()]
    if empty:
        raise ValueError("missing GDS cells: " + ", ".join(empty))
    if final.bbox().empty() or result.cells() < 2:
        raise ValueError("empty/nonphysical stream output")
    result.write(str(work / "repeater_core_unfilled.gds"))
    summary = dict(top=final.name, physical_cells=result.cells(), dbu_um=result.dbu,
                   bbox_um=[value * result.dbu for value in (final.bbox().left, final.bbox().bottom,
                                                           final.bbox().right, final.bbox().top)],
                   scope="routed digital module; not a complete 1000BASE-T chip")
    (work / "stream.json").write_text(json.dumps(summary, indent=2) + "\n")
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pdk", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--top", default="RepeaterCore")
    args = parser.parse_args()
    print(json.dumps(stream(args.pdk.resolve(), args.work.resolve(), args.top), indent=2))
