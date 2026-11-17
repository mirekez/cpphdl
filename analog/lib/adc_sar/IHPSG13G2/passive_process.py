"""Explicit IHP MOS, R3, and MIM corner binding for a physical SAR netlist.

This companion lowering keeps the active nominal-only process.py source stable
while long content-hashed campaigns finish. It creates an independent binding
whose resistor and capacitor corners are explicit in the receipt.
"""

import hashlib
import json
import math
from pathlib import Path

import process
import parameters
from extract_analog import digest
from process import mim, rppd, statements

RES_CORNERS = ("res_bcs", "res_typ", "res_wcs")
CAP_CORNERS = ("cap_bcs", "cap_typ", "cap_wcs")


def lower(text, pdk, work, temp=27, corner="mos_tt", observations=None,
          res_corner="res_typ", cap_corner="cap_typ"):
    if res_corner not in RES_CORNERS or cap_corner not in CAP_CORNERS:
        raise ValueError("unknown deterministic R3 or MIM process corner")
    if not math.isfinite(temp):
        raise ValueError("nonfinite process temperature")
    work.mkdir(parents=True, exist_ok=True)
    rows = [f"TEMP {temp:.17g}"]
    hashes, bundles, counts = {}, {}, {}
    directory = pdk / "ihp-sg13g2/libs.tech/ngspice/models"
    hashes.update({str(p): digest(p) for p in
                   (directory / "cornerCAP.lib", directory / "capacitors_mod.lib")})
    for name, node in (observations or {}).items():
        rows.append(f"OBS {name} {node}")
    for line in statements(text):
        fields = line.split()
        name = fields[0]
        if name.lower() in (".subckt", ".ends"):
            continue
        if name[0] in "RC":
            if len(fields) not in (4, 5) or (len(fields) == 5 and
                    fields[4].lower() not in ("ic=0", "ic=0.0")):
                raise ValueError("unsupported passive syntax")
            value = parameters.expression(fields[3], {})
            if name[0] == "C" and value == 0:
                counts["zero_capacitors_omitted"] = counts.get("zero_capacitors_omitted", 0) + 1
                continue
            if not value > 0:
                raise ValueError("nonpositive passive")
            rows.append(f"{name[0]} {name} {fields[1]} {fields[2]} {value:.17g}")
            counts[name[0]] = counts.get(name[0], 0) + 1
            continue
        if not name.startswith("X"):
            raise ValueError("unsupported physical statement: " + line)
        model_index = next((i for i, value in enumerate(fields) if value in
                            ("cap_cmim", "rppd", "sg13_hv_nmos", "sg13_hv_pmos",
                             "sg13_lv_nmos", "sg13_lv_pmos")), None)
        if model_index is None:
            raise ValueError("unbound process device")
        nodes = fields[1:model_index]
        model = fields[model_index]
        geometry = parameters.assignments(" ".join(fields[model_index+1:]), {})
        if model == "cap_cmim":
            if len(nodes) != 2 or not set(geometry) <= {"w", "l", "ic"}:
                raise ValueError("unsupported MIM")
            value = mim(pdk, geometry, temp, cap_corner)
            rows.extend((f"R {name}_access {nodes[0]} {name}_plate .055",
                         f"C {name}_storage {name}_plate {nodes[1]} {value:.17g}"))
            counts["mim"] = counts.get("mim", 0) + 1
            continue
        if model == "rppd":
            if len(nodes) != 3 or not set(geometry) <= {"w", "l", "a", "p", "rz"}:
                raise ValueError("unsupported physical resistor")
            bundle = rppd(pdk, geometry, res_corner)
            nodes = [nodes[0], nodes[2], nodes[1], name + "_thermal"]
        else:
            if len(nodes) != 4 or not set(geometry) <= {"w", "l", "ng", "as", "ad", "ps", "pd"}:
                raise ValueError("unsupported MOS geometry")
            family, polarity = model.split("_")[1:]
            fingers = int(geometry.get("ng", 1))
            if fingers != geometry.get("ng", 1) or fingers < 1:
                raise ValueError("nonintegral MOS finger count")
            bundle = parameters.psp(pdk, corner, polarity[0],
                                    float(f"{geometry['w']:.12g}"),
                                    float(f"{geometry['l']:.12g}"), family, fingers)
            junction = {key: geometry[key] for key in ("as", "ad", "ps", "pd") if key in geometry}
            if junction:
                if set(junction) != {"as", "ad", "ps", "pd"} or junction["as"] <= 0:
                    raise ValueError("incomplete/zero extracted source junction; restore from GDS first")
                bundle["instance"].update(junction)
        hashes.update(bundle["input_sha256"])
        key = json.dumps([bundle["module"], bundle["model"], bundle["instance"]], sort_keys=True)
        if key not in bundles:
            path = work / f"model_{len(bundles)}.txt"
            parameters.write(bundle, path)
            path.write_text("binary " + json.dumps(bundle["binary"]) + "\n" + path.read_text())
            bundles[key] = path
        rows.append("M " + name + " " + " ".join(nodes) + " " +
                    json.dumps(str(bundles[key].resolve())))
        counts[model] = counts.get(model, 0) + 1
    (work / "circuit.txt").write_text("\n".join(rows) + "\n")
    receipt = dict(corner=corner, temperature_C=temp, res_corner=res_corner,
                   cap_corner=cap_corner, counts=counts,
                   input_netlist_sha256=hashlib.sha256(text.encode()).hexdigest(),
                   process_sha256=hashes,
                   source_sha256={str(Path(__file__).resolve()): digest(__file__),
                                  str(Path(process.__file__).resolve()): digest(process.__file__),
                                  str(Path(parameters.__file__).resolve()): digest(parameters.__file__)})
    (work / "lowering.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt
