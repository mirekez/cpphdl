# Current 1G shared-die placement

The current GDS contains **879,909 digital cells, 118,019 analog physical leaves,
eight SRAMs and the IHP pad ring on one 11.44 × 11.44 mm die**. Digital PHY,
DSP, PCS, forwarding and control cells share placement rows. Analog leaves
implement all 67,572 elaborated circuit laws, including all eight lanes and
104 receiver slices. The old collection of standalone candidate blocks is no
longer the source of this merged GDS.

- [Current GDSII](repeater_development.gds.gz): open `RepeaterSharedDie`.
- [Full-resolution image](repeater_full_resolution.png).
- [Layer views](repeater_layers.png).
- [Actual inventory and checks](development_provenance.json).

![Shared die](repeater_full_resolution.png)

**Legal placement and physical terminal connectivity are checked. Full-chip
routing, core power distribution, DRC/LVS and electrical qualification remain
unfinished. Timing closure does not gate this as-is PnR pass.** Unresolved
analog controls and digital configuration/clock inputs remain explicit;
this is not a fabrication-ready ASIC.

Large resistors and transistors are expanded into actual series/parallel
physical leaves. Ten explicit 1 GOhm resistors consume 50,000 segments and
much of the die area. Reducing that area requires changing and validating
the circuit/model, not omitting devices from the drawing.

The other per-module images in this folder are historical module views;
they do not define the current full-die inventory.

Regeneration and resumable PnR commands are documented in
[shared-space.md](../../../pdk/1G_repeater_IHPSG13G2/shared-space.md).
Embedded IHP geometry is distributed under [Apache-2.0](IHP-LICENSE.txt).
