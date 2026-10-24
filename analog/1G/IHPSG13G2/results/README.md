# 1G repeater ASIC development artifacts

This package contains the existing IHP SG13G2 digital forwarding core and a
**separate, unpopulated padframe prototype**. These are real physical layouts.
**The integrated 1000BASE-T repeater ASIC is unfinished and is not released for
fabrication.** No new simulation, regression, DRC or LVS was run for this package.

Only the final layouts, three images, provenance and the upstream license are
retained here. The bulk test results and raw waveform capture were deleted at
the user's request. The images were generated from the saved layouts and capture
before that cleanup; no circuit geometry or waveform samples were invented.

## Files

| File | Content |
| --- | --- |
| [repeater_core.gds](repeater_core.gds) | Filled, routed `RepeaterCore`, 120 × 120 µm; 1 nm database unit |
| [repeater_padframe.gds.gz](repeater_padframe.gds.gz) | Losslessly compressed `RepeaterPadframe`, 1280 × 1280 µm; independent of the core |
| [core_layers.png](core_layers.png) | Six views of actual core masks, with scale bars |
| [padframe.png](padframe.png) | Actual padframe geometry and its unpopulated opening |
| [forwarding_waveforms.png](forwarding_waveforms.png) | Selected samples from the previously recorded digital forwarding capture |
| [provenance.json](provenance.json) | Artifact hashes, source/PDK revisions, dimensions, pinout, recorded verification summaries and limitations |
| [IHP-LICENSE.txt](IHP-LICENSE.txt) | Apache-2.0 license for the embedded IHP PDK geometry |

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

The packaged core bytes match the historical successful flow receipt, and its
recorded input hashes matched the available inputs when packaged. That run
recorded zero full-deck DRC violations, matched transistor LVS with 48 top-level
ports, and routed-RC setup/hold slack of **+5.95388031 / +0.787586033 ns**. Timing
used nominal extracted routing with slow/fast IHP cell corners; post-fill
coupling and full-chip/package effects were not extracted.

The padframe bytes match its historical standalone passing DRC/deep-LVS receipt
(zero DRC violations, 43 top-level ports). Two verification scripts,
`checks.py` and `tool_process.py`, changed after that run. This package preserves
the historical result and identifies the changed hashes; it does not claim a
fresh qualification. Raw verification databases were removed during cleanup,
so the retained summaries alone are not a signoff archive.

A complete product still requires physical receive AFE/ADC/DSP implementation,
qualified continuous PCS/training and role negotiation, transmit-driver layout,
receive timing recovery and clock/reset/CDC integration, a suitably sized and
routed chip, full-chip extraction, and package/channel/interoperability and
physical qualification. See the project's
[implementation requirements](../../../pdk/1G_repeater_IHPSG13G2/requirements.md).

## Viewing

Open either layout in KLayout; the compressed GDS is directly readable:

```sh
klayout repeater_core.gds
klayout repeater_padframe.gds.gz
```

Select the named top cell and expand its hierarchy. If the IHP PDK is installed,
load `ihp-sg13g2/libs.tech/klayout/tech/sg13g2.lyp` for the full technology layer
palette. Alternatively, `gzip -dk repeater_padframe.gds.gz` creates an ordinary
GDS file. Neither viewing nor decompression runs verification.

The three PNGs can be viewed directly without the PDK or EDA tools. Their colors
are a simplified display palette; dimensions and shapes come from the GDS.

## Attribution

The GDS hierarchies embed standard-cell, I/O and seal-ring geometry from the
[IHP Open PDK](https://github.com/IHP-GmbH/IHP-Open-PDK), revision
`89d42ee6d2fd9b50b86a1aada6fee6673c3e6008`. Copyright IHP PDK Authors;
distributed under [Apache-2.0](IHP-LICENSE.txt). The custom circuit arrangement,
routing and package illustrations are cpphdl project work. The original PDK
cell geometry in these existing layouts was not edited during packaging.
