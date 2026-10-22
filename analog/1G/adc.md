# Successive-approximation conversion

[`ADCSARController`](adc_sar_control.hpp) is a C++HDL `Module` with member ports.
It does no analog comparison. Its `comparison_in` port receives a resolved
comparator decision; `trial_out` drives the DAC through a level interface.

At a 250 MHz controller clock, start edge 0 accepts a conversion. Edges 1–2
assert acquisition, edge 3 opens the sampler, and edge 4 enables conversion with
trial code 128. Edges 5–12 accept one decision each, from the most significant
bit to the least significant bit. Edge 12 pulses valid, updates the result and
clears busy. Starts while busy are ignored. Reset clears all controls and data;
a held start launches again when idle. The independent sequencer test uses
interval bisection, all 256 codes, out-of-range saturation, busy starts, reset at
every phase and continuous operation. Native, RTL and IHP gates agree; slow-cell
setup slack is +2.31 ns at 4 ns for 171 mapped cells.

[`SARADCFrontend`](adc.hpp) is the electrical `IC`: a MOS track/hold, a switched
R/2R DAC, resistor-loaded transistor preamplifier and eleven-transistor
regenerative comparator. No ideal voltage
comparison drives its outputs. All three circuits share their real port nodes,
so DAC input charge, comparator kickback, source impedance and hold leakage can
change a decision. Directly connecting the high-impedance ladder to the latch
failed: its evaluation kickback displaced the reference enough to select wrong
bits. The preamplifier's low-impedance loaded drains absorb that charge while
its input gates preserve the held and reference voltages. Its reduced explicit
input capacitors are part of the declared generic model, not a PSP charge fit.
The complemented trial bits, acquisition and evaluation
clocks are explicit electrical gate ports. Power and gate-level translation
still need physical implementation in a chip.

`tests/adc_cosim.py` closes controller feedback independently around each solver.
The test uses offline waveform relaxation: run the actual controller with the
previous pass's physical comparator observations, apply its gate trajectory to
the circuit, then repeat. It accepts a result only when the **entire control
trajectory** is unchanged. This establishes a self-consistent causal simulation;
it is not an ideal ADC response or a synthesized simulator inside the IC.
Expected input voltage is used only by the final independent accuracy checker.

Four conversions span the PAM5 input range. The source changes again while the
capacitor is holding, so successful conversion must reject that disturbance.
Each flow must resolve every comparator decision, complete at controller clocks
12/28/44/60, and meet the declared two-count quantization-error and 20 mV hold
limits. Only then may the two flows' output codes be compared within one count.
The experiment preserves controls, feedback, compressed waveforms and source
hashes for every iteration; incomplete or unconverged runs fail.

The preamplified integration converges on iteration six in both flows (seven
electrical passes). Each independently returns codes `89, 108, 147, 167` for
inputs `1.15, 1.4, 1.9, 2.15 V`; the ideal settled quantizer gives
`89, 108, 147, 166`. The largest observed conversion departure is one count,
and the cross-solver output codes match exactly. These four conversions do not
replace a full analog transfer/noise characterization.

A separate center-level profile exercises `1.65, 1.649, 1.651, 1.65 V`, including
an exact nominal DAC comparison. Both flows converge on iteration three and
return `128, 128, 128, 128`, with at most one count of ideal quantization error
and 1.984 mV of held-input departure. The PAM and center profiles pass all six
registered CTests in 492.82 s (`analog/out/adc-profiles-ctest.log`). Center-level
resolution in these deterministic circuits does not characterize comparator
offset, random noise or metastability probability.

Those tests exercise a host-clocked converter slice, with starts 64 ns apart.

`InterleavedSARController` sequences sixteen `ADCSARController` children. Each
child has sixteen clocks between starts and needs twelve clocks
to convert. A registered output mux delivers its result on clock 13. This
permits one result per controller clock. The initial electrical qualification
used 125 MHz. The receive target is now 250 MHz / 250 MS/s: the existing Gardner
timing loop requires observations half a 125 MBd symbol apart. One external
125 MHz reference remains the chip requirement; the receive clock is internal.
The sequencer's independent test checks 12,000 clocks, 11,782 conversions,
all 256 input codes on all sixteen slices, continuous digital cadence, launch
gaps, reset and disable. Native, generated RTL and IHP mapped trajectories match
exactly at the 250 MHz target. Its slow-corner cell-only setup estimate is
+0.87 ns at 4 ns; the initial 125 MHz estimate was +4.87 ns at 8 ns.

`SARADCFeedbackFrontend` adds a transistor NAND latch to the physical comparator
outputs. It retains the resolved decision through comparator precharge, so the
controller can read an actual stored bit at its own clock edge.
`SARADCArray<16>` connects sixteen complete electrical frontends to their common
input, evaluation clock and supply. Their input charge, reference switching and
supply currents enter the same equation system. The clear inverter includes its
gate/wire storage; omitting it produced an algebraic singularity during the
clear transition in the four-slice test and at a paused ngspice boundary.

`tests/adc_array.py` runs the controller online. Native execution reads each
electrical decision latch. The Verilog-A flow pauses ngspice at every controller
edge, reads that solver's own decision voltages, steps the C++ controller, alters
its gate-control sources and resumes with the retained electrical state. There
is no ideal input comparison in either feedback path. The common checker
independently requires conversion order/cadence, finite supply operation,
two-count accuracy and a 20 mV hold bound. A subsequent comparison allows at
most one count between independently passing flows.

At 250 MS/s, all nine one/four/sixteen-slice CTests pass: each native and
Verilog-A run satisfies its independent contract before comparison.
Both flows return `166, 128, 89, 147` in the four-slice case. All sixteen codes
also agree exactly, with maximum hold departures of 12.92 mV in native and
15.42 mV in Verilog-A. The sixteen-slice shared supply falls to 3.252 V and
3.247 V respectively. The nine array checks and three sparse-solver checks
pass together in 829 s. Wider sampling devices and
proportionally larger explicit gate/coupling capacitors are implemented as
`HighRateSARADCFeedbackFrontend`, the default array frontend type. This changes
the physical acquisition resistance and loading together. The original switches
failed the unchanged 20 mV hold contract at the faster rate in both solvers.
The wider four- and sixteen-slice Verilog-A profiles pass that contract with
15.84 and 15.42 mV maximum hold error respectively. Native runs use the
[certified sparse backend](../doc/sparse_solver.md); inconclusive LU rank
certificates fall back to QR without relaxing the accepted residual bound.

The earlier native four-slice run rejected an inaccurate Newton direction at
80.42 ns. This was a full-rank, ill-conditioned transient Jacobian. Retaining
the residual bound and retrying a smaller integration interval allowed the
experiment to complete. Structural rank failures remain fatal. The captured
linear system and rollback/RC tests provide regression coverage.
The original registered array experiments launch one acquisition burst. A new
`adc_array_16_repeat` regression launches 32 consecutive conversions at 250 MS/s,
reusing each physical slice after sixteen clocks. Both electrical flows must
produce one result per clock from clocks 13 through 44, in slice order, with
the same accuracy, hold and finite-supply bounds. Its second acquisition uses
the retained electrical state from the first; no slice is reinitialized. This
regression passes in both flows and in comparison. The largest code departure
is one count in each flow. Maximum hold errors are 13.11 mV in native C++ and
15.55 mV in Verilog-A; minimum shared supplies are 3.252 V and 3.247 V.
Thirty-one of the 32 independently resolved codes match exactly; the other
differs by one count, within the comparison contract. The one/four/repeated
sixteen-slice regression passes all nine CTests in 3280 s
(`analog/out/adc-array-reuse-current-ctest.log`). A finite repeated run does not establish
long-term drift, offset/gain/skew calibration,
reference distribution, physical clock/gate translation or IHP macro layout.
The full four-pair link still uses its documented simulation ADC boundary.
PSP comparator/sampler characterization remains separate from generic MOS
co-simulation; neither establishes full ADC noise, matching or layout performance.
