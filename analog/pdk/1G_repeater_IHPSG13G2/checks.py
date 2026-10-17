"""Read real tool evidence; missing, malformed or failing reports never qualify."""
import json
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def check_drc(report):
    root = ET.parse(report).getroot()
    items, categories = root.find("items"), root.find("categories")
    if root.tag != "report-database" or items is None or categories is None or not len(categories):
        raise ValueError("missing/malformed KLayout DRC evidence")
    if len(items):
        rules = sorted({item.findtext("category") for item in items})
        raise ValueError(f"KLayout DRC has {len(items)} violations: {rules}")
    names = {category.findtext("name") for category in categories.iter("category")}
    required = {"M2.d", "M3.d", "Ant.a", "Pin.a", "metal2_drw_Angle45"}
    if len(names) < 800 or not required.issubset(names) or not any("offgrid" in (name or "").lower() for name in names):
        raise ValueError("DRC evidence omits required rule groups")
    # The upstream density deck creates rule categories only on violations.
    # Its database is merged into the full report, then removed by the runner.
    # Require its measured limits even when no density categories were emitted.
    prefix = str(report).removesuffix("_full.lyrdb")
    log = Path(prefix + "_density.log").read_text()
    values = {}
    for material, number, limits in re.findall(r"(Activ|GatPoly|Metal[1-5]|TopMetal[12])\s+Density %\s*=\s*([\d.]+)%\s*\[([^\]]+)\]", log):
        value = float(number)
        for kind, bound in re.findall(r"(Min|Max)\s*=\s*([\d.]+)%", limits):
            if (kind == "Min" and value < float(bound)) or (kind == "Max" and value > float(bound)):
                raise ValueError("density limit failed for " + material)
        values[material] = value
    if len(values) != 9:
        raise ValueError("density evidence omits material layers")
    return dict(violations=0, report=str(report), categories=len(names), density_percent=values)


def check_lvs(report, top_name="RepeaterCore", expected_ports=None, minimum_devices=100):
    import klayout.db as db
    result = db.LayoutVsSchematic()
    result.read(str(report))
    pairs = list(result.xref().each_circuit_pair())
    top = [pair for pair in pairs if pair.first() is not None and pair.first().name == top_name]
    if len(top) != 1 or top[0].second() is None:
        raise ValueError("LVS lacks the requested top circuit")
    if any(pair.status() != db.NetlistCrossReference.Match for pair in pairs):
        raise ValueError("LVS contains unmatched circuits")
    first, second = top[0].first(), top[0].second()
    # Recheck persisted top-port correspondence explicitly. SPICE is case-insensitive.
    # The in-memory comparer used by flag_missing_ports is not preserved by read().
    if expected_ports is None:
        if top_name != "RepeaterCore":
            raise ValueError("LVS requires an explicit interface contract for this top")
        expected_ports = {"VDD", "VSS", "REF_CLOCK", "RESET", "ENABLE_IN", "LINK_A_IN", "LINK_B_IN", "ACTIVE_OUT"}
        expected_ports.update(f"{bus}[{bit}]" for bus in ("RX_A_IN", "RX_B_IN", "TX_A_OUT", "TX_B_OUT") for bit in range(10))
    expected = {pin.upper() for pin in expected_ports}
    if first.pin_count() != len(expected) or second.pin_count() != len(expected):
        raise ValueError("LVS did not verify all signal and supply ports")
    if {pin.name().upper() for pin in first.each_pin()} != expected:
        raise ValueError("layout top-port labels differ from the core interface")
    for pin in result.xref().each_pin_pair(top[0]):
        if pin.first() is None or pin.second() is None or pin.first().name().upper() != pin.second().name().upper():
            raise ValueError("LVS found missing/mislabeled top-level ports")
    def device_count(circuit):
        return len(list(circuit.each_device())) + sum(device_count(child.circuit_ref()) for child in circuit.each_subcircuit())
    devices = device_count(first)
    if devices < minimum_devices:
        raise ValueError("LVS did not include transistor-level schematics")
    return dict(matched_circuits=len(pairs), top_ports=len(expected), instantiated_devices=devices,
                report=str(report))


def check_timing(work):
    measurements = {}
    for kind, field in (("setup", "max"), ("hold", "min")):
        text = (work / f"{kind}.rpt").read_text()
        values = re.findall(r"worst slack " + field + r"\s+([-+\deE.]+)", text)
        if len(values) != 1:
            raise ValueError("missing/ambiguous " + kind + " timing evidence")
        slack = float(values[0])
        if not math.isfinite(slack) or slack < 0:
            raise ValueError(f"{kind} timing failed: {slack} ns")
        measurements[kind + "_slack_ns"] = slack
    electrical = (work / "electrical.rpt").read_text()
    if "VIOLATED" in electrical.upper():
        raise ValueError("slew/capacitance/fanout violation")
    spef = (work / "core.spef").read_text()
    if "*SPEF" not in spef or spef.count("*D_NET") < 40:
        raise ValueError("missing/incomplete routed-wire parasitic extraction")
    measurements["extracted_nets"] = spef.count("*D_NET")
    measurements["basis"] = "OpenRCX nominal routed RC, slow/fast IHP cells; fill coupling not extracted"
    return measurements


def chip_blockers():
    return [
        "Complete Clause 40 PCS beyond the implemented mapper/LFSRs: scrambling/sign expansion, trellis/decoder, state machines, training and auto-negotiation",
        "Physical receive AFE/ADC/DSP and layout/characterization of the MOS-biased/trimmed transmit drivers",
        "Physical receive timing recovery, reset distribution and routed CDC proof for the implemented reference-clock aligner",
        "DRC/LVS-qualified padframe with implemented core routing, seal ring, power distribution, package/magnetics and ESD qualification",
        "Full-chip post-fill extraction, PVT/mismatch/noise, channel/interoperability and physical signoff",
    ]


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("drc", "lvs", "timing"))
    parser.add_argument("path", type=Path)
    args = parser.parse_args()
    result = {"drc": check_drc, "lvs": check_lvs, "timing": check_timing}[args.mode](args.path)
    print(json.dumps(result, indent=2))
