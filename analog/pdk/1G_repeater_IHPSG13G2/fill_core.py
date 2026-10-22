"""Run the pinned IHP fill patterns inside a standalone core's extent.

Invoked in the KLayout application, with the unfilled GDS open. Upstream fill
macros select EdgeSeal.holes, which is empty for a core without a chip sealring.
The fill window is adapted and the PDK's configurable Metal1-5 fill distances
are set to 1 um (above its 0.42 um minimum). Exclusion rules are unchanged.
This does not add a sealring or claim a complete die.
"""
import os
import re
from pathlib import Path
import pya

technology = Path(os.environ["REPEATER_PDK"]) / "ihp-sg13g2/libs.tech/klayout/tech"
for group in ("ActGatP", "Metal", "TopMetal"):
    macro = pya.Macro(str(technology / f"macros/sg13g2_filler_{group}.lym"))
    original = macro.text
    if "EdgeSeal.holes" not in original:
        raise RuntimeError("PDK fill window changed; review the pinned macro")
    macro.text = original.replace("EdgeSeal.holes", "source.extent")
    if group == "Metal":
        macro.text, count = re.subn(r"('distance_m[1-5]' => )\d+(?:\.\d+)?", r"\g<1>1.0", macro.text)
        if count != 5:
            raise RuntimeError("PDK fill distance configuration changed")
    macro.run()
layout = pya.CellView.active().layout()
layout.write(output_file)
