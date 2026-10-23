# Physical chip assembly and verification

This directory contains a physically verified digital forwarding core and new
padframe assembly/verification tools. It still does **not** contain a complete
1000BASE-T repeater chip. [requirements.md](requirements.md) tracks the missing
physical PHY, PCS qualification and chip integration work without substituting empty
macros for those circuits.

Reusable clock alignment and PCS hardware live in `analog/1G/`; process-specific
MOS-biased/trimmed driver schematics live in `analog/1G/IHPSG13G2/`. The alignment,
PCS transmit/receive, acquisition and trellis paths run in native C++, generated RTL and mapped IHP gates with
exact trace comparisons. The driver schematics run in native C++, generated
Verilog-A/ngspice and separate actual-IHP-PSP process-corner experiments.

## Padframe

```bash
cd ~/cpphdl
cmake --build analog/build --target repeater-padframe
```

[padframe.py](padframe.py) imports actual `sg13g2_io.gds` cells and their LEF/CDL
views. It places eight 80 um pads on each side of a 1000 um square ring and four
180 um corners. The 1280 um die includes the actual PDK `SG13_dev/sealring`
PCell with a continuous closed EdgeSeal. [sealring.py](sealring.py) checks its
extent and opening. A separate hierarchy contains 32 actual 1.2 V PDK decap
cells and their supply routing. There are 38 top-level physical instances
before fill: 36 pad/corner instances, the seal and the decoupling block.

This is a padframe prototype, not a completed chip floorplan. One mapped receive
PCS already occupies approximately 0.5 mm² of standard-cell area, before receive
DSP, routing or the other port. The 640 × 640 µm central opening cannot fit the
required complete hardware; chip integration must select a larger floorplan.

`padframe.py --core-size N` accepts a square opening of at least 640 whole
micrometers. The 32 functional/supply pads remain present; actual PDK I/O filler
masters close the additional length between them. Abutment dimensions come
from the LEF rather than the numeric cell suffix. The decap row and its east
supply connections move with that edge. Geometry tests include a nonuniform
23 µm expansion, all four supply rings, both decap connections and invalid
size rejection. Five unit tests pass. Expanded rings require fresh DRC/LVS;
these unit checks do not qualify their transistor connectivity or density.

The 32 pads comprise sixteen analog MDI pads, three digital input pads
(`ref_clock`, `reset`, `enable`), one output pad (`active`), and twelve supply
pads: three each for vdd, vss, iovdd and iovss. Core and I/O supplies are 1.2 V and
3.3 V domains. Pads already include their PDK bond openings and ESD structures.
Each analog pad has two distinct core-accessible signal paths. Connect the
high-current PAM5/link-pulse transmitter to the primary `pad` net, which also
reaches the bond pad and primary clamps. Connect the receiver/sense input to
`padres`, exposed here as the corresponding `_core` net. The PDK secondary
protection places a nominal 586.899 Ω resistor between these paths; routing a
transmitter through `padres` would severely attenuate a 100 Ω differential load.
The assembly report lists these connections in `mdi_paths`. There are no extra
digital GMII bond pads substituting for the copper interface. Actual pad
capacitance, clamp loading and transmitter current still require characterization.

The assembly checker rejects gaps, overlaps, absent GDS/CDL/LEF masters,
disconnected supply abutment graphs and intersecting supply pin geometry.
These are assembly checks, not transistor connectivity proof. Real internal
supply and signal connectivity must also pass LVS. The generated padframe
exposes both exterior and core terminals as 44 test ports so it can be verified
independently before integration.

[`decoupling.py`](decoupling.py) connects 32 `sg13g2_decap_8` cells to the
core VDD/VSS ring, using Metal3 for VDD and Metal4 for VSS. Their independent
schematic comes from the PDK standard-cell CDL. This adds physical supply
capacitance and gate area on the otherwise unloaded core supply ring. The nine
earlier antenna markers were on VDD-tied dummy gates in the input level shifters;
the unassembled input pad alone has no antenna violations. The decoupled
padframe's standalone full antenna deck has zero violations, and both the row
and decap cell match their transistor schematics in LVS. These are specific
passing subchecks, not a complete padframe LVS result.

Artifacts in `results/padframe/`:

- `repeater_padframe.gds`: real PDK cells; no fabricated PHY geometry.
- `padframe.cdl` and `lvs.cir`: independently constructed connectivity and PDK
  transistor schematic includes.
- `io_klayout.cdl`: the Cadence CDL in KLayout reader syntax. Tap instances use
  the PDK resistor class. The chip-internal substrate is threaded through child
  terminals, including resistor bulk annotations, without adding an external
  pin. Device dimensions remain unchanged; graph tests cover the translation.
- `interface.json`: exact verification interface, including core-side terminals.
- `padframe.json`: placement, pinout, input/output hashes and scope.

`repeater-padframe-filled` runs the PDK active/poly/metal fill generators around
the unchanged I/O macros and actual seal ring. [fill_padframe.py](fill_padframe.py)
uses a compact proxy with conservative no-fill extents around I/O and decoupling
so repeated geometry
does not need to be flattened during fill. Only filler-purpose geometry is
merged into the actual layout; proxy exclusion markers never reach the DRC
input. Tests check the macro coverage and preserved instances.

`repeater-padframe-verify` builds this filled layout and runs both full PDK
checks. Its verification directory must be fresh, as for the command below.

The analog pads instantiate the PDK positive/negative clamp cells and secondary
protection; supply pads contain PDK RC-triggered clamps. This establishes the
selected topology, not an ESD rating. HBM/CDM/TLP qualification, IEC surge,
package/magnetics protection, current capacity and latch-up require separate
engineering evidence. No ESD rating is inferred from cell names.

## Full-layout DRC and transistor LVS

[verify_physical.py](verify_physical.py) operates on actual assembled GDS and a
transistor schematic. It runs the complete PDK DRC deck, including density and
antenna, and transistor LVS; reads both databases; checks all expected ports;
and refuses missing, stale, partial or failed evidence. It checks completion of
all 37 main rule tables plus density and maximum rules. The PDK parallel runner
can report success after a worker crashes; a zero-violation partial database is
therefore insufficient. It runs both checks even
when one fails. In particular, a zero runner exit code is insufficient because
the current PDK LVS runner can return zero after reporting a mismatch.

```bash
PDK_PYTHON=analog/pdk/IHPSG13G2/.cpphdl/venv/bin/python
DESIGN=analog/pdk/1G_repeater_IHPSG13G2
"$PDK_PYTHON" "$DESIGN/verify_physical.py" \
    --top RepeaterPadframe \
    --layout "$DESIGN/results/padframe/repeater_padframe_filled.gds" \
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
`--workers 1` is the default for physical verification, to limit memory use;
`--workers 2` enables parallel DRC on a workstation with sufficient memory.

Passing DRC/LVS establishes those physical checks only. Full-chip post-fill RC
extraction and timing, analog/PVT/noise/mismatch behavior, reset/CDC, package/IR/EM,
physical ESD qualification and cable interoperability are not proved by it.
`physical_checks.json` therefore never silently upgrades a DRC/LVS result into
complete PHY qualification.

## Current evidence

Native/generated-RTL/mapped PCS tests and the driver schematic tests pass.
Assembly checks pass with 36 pad/corner instances, the seal ring and all 44 test
ports. The complete check of the sealed, filled padframe on 2026-10-18 still
**fails**: nine antenna violations, three each in `Ant.e_Metal5`,
`Ant.e_TopMetal1` and `Ant.e_TopMetal2`, and unresolved transistor LVS. All nine
density materials pass: active 44.25%, poly 15.10%, Metal1–5
46.93/35.83/57.03/56.94/57.01%, TopMetal1/2 51.38/42.94%.
The historical full database and `physical_checks.json` are retained in
`results/sealed-padframe-feol-verification/`. This run used the original I/O CDL;
the later dialect adapter also fails cached-extraction comparisons and has not
qualified the layout.

The antenna markers are on the three input-pad level-down circuits. The LVS
diagnostic additionally finds reference/extracted tap perimeter discrepancies:
for example the DCN diode guard tap has reference A=141.253 µm², P=47.54 µm
versus extracted A=141.2964 µm², P=221.76 µm. Converting CDL syntax cannot repair
this geometric discrepancy. The PDK has an [open discussion of tap checking
and overlapping taps](https://github.com/IHP-GmbH/IHP-Open-PDK/issues/955).
Those historical runs used explicit geometry-checked tap devices. A separate
common-ground prototype now supports the PDK's documented ideal-ohmic-contact
model; its selection and independent source preparation are recorded below.

The newer decoupled padframe passes complete serial DRC: **zero violations,
869 categories, all 37 main tables, maximum rules, antenna and all nine density
materials**. Density is active 44.19%, poly 15.07%, Metal1–5
46.79/35.68/56.89/56.79/56.86%, TopMetal1/2 51.27/42.83%.
Its GDS SHA-256 is
`37bb427fd47f63723145aa2ab7d24dba76a6876fa17afeb2e9460e7ff852d44e`.
The databases and measured completion evidence are in
`results/decoupled-padframe-serial-verification/physical_checks.json`.
The first parallel run lost its maximum-rule worker to a memory limit; the
wrapper rejected that partial database despite the PDK runner printing “passed”.
Full transistor LVS of the I/O hierarchy remains unresolved. DRC completion
establishes the physical rule check for this padframe; the completed chip still
needs matching LVS, the PHY circuits and chip integration.

Primary physical library source:
[IHP SG13G2 I/O library](https://github.com/IHP-GmbH/IHP-Open-PDK/tree/main/ihp-sg13g2/libs.ref/sg13g2_io).
The actual pinned checkout's LEF, GDS and CDL hashes are recorded by the assembly.

The separate `results/common_ground_padframe/` prototype joins VSS and IOVSS
through four actual corner metal straps. It exposes 43 test ports and passes
the same complete DRC contract: zero violations in 869 categories, all main and
maximum tables, antenna and all nine density materials. Its filled GDS hash is
`14928a527a2bb32e9d53da9bdefb62a1dc4285a8a94927d0f2412787939637b3`.
Its original flat LVS attempt failed. Fresh deep extraction against the corrected
independent schematic passes: all 43 top ports and 246 reduced devices match.
The one matched circuit is the independently flattened top; hierarchy was
retained during geometry extraction to keep child-cell labels local. The report
is `results/common_ground_padframe/verification_deep/physical_checks.json`.
The recorded DRC pass is explicitly reused for the identical GDS and rules;
LVS is a fresh extraction and comparison.

`--ideal-tap-contacts` selects the PDK's documented ideal ohmic tie-to-well model
on both layout and schematic sides. [`ideal_taps.rb`](ideal_taps.rb) prepares
the schematic without reading geometry, preserves every non-tap device, and
checks a SPICE write/read round trip for exact terminal connectivity and device
parameters. The common-ground source passes this guard for 197 contacts and
1,577 other devices. This does not itself establish LVS. The default verification
mode retains the original geometry-checked tap devices.

`--drc-evidence PATH` can reuse a complete DRC pass while rerunning LVS. It
requires identical GDS, top, PDK revision and rule/checker hashes, then reparses
the actual database, completion logs and measured density values. Reuse is
recorded explicitly with hashes of the original evidence and all raw reports;
it cannot reuse a failed or partial DRC check. Verification also checks that its
inputs did not change during the run.
