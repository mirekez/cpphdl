"""Run the pinned IHP fill patterns inside a standalone core's extent.

Invoked in the KLayout application, with the unfilled GDS open. Upstream fill
macros select EdgeSeal.holes, which is empty for a core without a chip sealring.
The fill window is adapted and the PDK's configurable Metal1-5 fill distances
are set to 1 um (above its 0.42 um minimum). Larger receive modules use 0.6 um
for Metal2/3 to meet their measured 35% minimum density. The normalizer uses
0.42 um Metal2 spacing after a routed revision missed the 35% density minimum.
The compact receive filters also use 0.42 um Metal2 spacing after their legal
remainder fill reached only 33.45%, below the PDK's 35% minimum.
Their routed Metal4 needs remainder fill too: the pinned pattern grid alone
reached 34.822%, below the same minimum.
Exclusion rules are unchanged.
The sparse reference PLL uses 1 um on every metal layer: the digital receive
module's 0.6 um setting exceeded the 60% Metal2/3 maximum in its open regions.
This does not add a sealring or claim a complete die.
"""
import os
import gc
import re
import sys
from pathlib import Path
import pya

technology = Path(os.environ["REPEATER_PDK"]) / "ihp-sg13g2/libs.tech/klayout/tech"
for group in ("ActGatP", "Metal", "TopMetal"):
    print("PDK fill: " + group, flush=True)
    macro = pya.Macro(str(technology / f"macros/sg13g2_filler_{group}.lym"))
    original = macro.text
    if "EdgeSeal.holes" not in original:
        raise RuntimeError("PDK fill window changed; review the pinned macro")
    macro.text = original.replace("EdgeSeal.holes", "source.extent")
    if group == "ActGatP":
        # Keep repeated-cell exclusion geometry hierarchical until the final
        # fill mask. KLayout's fill operation itself requires a flat layer.
        if macro.text.count(".fill(") != 2:
            raise RuntimeError("PDK Active/Poly fill calls changed")
        macro.text = "deep\n" + macro.text.replace(".fill(", ".flatten.fill(")
    if group == "Metal":
        receive_module = os.environ.get("REPEATER_TOP", "RepeaterCore") not in ("RepeaterCore", "ReferenceClockGenerator")
        def spacing(match):
            distance = 0.6 if receive_module and match[2] in ("2", "3") else 1.0
            if os.environ.get("REPEATER_TOP") in ("ReceiveSampleNormalizer", "ADCReceiveController",
                                                 "ReceiveFilter", "PipelinedReceiveFilter", "InterleavedSARBankController", "ADCReceiveLaneController") and match[2] == "2":
                distance = 0.42
            return match[1] + str(distance)
        macro.text, count = re.subn(r"('distance_m([1-5])' => )\d+(?:\.\d+)?", spacing, macro.text)
        if count != 5:
            raise RuntimeError("PDK fill distance configuration changed")
    macro.run()
    del macro
    gc.collect()
    print("PDK fill complete: " + group, flush=True)
layout = pya.CellView.active().layout()
if os.environ.get("REPEATER_TOP") in ("ReceiveSampleNormalizer", "ADCReceiveController",
                                      "ReceiveFilter", "PipelinedReceiveFilter", "InterleavedSARBankController", "ADCReceiveLaneController"):
    sys.path.insert(0, str(Path(os.environ.get("REPEATER_SOURCE", Path(__file__).resolve().parent))))
    from fill_remainder import add_metal2_remainder, add_metal3_remainder, add_metal4_remainder
    print("Metal2 remainder fill: " + str(add_metal2_remainder(layout, layout.top_cell())), flush=True)
    print("Metal3 remainder fill: " + str(add_metal3_remainder(layout, layout.top_cell())), flush=True)
    if os.environ.get("REPEATER_TOP") in ("ReceiveFilter", "PipelinedReceiveFilter", "InterleavedSARBankController", "ADCReceiveLaneController"):
        print("Metal4 remainder fill: " + str(add_metal4_remainder(layout, layout.top_cell())), flush=True)
layout.write(output_file)
