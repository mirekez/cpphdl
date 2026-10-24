# Receive-lane startup and conversion controls

`ADCReceiveLaneController<Slices> : Module` connects
`ADCStartupSequencer<>`, `InterleavedSARBankController<Slices>` and
`ReceiveSampleNormalizer`. The public
ports are declared in the class, and `_assign()` binds children once. The
parent calls `_work(reset)` and `_strobe()` once per controller clock.

`enable_in` requires valid supplies and a qualified 250 MHz clock. The
startup sequence holds every real sampler in track for 4096 clocks
(16.384 µs), opens them for four clocks, then asserts `startup_ready_out`.
`clear_out` holds the analog decision latches clear until that point.
`evaluate_enable_out` gates comparator-clock delivery. Requests during
startup are ignored; requests after startup enter the normal SAR pipeline.
Disabling the module clears in-flight work and restarts precharge.

Startup drives the acquisition outputs directly. The converter and normalizer
stay disabled until `startup_ready_out`. Their acquisition and valid registers
clear on the same edge that resets readiness; the physical output gates need
only `enable_in`. This removes the high-fanout ready signal from the acquisition
output path. The parent carries each slice tag through the normalizer's eight
register stages. Unused acquisition positions stay low throughout startup.

The controller exports sixteen positions for compatibility with the existing
simulation interface. A thirteen-slice instance has thirteen active slices;
the remaining three positions are constants and have no converter devices.
Each sample has a slice tag through raw conversion and calibration. Changing
offset/gain, clipping, request gaps and flushes are covered by the regression.

```cpp
ADCReceiveLaneController<13> lane;

void _assign() {
    lane.enable_in = _ASSIGN(power_good() && clock_ready());
    lane.sample_enable_in = _ASSIGN(sample_request());
    lane.comparisons_in = _ASSIGN(comparison_bits());
    lane.offset_in = _ASSIGN(calibration_offset());
    lane.gain_in = _ASSIGN(calibration_gain());
    lane._assign();
}
```

The snippet assumes the parent declares the named input ports. Actual analog
samplers, DAC switches and comparators need voltage-domain translators driven
by the exported controls. `power_good` and `clock_ready` must be implemented
by the containing chip; this module does not detect rail or clock quality.

Build `analog_1g_adc_receive_lane`. CTests
`analog.1g.adc_receive_lane.native` and `.hardware` independently check the
startup masks, exact sample timing, expected codes and tags. The hardware
runner converts the actual C++ class, simulates RTL and IHP-mapped gates,
then compares all recorded outputs against native C++RTL. The current proof
covers 15,468 clocks and 2,914 samples. The `.boundaries` regression also
compares both sides of 60,000 clock edges with the original nested bank,
including reset during conversion and enable drops. It establishes digital behavior;
the analog fixture still needs integration with these physical startup,
clock and voltage-domain interfaces. The [physical controller flow](IHPSG13G2/adc_lane_physical.md)
adds routed timing, gate replay, full DRC and transistor LVS to this digital
composition, with explicit analog-interface loads.
