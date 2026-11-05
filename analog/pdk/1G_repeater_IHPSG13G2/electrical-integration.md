# Connected electrical partition

`repeater_analog.hpp` is the C++ IC assembly for two copper ports. Each port
contains four coupled receive/transmit lanes, thirteen actual SAR slices per
lane, two finite reference circuits and an FLP interface. Both ports share
the external supplies and reference-clock generator. The processing oscillator
and admission gate connect to the digital controller's actual trim, clear,
admit and clock pins.

Each lane exposes the transmitter's direct `P`/`N` nodes separately from
`SENSE_P`/`SENSE_N`. `pad_integration.py` connects TX to the IHP analog pad's
`pad` terminal and RX, including FLP detection, to its protected `padres`
terminal. The pad circuit couples these paths; shorting them would bypass
receive protection. The pad-connected implementation netlist also binds the
core/analog supplies and reference, reset, enable and status I/O cells.

`core_receive_lane.hpp` connects the MDI input network, terminated differential
preamplifier, five translated gain controls and thirteen `core_receive_slice`
objects. Each slice connects a real tracking translator, balanced sampling
switch, hold capacitor, translated DAC and retained comparator decision.
Its tracking translator loads the DAC's finite replica bias. This uses direct
tracking controls from the existing SAR sequencer; it does not adopt the
unqualified shared-event sampler or claim to fix that candidate's timing.

Startup tracks all thirteen capacitors for 4096 ADC clocks, opens them for four
clocks, then enables conversion. Clear and evaluation-enable now propagate
through every digital hierarchy level. The low-voltage evaluation gate selects
the opposite ADC half-cycle, allowing trial bits to settle after the rising
edge. Reserved digital slots 13–15 have no analog converters and return zero.

`electrical_export.cpp` exports the actual electrical nodes and all 67,572
circuit laws, including 23,717 transistors. `electrical_connections.py` checks
1,213 digital-bit connections and the internal circuit terminals. It produces
`RepeaterElectrical.sv`, connecting the converted digital hierarchy to the
analog partition, and `RepeaterAnalog.va`, preserving its complete circuit
laws. `RepeaterAnalog.blackbox.v` is a physical implementation interface only;
it is not a behavioral simulator. No logic/electrical co-simulation result is
claimed from elaborating that interface.

Run from `analog/`:

```sh
cmake -S . -B build/behavior
ctest --test-dir build/behavior \
  -R '^analog.pdk.1g_repeater.electrical_integration$' --output-on-failure
```

CTest first runs the digital integration fixture, then the electrical export,
connection checks and combined-netlist elaboration. Sixteen negative cases
exercise opens, wrong lanes/bits, shorts, incorrect supplies, missing storage
and missing digital ports, plus bypassed RX protection and TX behind the sense
resistor. The `pad_integration` CTest adds thirteen pad-binding fault cases and
elaborates `RepeaterPadConnected.sv` against the actual padframe CDL interface.
Source hashes reject stale digital proofs. The root
flow exposes the same work as `--target electrical` and performs it as part of
`--target chip`. Reproducible outputs stay in `build/`, outside source results.

The active ADC capture CTest also checks the startup controls in native and
RTL flows, alongside 209,047 aperture-aligned sample quartets. The separate
evaluation-gate CTest runs 138 ns in C++RTL and compiled Verilog-A/ngspice.
Each independently checks sixteen admitted pulses and sixteen disabled cycles
at 728 fF load; output RMS must agree within 20 mV. With the prepared PDK,
the same checker also runs explicit IHP PSP devices at TT/27°C/1.2 V,
SS/125°C/1.08 V and FF/−40°C/1.32 V.

The first evaluation gate failed the slow-corner 1 ns edge budget at 1.015 ns.
Increasing its transistor widths from N/P = 4/8 µm to 16/32 µm at L = 0.13 µm
reduced worst rise delay to 0.312 ns. The revised load screen passes all five
flows. This still excludes live translator feedback, clock-tree load and
extracted parasitics; it does not qualify the complete receive conversion.

**Remaining chip work:** slave TX clock recovery, aperture-aligned TX echo
history, calibration/configuration control, process geometry and loaded
electrical qualification, digital timing closure, physical pad/power
integration, routing and extracted verification. The common digital placement
now contains eight mapped SRAM macros and individually placed logic cells.
Gain/trim
and unresolved digital inputs remain explicit boundary ports. The straight-MDI
FLP connection uses pair A for transmit and pair B for receive; automatic MDI-X
switching is not implemented by this assembly. Full-chip qualification stays
false, and the published GDS is still the earlier development floorplan.
