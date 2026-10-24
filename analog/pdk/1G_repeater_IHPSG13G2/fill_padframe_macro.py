"""Apply pinned IHP fill patterns inside an actual closed chip sealring.

Run with the GDS open in KLayout, as for fill_core.py. This preserves the PDK's
EdgeSeal.holes window and every no-fill exclusion. Metal spacing is 1 um,
the same configurable value used by the independently checked core flow.
"""
import os
from pathlib import Path
import re
import pya

technology = Path(os.environ["REPEATER_PDK"]) / "ihp-sg13g2/libs.tech/klayout/tech"
layout = pya.CellView.active().layout()
top = pya.CellView.active().cell
seal = pya.Region(top.begin_shapes_rec(layout.layer(39, 0))).merged()
if seal.is_empty() or seal.holes().count() != 1:
    raise ValueError("padframe fill requires a real closed EdgeSeal ring")
for group in ("ActGatP", "Metal", "TopMetal"):
    macro = pya.Macro(str(technology / f"macros/sg13g2_filler_{group}.lym"))
    if "EdgeSeal.holes" not in macro.text:
        raise ValueError("pinned PDK fill window changed")
    if group == "Metal":
        macro.text, count = re.subn(r"('distance_m[1-5]' => )\d+(?:\.\d+)?", r"\g<1>1.0", macro.text)
        if count != 5:
            raise ValueError("pinned PDK metal fill configuration changed")
    macro.run()
options = pya.SaveLayoutOptions()
options.write_context_info = False
layout.write(output_file, options)
