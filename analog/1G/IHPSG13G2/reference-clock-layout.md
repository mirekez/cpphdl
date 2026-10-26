# Physical reference PLL and translator

`reference_clock_generator` connects the divide-by-two feedback, phase/frequency
detector, charge pump, filter, balanced ring oscillator, output buffers and
1.2 V to 3.3 V translator. It derives the ADC clock from the sole external
125 MHz reference. The separate receive-processing oscillator does not replace
this PLL.

The current IHP candidate replaces 65 one-gigaohm simulation returns with
explicit storage: 32 NAND stack capacitors and one pump-node capacitor. The
230-MOS circuit retains nine intended resistors. The basic classes retain their
existing defaults; this process implementation selects the charge-storage
configuration. The process binder rejects any changed resistor inventory,
including a restored one-gigaohm return. Fresh schematic C++RTL, Verilog-A,
their comparison and nominal PSP checks pass for this revision. The PSP
output is 250.004712 MHz. Actual physical-device and extracted-wire checks
remain separate from those schematic results.

`reference_clock_layout.py` lowers the exported circuit to actual SG13G2 MOS
fingers, contacted poly resistors and MIM plates. Small capacitors use explicit
series plates; larger storage uses parallel banks. Those floating junctions and
device parasitics must remain in subsequent electrical analysis. All NMOS
substrate contacts connect to the common substrate, including the HV devices;
every guard contact remains in the independent transistor schematic.

The first row measures 6.228 × 0.273 mm. Its seven ports and 652 instantiated
devices match in strict LVS. Complete DRC rejects it with 48 antenna markers
on three nets, in addition to nine isolated density failures. The markers
identify the reference input, tuning voltage and resistor-isolated detector
reset. Three physical P-diffusion antenna diodes now protect those nets against
the logic supply. Their charge and leakage require fresh electrical testing.

`analog_row_fold.py` partitions the row only at device gutters. It preserves
every device, rebuilds each net's Metal5 bus within a row, and connects rows
through separate Metal4 spines. Power spines have 5 µm metal and actual via
arrays. The four-row diagnostic reduces the longest dimension to 1.853 mm,
with a height of 1.044 mm, and passes strict LVS against the same schematic.
This is a floorplan improvement, not a reduction in transistor area. The
protected folded revision has its own maintained regression:

```sh
ctest --test-dir build -R '^analog\.1g\.ihp_reference_clock\.layout$' --output-on-failure
```

The test checks the complete DRC deck, strict LVS, a deliberately disconnected
input, and rejection of unbound transistor parameters. It applies the pinned
PDK fill patterns and requires every density rule to pass. The first filled
revision passed spacing and antenna checks but exceeded the 60% maximum on
Metal2 (65.444%) and Metal3 (65.282%). The reference macro now uses 1 µm fill
spacing on both layers; the digital receiver retains its existing fill settings.
LVS and DRC verdicts are recorded independently, including tool failures.
These memory-intensive stages serialize with routing.
The filled macro passes the complete DRC deck with zero violations. Hierarchical
LVS exceeded the available per-process memory budget. Flat LVS exposed the
child devices' pin labels as extra top-level ports, so `lvs_view.py` creates a
checking view with those local labels scoped out. It retains every mask shape,
instance and transform, every top-level label and every device-recognition
annotation. A signature checks those invariants after writing and rereading
the view. The final GDS retains its original labels.

The maintained flat comparison matches all seven ports. PDK simplification
normalizes the parallel fingers/plates and common substrate taps to 526 devices:
230 MOS, 167 capacitors, nine resistors, three protection diodes and 117 taps.
The maintained regression passes full DRC, this complete inventory and rejection
of a broken input connection. The electrical runner still
uses all individual physical fingers and capacitor plates.
No reference-clock macro is yet qualified for the ASIC: device-level and
extracted PLL behavior, load/clock-tree timing, process corners, supply integrity
and filled-chip verification remain required.

The compact-oscillator revision passed the geometry checks but failed the full
2 us extracted transient: its settled output is about 124.689 MHz, with the
reference-side phase detector missing alternate edges. Its failed trace is
retained. Linear placement cost had hidden long routes across row cuts:
several latch enable/reset nets spanned approximately 1.6 mm after folding.
`reference_clock_placement.py` now prices the per-row buses and their connecting
spine. The oscillator stays local while the remaining logic uses this folded
routing cost. Six independent geometric/optimizer checks are registered as
`analog.1g.ihp_reference_clock.placement_guards`. The new layout passes fresh full DRC with zero violations and strict LVS
matching all 526 normalized devices. Its 1.844 by 1.121 mm bounding box is
larger than the prior candidate. The unchanged 250 MHz extracted electrical
contract failed: the output settles near 166.612 MHz and the divided clock near
83.288 MHz. The detector still misses reference edges and the tuning voltage
has 59.5 mV ripple against a 2 mV limit. The failed complete trace is retained.

The conserved extraction confirms that reference-master enable loading falls
from 276 to 61 fF and feedback-clock inverter loading from 193 to 50 fF.
The common reset still carries about 527 fF of interconnect load; the reference
master's complementary feedback rises from 63 to 116 fF. These are collapsed
interconnect loads, excluding intrinsic device charge. The distributed network
is retained in simulation, and the remaining feedback/reset paths need the
same electrical acceptance as the shortened enable paths.

The hierarchical placement keeps each complete master/slave flip-flop within one row.
`tests/reference_clock_layout_export.cpp` exports the actual device ownership
from the divider and both phase-detector ICs. The layout test runs that exporter
and requires its Verilog-A circuit to match the maintained model byte for byte.
`reference_clock_hierarchy.py` binds these identities to physical transistors,
resistors and aggregated storage branches. A capacitor shared by different
groups stays outside both groups. No device or storage branch is removed.

Each flip-flop is optimized locally, with external connections costed to its
left-hand routing spine. The oscillator and remaining support circuitry occupy
separate rows. `reference_clock_row.py` reserves row boundaries before placement;
the final layout checks the actual instance coordinates after folding to reject
any split group. Twelve hierarchy/placement regression checks pass. Full filled
DRC passes and strict LVS matches 526 normalized devices. The new footprint is
1.611 by 1.404 mm.

Corrected-sidewall extraction still fails the electrical contract: the settled
output is 125.031 MHz and divided output 62.448 MHz, while the detector responds
to 25 of 50 reference edges. Tuning ripple is 142.7 mV. A separate diagnostic
adds observations without changing the circuit. At 388.063 ns the reference
edge arrives while the common-reset voltage is still about 0.51 V. The master
enable stays below 0.4 V throughout the following high half-cycle; it does not
recover until 392.796 ns, and UP rises at 397.694 ns. The extracted reset wire
contributes 785 fF, in addition to intentional and intrinsic storage.

A sizing trial reduces the common-reset R3 resistor from nominal 1 kohm to
200 ohms using the already supported 4-um-wide, 2.82-um-long PCell dimensions.
It retains the preceding routing parasitics to test reset recovery. This trial
is not a matching GDS/PEX result: a successful trial still requires a C++ IC
parameter update, regenerated physical geometry, DRC/LVS and fresh extraction.
The maintained clock model has not been changed on the strength of a trial.
