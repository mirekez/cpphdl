#!/usr/bin/env python3
"""Fill chip space around intact IHP I/O macros using a bounded-memory proxy.

The proxy contains the actual seal and conservative no-fill boxes over existing
macro geometry. Only generated filler-purpose geometry returns to the real GDS.
No proxy exclusion marker is added to the fabricated layout or its DRC input.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import klayout.db as db

SOURCE = Path(__file__).resolve().parent
FILL_LAYERS = {(layer, 22) for layer in (1, 5, 8, 10, 30, 50, 67, 126, 134)}


def prepare_proxy(original, top, proxy):
    proxy.dbu = original.dbu
    target = proxy.create_cell("PadframeFillProxy")
    margin = round(10 / original.dbu)
    feol_margin = round(5 / original.dbu)
    exclusions = []
    seals = 0
    for instance in top.each_inst():
        cell = instance.cell
        seal = db.Region(cell.begin_shapes_rec(original.layer(39, 0)))
        if not seal.is_empty():
            copy = proxy.create_cell(cell.name)
            copy.copy_tree(cell)
            target.insert(db.CellInstArray(copy.cell_index(), instance.trans))
            seals += 1
        else:
            # These footprints enclose all existing geometry, not just LEF
            # abutment boxes. The pad cells and their internal fill stay intact.
            box = cell.bbox().transformed(instance.trans).enlarged(margin)
            exclusions.append([box.left, box.bottom, box.right, box.top])
            for layer, purpose in ((1, 23), (5, 23), (160, 0), (126, 23), (134, 23)):
                exclusion = cell.bbox().transformed(instance.trans).enlarged(feol_margin) if layer in (1, 5) else box
                target.shapes(proxy.layer(layer, purpose)).insert(exclusion)
    if seals != 1:
        raise ValueError("padframe proxy requires exactly one actual sealring instance")
    return target, exclusions


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    layout = db.Layout()
    layout.read(str(args.layout.resolve()))
    top = layout.top_cell()
    if top is None or top.name != "RepeaterPadframe":
        raise ValueError("fill expects the actual RepeaterPadframe top")
    original_instances = sum(1 for _ in top.each_inst())
    proxy = db.Layout()
    proxy_top, boxes = prepare_proxy(layout, top, proxy)
    proxy_path = work / "fill_proxy.gds"
    proxy_filled = work / "fill_proxy_filled.gds"
    proxy.write(str(proxy_path))
    environment = dict(os.environ, REPEATER_PDK=str(args.pdk.resolve()))
    command = [str(args.tools.resolve() / "bin/klayout"), "-zz", "-r", str(SOURCE / "fill_padframe_macro.py"),
               "-rd", "output_file=" + str(proxy_filled), str(proxy_path)]
    with (work / "fill.log").open("w") as log:
        subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
    filled = db.Layout()
    filled.read(str(proxy_filled))
    filled_top = filled.top_cell()
    counts = {}
    for layer_index in filled.layer_indexes():
        info = filled.get_info(layer_index)
        if (info.layer, info.datatype) not in FILL_LAYERS:
            for cell in filled.each_cell():
                cell.shapes(layer_index).clear()
        else:
            counts[str(info)] = sum(cell.shapes(layer_index).size() for cell in filled.each_cell())
    for cell_index in list(filled.each_cell_bottom_up()):
        cell = filled.cell(cell_index)
        for instance in list(cell.each_inst()):
            if instance.cell.is_empty():
                instance.delete()
    if len([count for count in counts.values() if count]) != len(FILL_LAYERS):
        raise ValueError("PDK fill omitted material layers")
    fill_cell = layout.create_cell("PadframeFill")
    fill_cell.copy_tree(filled_top)
    for layer_index in layout.layer_indexes():
        info = layout.get_info(layer_index)
        if ((info.layer, info.datatype) not in FILL_LAYERS and
                not fill_cell.begin_shapes_rec(layer_index).at_end()):
            raise ValueError("fill imported functional geometry or an exclusion marker")
    top.insert(db.CellInstArray(fill_cell.cell_index(), db.Trans()))
    if sum(1 for _ in top.each_inst()) != original_instances + 1:
        raise ValueError("fill altered the existing pad/seal instance list")
    options = db.SaveLayoutOptions()
    options.write_context_info = False
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    layout.write(str(output), options)
    report = dict(status="filled_not_verified", top=top.name, no_drc_or_lvs_waivers=True,
                  original_instances=original_instances, fill_layer_shapes=counts,
                  exclusion_margin_um=dict(active=5, poly=5, metal=10),
                  proxy_exclusions_dbu=boxes, proxy_markers_in_output=False,
                  input_sha256=hashlib.sha256(args.layout.read_bytes()).hexdigest(),
                  output_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
                  output=str(output))
    (work / "fill.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--pdk", type=Path, default=SOURCE.parent / "IHPSG13G2")
    parser.add_argument("--tools", type=Path, default=SOURCE.parents[1] / "out/repeater-toolchain")
    run(parser.parse_args())
