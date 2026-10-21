# Physical chip assembly and verification

This directory contains a physically verified digital forwarding core and new
padframe assembly/verification tools. It still does **not** contain a complete
1000BASE-T repeater chip. [requirements.md](requirements.md) tracks the missing
physical PHY, complete PCS and chip integration work without substituting empty
macros for those circuits.

Reusable clock alignment and PCS hardware live in `analog/1G/`; process-specific
MOS-biased/trimmed driver schematics live in `analog/1G/IHPSG13G2/`. The alignment,
mapper and scrambler run in native C++, generated RTL and mapped IHP gates with
exact trace comparisons. The driver schematics run in native C++, generated
Verilog-A/ngspice and separate actual-IHP-PSP process-corner experiments.

## Padframe

```bash
cd ~/cpphdl
cmake --build analog/build --target repeater-padframe
```

[padframe.py](padframe.py) imports actual `sg13g2_io.gds` cells and their LEF/CDL
views. It places eight 80 um pads on each side of a 1000 um square ring and four
180 um corners. An additional 140 um inset reserves space around the ring in
the proposed 1280 um die; this is **not** an implemented seal ring.

The 32 pads comprise sixteen analog MDI pads, three digital input pads
(`ref_clock`, `reset`, `enable`), one output pad (`active`), and twelve supply
pads: three each for vdd, vss, iovdd and iovss. Core and I/O supplies are 1.2 V and
3.3 V domains. Pads already include their PDK bond openings and ESD structures.
The analog pads expose protected `padres` terminals toward the core; there are
no extra digital GMII bond pads pretending to be a copper PHY.

The assembly checker rejects gaps, overlaps, absent GDS/CDL/LEF masters,
disconnected supply abutment graphs and intersecting supply pin geometry.
These are assembly checks, not transistor connectivity proof. Real internal
supply and signal connectivity must also pass LVS. The generated padframe
exposes both exterior and core terminals as 44 test ports so it can be verified
independently before integration.

Artifacts in `results/padframe/`:

- `repeater_padframe.gds`: real PDK cells; no fabricated PHY geometry.
- `padframe.cdl` and `lvs.cir`: independently constructed connectivity and PDK
  transistor schematic includes.
- `interface.json`: exact verification interface, including core-side terminals.
- `padframe.json`: placement, pinout, input/output hashes and scope.

The analog pads instantiate the PDK positive/negative clamp cells and secondary
protection; supply pads contain PDK RC-triggered clamps. This establishes the
selected topology, not an ESD rating. HBM/CDM/TLP qualification, IEC surge,
package/magnetics protection, current capacity and latch-up require separate
engineering evidence. No ESD rating is inferred from cell names.

## Full-layout DRC and transistor LVS

[verify_physical.py](verify_physical.py) operates on actual assembled GDS and a
transistor schematic. It runs the complete PDK DRC deck, including density and
antenna, and transistor LVS; reads both databases; checks all expected ports;
and refuses missing, stale, partial or failed evidence. It runs both checks even
when one fails. In particular, a zero runner exit code is insufficient because
the current PDK LVS runner can return zero after reporting a mismatch.

```bash
PDK_PYTHON=analog/pdk/IHPSG13G2/.cpphdl/venv/bin/python
DESIGN=analog/pdk/1G_repeater_IHPSG13G2
"$PDK_PYTHON" "$DESIGN/verify_physical.py" \
    --top RepeaterPadframe \
    --layout "$DESIGN/results/padframe/repeater_padframe.gds" \
    --netlist "$DESIGN/results/padframe/lvs.cir" \
    --interface "$DESIGN/results/padframe/interface.json" \
    --work "$DESIGN/results/padframe-verification"
```

Use a fresh `--work` path for another run. For a completed chip, use
`--top RepeaterChip`; its exact external contract is sixteen MDI pins, four
reference/control/status pins and four supply nets. The root build target
`repeater-chip-verify` expects `results/chip/repeater_chip.gds` and
`results/chip/repeater_chip.cdl`. Missing chip inputs fail and produce a report.
The current `repeater-chip-gds` target continues to reject an incomplete chip.

Passing DRC/LVS establishes those physical checks only. Full-chip post-fill RC
extraction and timing, analog/PVT/noise/mismatch behavior, reset/CDC, package/IR/EM,
physical ESD qualification and cable interoperability are not proved by it.
`physical_checks.json` therefore never silently upgrades a DRC/LVS result into
complete PHY qualification.

## Current evidence

Native/generated-RTL/mapped digital tests and new driver schematic tests pass.
Assembly checks pass with all 36 instances and all 44 expected test ports.
The full padframe verification **fails**: 22 DRC violations in ten density/fill
and antenna groups (`AFil.g`, `AFil.g2`, `GFil.g`, `M1.j`, `M2.j`, `M1Fil.h`,
`M2Fil.h`, `Ant.e_Metal5`, `Ant.e_TopMetal1`, `Ant.e_TopMetal2`), plus transistor
LVS mismatches in I/O/ESD/substrate circuits. Both checks ran, and their databases
and `physical_checks.json` are retained in `results/padframe-verification/`.
These issues must be resolved. There is no DRC-clean/LVS-matched padframe or
completed chip claim.

Primary physical library source:
[IHP SG13G2 I/O library](https://github.com/IHP-GmbH/IHP-Open-PDK/tree/main/ihp-sg13g2/libs.ref/sg13g2_io).
The actual pinned checkout's LEF, GDS and CDL hashes are recorded by the assembly.
