# Processing oscillator physical implementation

`processing_clock_oscillator_layout.py` lowers the exported
`OneGIHPProcessingClockOscillator` topology to contacted IHP devices. It checks
the exported device roles, dimensions, resistor values and storage branches
before constructing geometry. The independent CDL uses those intended
dimensions; it does not copy dimensions from an extracted netlist.

The current implementation contains 55 MOS fingers for 51 logical MOS devices,
33 `rppd` resistors, 32 series `rhigh` resistors, 60 MIM plates and one
`dpantenna` protection diode. The 5.09 Mohm fine-control branch uses straight
resistors to avoid the pinned PCell's inconsistent bent-resistor length
interpretation. Each 1 fF branch uses three legal 3 fF MIM plates in series;
both intermediate conductor nodes remain real electrical nodes. Guards,
contacts and all series connections are present in the layout.

The diode connects CONTROL to VDD through a P-diffusion/N-well junction. It
provides antenna protection and remains in subsequent electrical simulations.
The alternative N-diffusion/substrate diode passed local geometry checks but
its process simulation stalled; it is not the selected implementation.

## Current evidence and failure

The narrower-resistor layout passes strict LVS (294 instantiated devices including
contacts), pin checks and local DRC including antenna checks. Nine isolated
minimum-density categories remain. These checks do not qualify a complete die.
The passing receipt is
`build/clock-integration/oscillator-layout-narrow/checks.json`. Its dimensions
are 2294.67 × 141.4 µm, or 0.324466 mm², versus 0.748962 mm² for the previous
wide-resistor implementation. Both are macro bounding areas, not die area.

The previous wide-resistor physical-device electrical test **fails**. At trim 28672 it produces
24.801876 MHz, but its 665.464 ps period spread exceeds the unchanged 80 ps
settling limit. A separate central-trim diagnostic produces 127.091526 MHz
with 133.847 ps spread, also failing. Removing the protection diode in a
diagnostic does not remove this failure. These results do not qualify the
clock or the ASIC. Its extracted test also fails settling: 742.537 ps at
13.887539 MHz for trim 28672. The earlier schematic process tests used ideal resistors
and capacitors; they do not establish the behavior of these physical passives.

Replacing only the physical resistors with ideal resistances in a diagnostic
restores stable oscillation at 133.522 MHz. The revised geometry therefore
uses the legal 0.5 µm poly width and recalculates lengths from the pinned PDK.
The 10/20 kohm branches become 19.19/38.66 µm long; each of the 32 high-resistance
units becomes 53.70 µm long. Intended resistance and all circuit connections
remain the same. The independent narrow-device trial passes at trims 28672
and 38528, with period spreads of 79.631 and 22.662 ps respectively. The full
six-case physical-device CTest now passes: monotonic range 25.789–171.493 MHz,
nominal central frequency 133.354692 MHz, loaded frequency 133.212410 MHz,
and refined frequency 133.356729 MHz. These are device-level results before
wiring. Its resistor
and series-passive CTests pass, including independent process-model DC checks
of the 0.5 µm 10/20 kohm devices and rejection of broken contacts/connections.

The new layout's extraction passes device and capacitance conservation with
2.083578371 pF of interconnect capacitance, about 59% below the previous layout.
The receipt is `build/clock-integration/oscillator-extraction-narrow/extraction.json`.
An additional connectivity audit rejects this extracted netlist: 65 nonzero
coupling-capacitor endpoints refer to resistor-body names without a wire,
device terminal or port connection. The bodies correspond to its 33 `rppd`
and 32 `rhigh` devices. This matches [Magic issue #559](https://github.com/RTimothyEdwards/magic/issues/559).
`extracted_connections.py` now rejects these disconnected endpoints before
simulation; the CTest covers this defect and valid physical MIM junctions.
Numeric capacitance conservation alone did not detect the electrical error.
The audit is `build/clock-integration/oscillator-extracted-connections.json`.
The raw extraction remains unchanged. `resistor_body_extraction.py` now binds
all 65 bodies to finite-resistance electrical midpoints and removes 0.361581 pF
of duplicate body-to-substrate shunts. Its connection audit checks all 1,433
nonzero capacitor endpoints. Two half-length R3 segments preserve each physical
resistor's original contact resistance and total intrinsic charge; nine
independent DC/common-mode AC process cases pass across −40/27/125 °C.
This electrical representation is not two extra physical layout resistors.
Contact-head capacitance and independent field calibration remain unresolved.
The corrected candidate still fails the early settling stress: 119.844 ps
period spread at 14.861919 MHz, against the unchanged 80 ps bound.

The earlier extracted electrical test also fails the original early settling
stress: 402.948 ps period spread versus 80 ps at trim 28672. Rail, supply and
bounded-voltage checks pass. This failure is retained; neither the smaller
area nor the passing device-only test qualifies the extracted oscillator.

The actual `ProcessingClockControl` discards two 128-cycle reference windows
before measuring frequency after a trim change. At 125 MHz that is 2.048 µs,
whereas the original unit stress measures between 256 and 512 ns. The separate
`extracted_acquisition` test checks cold-start behavior over the next 128-cycle
window, from 2.0801 to 3.1041 µs after time zero. It retains the 80 ps settling,
rail, supply, load and timestep-refinement limits, with four fixed trim points
within the controller's actual 16-bit range. All six cases pass: the four trim
frequencies are 14.883554, 32.548040, 127.177361 and 156.481310 MHz. Increasing
the output load from 20 to 200 fF changes the third point to 126.762052 MHz
(−0.3266%); halving the maximum timestep gives 127.179148 MHz (0.00141%).
The receipt is `build/1G/IHPSG13G2/tests/results/processing_clock_oscillator_extracted_acquisition/checks.json`.
This fixed-trim test does not execute the controller and cannot establish lock.
The earlier later-window diagnostics used disconnected body couplings and
remain invalid as electrical qualification. Full extracted closed-loop
acquisition and admission tests remain necessary.

The previous layout's conserved extraction preserves all device counts and 5.060760695 pF of
interconnect capacitance. The distributed network initially contains 113,383
wire resistors and 1,818 capacitors. `reduce_extracted_wires.py` eliminates only
resistor-only nodes using exact nodal equations. Every port, device terminal,
observation and capacitor endpoint remains. This reduces the previous network
to 3,329 resistors without dropping capacitance or approximating time
constants. Independent nodal-current regression checks exercise the reduction.

`oscillator_extracted_process.py` resolves observations through Magic's actual
connectivity aliases. The extracted electrical test uses the same trim-range,
rail, supply, settling, load-sensitivity and timestep-refinement acceptance
as the original oscillator test. It requires matching current layout and
extraction hashes. Passing extraction alone is not an electrical result.

## Reproduction

Configure CMake with the prepared PDK and tools as described in
[clocks.md](clocks.md). The relevant CTest names are:

- `analog.1g.ihp_processing_clock_oscillator.passive_layout`
- `analog.1g.ihp_processing_clock_oscillator.layout`
- `analog.1g.ihp_processing_clock_oscillator.physical_devices`
- `analog.1g.ihp_processing_clock_oscillator.extracted`
- `analog.1g.ihp_processing_clock_oscillator.extracted_acquisition`
- `analog.1g.ihp_processing_clock_oscillator.acquisition_guards`
- `analog.1g.ihp_extraction.resistor_body`
- `analog.1g.ihp_extraction.resistor_body_connections`
- `analog.1g.ihp_extraction.wire_reduction`

Set `CPPHDL_1G_PROCESSING_OSCILLATOR_LAYOUT_PROOF` to the passing layout
`checks.json`, and `CPPHDL_1G_PROCESSING_OSCILLATOR_EXTRACTION` to the directory
containing its passing `extraction.json`. The latter is produced by
`extract_analog.py`, using top `ProcessingClockOscillator` and the explicit
port order in `processing_clock_oscillator_layout.PORTS`. Use the locally
prepared Magic executable; [analog-extraction.md](analog-extraction.md)
describes the extraction corrections and remaining calibration limitations.

The physical tests start storage discharged, hold CLEAR for 32 ns, release
it over 100 ps and simulate to 512 ns. They use a 10 ohm supply impedance,
the original four trim settings, 20/200 fF output loads, and a refined timestep.
The current electrical scope is TT/27 C/1.2 V. Wiring, physical-passive PVT,
closed-loop acquisition, noise, mismatch and complete chip integration require
their own evidence.
