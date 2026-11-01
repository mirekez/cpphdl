# 1G repeater ASIC development artifacts

The October 28 update adds the receiver-reference storage-access layout and two
views rendered from its actual GDSII masks. Its local geometry and LVS checks
pass; electrical qualification remains open. **The integrated 1000BASE-T
repeater ASIC is unfinished and is not released for fabrication.**

The digital forwarding core, separate unpopulated padframe, and their four
images are retained October 24 snapshots. Their verification and timing figures
below describe that historical revision, not the current integrated design.
None of these three standalone layouts contains a complete repeater.

Only selected layouts, images, provenance, documentation and the upstream
license are committed here. Raw traces and verification databases stay in the
ignored build directory.

## Files

| File | Content |
| --- | --- |
| [receive_reference.gds.gz](receive_reference.gds.gz) | October 28 receiver reference with direct storage-plate and ground access; 1846.23 × 321.47 µm |
| [receive_reference_layers.png](receive_reference_layers.png) | Whole macro, lower devices/routing, and upper routing masks |
| [receive_reference_storage.png](receive_reference_storage.png) | Divider and output storage connection detail |
| [receive_reference_provenance.json](receive_reference_provenance.json) | Current macro hashes, geometry evidence, source fingerprints and qualification limits |
| [repeater_core.gds](repeater_core.gds) | Filled, routed `RepeaterCore`, 120 × 120 µm; 1 nm database unit |
| [repeater_padframe.gds.gz](repeater_padframe.gds.gz) | Losslessly compressed `RepeaterPadframe`, 1280 × 1280 µm; independent of the core |
| [core_layers.png](core_layers.png) | Six views of actual core masks, with scale bars |
| [padframe.png](padframe.png) | Actual padframe geometry and its unpopulated opening |
| [forwarding_waveforms.png](forwarding_waveforms.png) | Selected samples from the fresh digital forwarding capture |
| [receive_dsp_pcs.png](receive_dsp_pcs.png) | Receiver training, pair acquisition, PCS lock and byte-valid traces for all four functional cases |
| [provenance.json](provenance.json) | Artifact hashes, source/PDK revisions, dimensions, pinout, recorded verification summaries and limitations |
| [IHP-LICENSE.txt](IHP-LICENSE.txt) | Apache-2.0 license for the embedded IHP PDK geometry |

## Receiver reference: October 28

![Receiver-reference masks](receive_reference_layers.png)

![Storage connections](receive_reference_storage.png)

The `ReceiveCommonReference` macro retains the original devices and intentional
capacitors. Direct upper-metal access connects the 16 output MIM plates (80 pF)
and two divider MIM plates (10 pF), with additional ground-return connections.
The footprint is **0.5935 mm²**. The maintained layout test passed with 46
matched LVS circuits, three top ports and 81 instantiated devices.

The local DRC check found zero non-density violations. Minimum-density rules
`AFil.g`, `GFil.g`, `M1.j`, `M2.j`, `M3.j`, `M4.j`, `M5.j`, `TM1.c` and `TM2.c`
remain for fill during chip integration. This is an unfilled standalone macro;
these checks do not establish full-chip DRC or electrical acceptance. The
extracted transient for the storage-access repair is still pending.

The packager verifies source and artifact hashes against the completed geometry
receipt and checks lossless GDS compression before publishing. Its separate
provenance preserves the older core/padframe package history.

## Historical October 24 snapshots

The remaining layout, timing and functional evidence below belongs to the
October 24 package. Consult [qualification-status.md](../qualification-status.md)
for subsequent implementation work and unresolved checks.

## Digital forwarding core

![Core mask layers](core_layers.png)

The C++RTL source is
[`RepeaterCore`](../../../pdk/1G_repeater_IHPSG13G2/repeater.hpp).
An external 125 MHz reference clocks both directions. Each input word contains
eight data bits, a valid bit and an error bit. The core registers A's received
word onto B's transmit interface and vice versa when both links and the enable
input are asserted. Reset or a lost link suppresses forwarding. Input streams
must already be aligned to the reference clock by the PHYs.

The block uses a nominal 1.2 V supply and contains 21 mapped state bits. Its
120 × 120 µm footprint is **0.0144 mm²**, not the area of a complete repeater.
It has no copper receivers, transmitters, pads, seal ring or integrated PHYs.

The masks are rendered with KLayout. Panels 01–05 show drawing-purpose geometry;
panel 06 also shows TopMetal1/TopMetal2 filler. Some upper layers serve only fill
in this core. Colors identify layers rather than physical material appearance.
Hidden layers and filler remain in the unchanged GDSII. The panel titles and
`visualization.palette` in the provenance identify the selected GDS layers.

## Standalone padframe

![Standalone padframe](padframe.png)

The common-ground padframe contains 32 pads, four corners, the PDK seal ring and
32 decoupling cells. Sixteen pads serve the planned two four-pair MDI ports;
four serve reference clock, reset, enable and active status; twelve are supply
pads. Core power is 1.2 V, I/O power is 3.3 V, and VSS/I/O ground are joined.
The package provenance records the individual placements and exposed ports.

The 640 × 640 µm central opening has no integrated forwarding core or PHYs.
This opening is too small for the complete receive hardware already estimated
in the project. The **1.6384 mm²** padframe footprint is a prototype dimension,
not a qualified full-chip die size. Its existing ESD structures do not establish
an HBM/CDM or system surge rating. The image hides filler to expose the ring;
the GDS retains it.

## Recorded digital behavior

![Recorded forwarding samples](forwarding_waveforms.png)

The first panel displays the first 24 captured cycles as packed 10-bit hex
words. The second displays the final 24 cycles of the recorded control sequence.
The source capture contains 20,513 rows. Each row records inputs before a clock
edge and registered outputs after that same edge. Equal horizontal positions
therefore do not mean zero-delay combinational forwarding. Lines connect saved
samples; they do not represent measured propagation delay or analog voltage.
This is a digital core illustration, not a PAM5 waveform or eye diagram.

## Evidence and release boundary

The October 24 forwarding core passed native C++, generated RTL, mapped-gate
and routed-gate checks, full-deck KLayout DRC (zero violations) and deep
transistor LVS (48 top-level ports). Routed-RC setup/hold slack is
**+5.95388031 / +0.787586033 ns**. Timing uses nominal extracted routing with
slow/fast IHP cell corners; post-fill coupling and full-chip/package effects
were not extracted.

The October 24 assembled and filled padframe passed full-deck KLayout DRC
(zero violations). Deep transistor LVS passes with 43 top-level ports using
the PDK's documented **ideal ohmic tap-contact model**, applied independently
to the schematic and layout. Default extracted-tap LVS failed with unmatched
circuits; that diagnostic receipt is retained in the provenance. The ideal
mode preserves non-tap devices and their parameters, but does not validate tap
area/perimeter parameters. This is the same model used by the earlier passing
padframe package; see [the tap-model explanation](../../../pdk/1G_repeater_IHPSG13G2/chip.md).
Neither standalone check verifies an integrated repeater.

![Receive DSP and PCS functional traces](receive_dsp_pcs.png)

The October 24 [ReceiveDSP and ReceiveDSPPCS](../../receive_dsp.md) sources, generated
RTL, independent reference models and CTest tests accompany this package.
Native C++, generated RTL and IHP gate simulation each pass the same independent
checks and match exactly: four composed cases, eight frames and 6,392 bytes.
The image shows training, acquisition and decoded-data availability from the
checked trace. It does not assert an achieved clock frequency.

That receiver revision failed the requested 4 ns cell-timing limit: DSP **−2.13 ns**,
DSP + PCS **−3.13 ns**. Its mapped cell area is **2.550 mm² per receive path**,
before routing and analog integration. External supervised training targets,
recovered-symbol samples and aligned local transmit references remain required.
The timing tests intentionally retain their failing constraint.

`provenance.json` records the fresh physical receipts, the existing receiver
functional/timing receipts with inputs rechecked, and hashes of the shipped
files. `source_base_commit` identifies the parent revision; the accompanying
source changes and exact input hashes identify the worktree used for generation.
This compact package is not a complete signoff archive.

A complete product still requires physical receive AFE/ADC/DSP implementation,
qualified continuous PCS/training and role negotiation, transmit-driver layout,
receive timing recovery and clock/reset/CDC integration, a suitably sized and
routed chip, full-chip extraction, and package/channel/interoperability and
physical qualification. See the project's
[implementation requirements](../../../pdk/1G_repeater_IHPSG13G2/requirements.md).

## Viewing

Open a layout in KLayout; the compressed GDS is directly readable:

```sh
klayout receive_reference.gds.gz
klayout repeater_core.gds
klayout repeater_padframe.gds.gz
```

Select the named top cell and expand its hierarchy. If the IHP PDK is installed,
load `ihp-sg13g2/libs.tech/klayout/tech/sg13g2.lyp` for the full technology layer
palette. Alternatively, `gzip -dk repeater_padframe.gds.gz` creates an ordinary
GDS file. Neither viewing nor decompression runs verification.

The six PNGs can be viewed directly without the PDK or EDA tools. Their colors
are a simplified display palette; dimensions and shapes come from the GDS.

## Attribution

The GDS hierarchies embed standard-cell, I/O and seal-ring geometry from the
[IHP Open PDK](https://github.com/IHP-GmbH/IHP-Open-PDK), revision
`89d42ee6d2fd9b50b86a1aada6fee6673c3e6008`. Copyright IHP PDK Authors;
distributed under [Apache-2.0](IHP-LICENSE.txt). The custom circuit arrangement,
routing and package illustrations are cpphdl project work. The original PDK
cell geometry is preserved in the generated layouts.

## Reproducing the receiver update

From `analog/`, use an IHP-enabled CMake build configured as described in
[analog-extraction.md](../analog-extraction.md). The Python environment needs
KLayout and Pillow. Run the maintained geometry test, then package its receipt:

```sh
ctest --test-dir build/regeneration-audit -R '^analog\.1g\.ihp_receive_common_reference\.storage_layout$' --output-on-failure
pdk/IHPSG13G2/.cpphdl/venv/bin/python pdk/1G_repeater_IHPSG13G2/package_receive_results.py --layout-evidence build/regeneration-audit/1G/IHPSG13G2/tests/results/receive_common_storage_layout/checks.json
```

If Pillow is installed separately, provide its directory through `PYTHONPATH`
(the local render environment uses `PYTHONPATH=out/pdf-renderer`). Packaging
reads completed verification evidence and does not run a simulation. Its
source-hash checks reject stale evidence.

## Reproducing the historical core/padframe package

From `analog/`, with the local IHP PDK, toolchain and root converter installed:

```sh
python3 pdk/1G_repeater_IHPSG13G2/flow.py --target core --work build/asic-refresh/core
pdk/IHPSG13G2/.cpphdl/venv/bin/python pdk/1G_repeater_IHPSG13G2/padframe.py --common-ground --work build/asic-refresh/padframe
pdk/IHPSG13G2/.cpphdl/venv/bin/python pdk/1G_repeater_IHPSG13G2/fill_padframe.py --layout build/asic-refresh/padframe/repeater_padframe.gds --output build/asic-refresh/padframe/repeater_padframe_filled.gds --work build/asic-refresh/padframe/fill
pdk/IHPSG13G2/.cpphdl/venv/bin/python pdk/1G_repeater_IHPSG13G2/verify_physical.py --layout build/asic-refresh/padframe/repeater_padframe_filled.gds --netlist build/asic-refresh/padframe/lvs.cir --interface build/asic-refresh/padframe/interface.json --top RepeaterPadframe --work build/asic-refresh/padframe/verification
# The default extracted-tap LVS run above records a mismatch. Reuse its passing
# DRC only after the verification runner checks identical layout and rule hashes.
pdk/IHPSG13G2/.cpphdl/venv/bin/python pdk/1G_repeater_IHPSG13G2/verify_physical.py --layout build/asic-refresh/padframe/repeater_padframe_filled.gds --netlist build/asic-refresh/padframe/lvs.cir --interface build/asic-refresh/padframe/interface.json --top RepeaterPadframe --work build/asic-refresh/padframe/verification_ideal --ideal-tap-contacts --drc-evidence build/asic-refresh/padframe/verification/physical_checks.json
pdk/IHPSG13G2/.cpphdl/venv/bin/python pdk/1G_repeater_IHPSG13G2/package_results.py --work build/asic-refresh
```

The first command needs `verilator` on `PATH`. The final renderer needs KLayout,
Pillow and matplotlib in its Python environment (or on `PYTHONPATH`). It also
requires receiver functional, mapped-gate and timing receipts in
`build/1G/tests/hardware`; reproduce those with the CTests documented in
[receive_dsp.md](../../receive_dsp.md). Timing failures remain recorded failures.
The packager rejects changed verification inputs or layout bytes and does not
run tests itself. It replaces the artifacts in this directory only after its
input checks pass.
