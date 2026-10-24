# Timing model

125 MBd gives an 8 ns nominal symbol period. The electrical benches provide a
1.2 V boundary clock. It captures the previous transmit symbol before the new
command changes at 0.15 UI; the command ramp ends at 0.19 UI. Receiver scoring
samples at 0.8 UI. These phases are declared, not fitted to each output.

`gardner_timing` is a sampled second-order timing-loop reference. The caller
interpolates actual receive samples at the predicted symbol time and half a
period before and after it. The error is `(early - late) * middle`, followed by
bounded proportional phase correction and integral period correction. Expected
symbols do not enter the loop. Unit tests start with phase error, check source
periods corresponding to both signs of 100 ppm, and exercise no-data frequency
holdover. Activity is recorded; it is not advertised as a PHY lock signal.

## Hardware timing controller

[`ReceiveTimingLoop`](receive_timing.hpp) implements a fixed-point controller
as a C++RTL `Module`. The root converter exports `generated/ReceiveTimingLoop.sv`.
Its three input samples use the same eight-bit offset-binary format as the
receive filter: zero 128, one PAM level 32 counts. Samples refer to the predicted
center time and half the current period on either side. `advance_in` accepts one
triplet; `track_in` enables correction. Deasserting `track_in` requests holdover
at the retained frequency, without integrating that observation. The pipeline
accepts one triplet per clock and returns its correction **six clocks later**;
the frequency update occurs three clocks after acceptance. A stall inserts a
bubble while previous transactions drain. An output bubble preserves phase,
interval, period and error and clears valid/activity. Reset discards pending
work and returns period/interval to one nominal UI. Drain six clocks before
scoring holdover after tracking has been disabled.

| Output | Meaning |
| --- | --- |
| `interval_out` | Unsigned Q24 interval to the next center, including phase correction |
| `period_out` | Unsigned Q24 retained period, without proportional correction |
| `phase_out` | Fractional Q24 phase accumulator, modulo one UI |
| `error_out` | Signed 14-bit two's-complement detector result, in ADC-count squared units |
| `activity_out` | This accepted triplet has `abs(early - late) > 1` ADC count |
| `valid_out` | An observation was accepted on this edge |

For signed input observations `a`, `m`, `b`, one tracking transaction computes:

```
e = clamp((a - b) * m, -4096, 4096)
f = clamp(f + e, -2147484, 2147484)
p = 2^24 - trunc(f / 64)
interval = p - 160 * e
phase = (phase + interval) mod 2^24
```

The Q30 integral state limits frequency correction to approximately ±2000 ppm.
At 32 counts per level the gains correspond to a proportional factor 0.009765625
and integral factor 2^-20 in normalized PAM-level units. The multiplier and
accumulator use explicit bounded integer widths and magnitude shifts. The error
port has no expected-symbol input. Activity is an observation, **not a lock
indication**; a working PHY needs training/decoder qualification and timeout logic.

`tests/timing_reference.py` derives 16,513 independent integer contract states,
covering both frequency clamps, stalls, holdover and reset. `tests/timing.cpp`
additionally closes feedback using each implementation's actual interval output
on a noisy interpolated PAM5 waveform. Twelve cases cover both initial phase
offsets and ±100/500/1500 ppm source rates, 20,000 symbols each. Holdout checks
phase error, period error and symbol decisions against the source, and compares
phase error to the same source sampled without feedback. Truth is used only to
score the outputs. Native, generated RTL and mapped IHP gates run the same
closed-loop cases and must produce identical integer trajectories.

All four timing CTests pass: the integer oracle, native execution, exact
RTL/mapped trajectories and slow-corner cell timing. The latter has +2.41 ns
setup slack at an 8 ns clock, 0.4 ns I/O delays, 0.1 ns input transition and
0.1 ns clock uncertainty, after pre-placement buffering. The closed-loop cases include the six-cycle
feedback delay; the scored mean phase error ranges from 0.0079 to 0.0121 UI with
zero symbol errors on the selected noisy fixture.

The standalone controller also completes the IHP physical flow: actual mapped
cells, clock tree, routing, extracted parasitics and exact post-route functional
checks. `IHPSG13G2/results/timing_clock_tree/physical_module.json` records
`component_checks_pass`, +1.14 ns slow-corner setup and +0.05 ns fast-corner hold
slack at 8 ns, 2986 extracted nets, zero full-PDK DRC violations across 868
categories, and 42 matched transistor-level LVS circuits. Electrical budgets are
0.5 ns transition, fanout eight and 0.05 pF capacitance, subject to stricter
individual library pin limits. This is a controller GDS result; it does not
include the analog sampling clock or establish a complete PHY result.

This controller still needs a physical sampler/interpolator and clock-generation
actuator. The full electrical system's clock network is not yet connected to it;
those link benches retain supplied timing. Integration must account for receive
filter delay, training, loss of signal and lock/loss qualification. In particular,
1000BASE-T needs master/slave startup and slave loop timing; a second independent
receive oscillator is not a substitute. See the
[IEEE interpretation of Clause 40 master/slave timing](https://www.ieee802.org/3/interp/interp-2-0705.pdf).
The detector follows the early/middle/late construction of
[Gardner's sampled timing-error detector](https://doi.org/10.1109/TCOM.1986.1096561);
its use on the selected PAM5 fixture is tested here, rather than inferred to
establish Ethernet timing compliance.

## Electrical integration

The native backend uses backward Euler and analytic Jacobians. The default
regular step is UI/128; SPICE uses second-order Gear with maximum UI/256.
PWL breakpoints are retained and failed Newton solves may subdivide to UI/65536.
Refined link cases halve both regular/maximal steps. Same-equation trajectory
agreement and refinement apply to the selected prototype laws, not to silicon
timing, metastability or a jitter-compliance mask.

These experiment decks set `spice_capacitance_threshold_F = 1e-18`: capacitors
are stamped once by the ngspice host with their original terminals, values and
initial voltages. All other device laws remain in the compiled Verilog-A model.
Flattening all charge derivatives into one OSDI instance caused ngspice 47 to
abort with "timestep too small" on channel, DFE and four-pair cases. Moving the
unchanged capacitor equations to native SPICE stamps resolves those failures;
the checker rejects aborted/truncated traces even when ngspice exits with zero.
The reusable files in `generated/` retain every Verilog-A `ddt` capacitor and
are compile-checked. The transient evidence is specifically for the exported
Verilog-A/SPICE partition, not a successful run of every unpartitioned file.

## Electrical clock actuator

[`receive_clock_oscillator`](receive_clock_oscillator.hpp) connects two eight-bit
MOS R/2R DACs through 20 kOhm and 5.1 MOhm total source arms to a shared control
node. Coarse and fine weights are approximately 255:1. A three-stage
current-starved MOS ring draws current from the finite supply network. Its
explicit stage, control and gate charges and the output load affect frequency.
A reset transistor holds one stage during bias settling; releasing it starts
the circuit without assumed noise or arbitrary initial capacitor charges.

The separate actuator regression passes all three CTests in 281.93 s. Each
solver independently checks coarse/fine monotonicity, rail swing, duty, sustained
periods, finite supply power, output-load sensitivity and timestep convergence.
Passing flows then agree within 1% in frequency, 1 mV in mean control voltage
and 30 mV RMS in phase-normalized waveform. The high coarse setting requires
UI/1536 regular steps; UI/768 gave a 1.15% frequency discrepancy and was rejected.
The other regular cases use UI/768, with a separately refined nominal case.

This circuit uses the framework's generic MOS equations. It is not yet fitted
to IHP PSP or connected to `ReceiveTimingLoop`, the sampler array or the full
electrical link. Frequency tuning by itself does not establish recovered phase,
lock detection or slave-transmit timing.
