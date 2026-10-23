#!/usr/bin/env python3
"""Select PDK fixed vias whose intermediate metal meets its minimum area.

The router otherwise emits isolated intermediate landings in stacked vias that
pass its checks but fail IHP M2.d/M3.d. Keep the original Via1 pin-access choices:
their upper Metal2 connects to a route or the larger Via2 landing. Filtering
these choices prevents legal access to the tightly spaced sg13g2_or2_1 pins.
Filter undersized upper-stack alternatives from a local LEF copy and add area-safe
single-cut variants with square and oriented rectangular metal landings. Their
0.19 um cut and 20 Ohm resistance follow the PDK's Via2_s/Via3_s/Via4_s.
The installed PDK and all rule limits remain unchanged.
"""
import argparse
import json
from pathlib import Path
import re


def prepare(pdk, work):
    original = pdk / "ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_tech.lef"
    text = original.read_text()
    rules = {}
    for match in re.finditer(r"^LAYER\s+(\w+)\s*\n(.*?)^END\s+\1\b", text, re.M | re.S):
        name, body = match.groups()
        area = re.search(r"\bAREA\s+([\d.]+)\s*;", body)
        direction = re.search(r"DIRECTION\s+(\w+)", body)
        if name in ("Metal2", "Metal3", "Metal4", "Metal5") and area and direction:
            rules[name] = (float(area[1]), direction[1])
    if len(rules) != 4:
        raise ValueError("PDK routing-layer rules changed; review the adapter")
    removed, kept = [], []

    def via(match):
        name = match[1]
        if name.startswith("Via1_"):
            kept.append(name)
            return match[0]
        for layer, coords in re.findall(r"LAYER\s+(\w+)\s*;\s*\n\s*RECT\s+([-\d.\s]+)\s*;", match[0]):
            if layer in rules:
                left, bottom, right, top = map(float, coords.split())
                area = (right - left) * (top - bottom)
                if area + 1e-12 < rules[layer][0]:
                    removed.append(name)
                    return "# Excluded undersized intermediate via landing: " + name
        kept.append(name)
        return match[0]

    text = re.sub(r"^(?:VIA|Via)\s+(\S+)[^\n]*\n.*?^[ \t]*END\s+\1\b", via, text, flags=re.M | re.S)
    if not removed or not kept:
        raise ValueError("PDK fixed-via catalog changed; review the adapter")
    added = []
    shapes = {"S": (-.19, -.19, .19, .19), "H": (-.36, -.1, .36, .1), "V": (-.1, -.36, .1, .36)}
    for index in (2, 3, 4):
        if .144 < max(rules[f"Metal{index}"][0], rules[f"Metal{index + 1}"][0]):
            raise ValueError("PDK minimum area exceeds the derived via landing")
        for below, above in (("S", "S"), ("H", "H"), ("H", "V"), ("V", "H"), ("V", "V")):
            name = f"Via{index}_CoreArea{below}{above}"
            lower = " ".join(f"{v:.3f}" for v in shapes[below])
            upper = " ".join(f"{v:.3f}" for v in shapes[above])
            added.append(name)
            via_text = (f"VIA {name} DEFAULT\n  RESISTANCE 20.0 ;\n"
                        f"  LAYER Metal{index} ;\n    RECT {lower} ;\n"
                        f"  LAYER Via{index} ;\n    RECT -0.095 -0.095 0.095 0.095 ;\n"
                        f"  LAYER Metal{index + 1} ;\n    RECT {upper} ;\nEND {name}\n")
            text = text.replace("END LIBRARY", via_text + "\nEND LIBRARY")
    work.mkdir(parents=True, exist_ok=True)
    (work / "route_tech.lef").write_text(text)
    (work / "route_tech.json").write_text(json.dumps(dict(excluded_vias=removed, retained_vias=kept, derived_vias=added), indent=2) + "\n")
    print("Retained", len(kept), "PDK vias, added", len(added), "area-safe variants; excluded", len(removed), "undersized alternatives")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pdk", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.pdk.resolve(), args.work.resolve())
