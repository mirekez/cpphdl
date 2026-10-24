# Fixed-point receive filter hardware

`ReceiveAdaptiveFilter` is a C++RTL `Module`, exported through the root converter
as `generated/ReceiveAdaptiveFilter.sv`. One instance processes one receive lane;
four instances share the same four local transmit references at each PHY port.
It implements four receive FFE taps and five echo/NEXT taps from each local
stream. The analog front end, ADC sampling and recovered clock remain outside
this block.

| Port | Representation |
| --- | --- |
| `sample_in` | Eight-bit offset binary: zero 128, one PAM level 32 counts |
| `local_in` | Four actual local digital PAM symbols before pulse shaping; each three-bit code is symbol + 2, A in bits 2:0 |
| `desired_in` | Training target, symbol + 2; ignored unless `train_in` is true |
| `train_in` | Allow coefficient updates on this accepted sample |
| `advance_in` | Accept a sample and shift histories; otherwise preserve all histories and coefficients |
| `sample_out` | Saturated offset-binary equalized sample, available after the accepting edge |
| `valid_out` | This edge accepted a legal sample; cleared on a stall or reset |
| `fault_out` | Sticky invalid local/training code; reset clears it |
| `coefficient_out[24]` | Signed two's-complement 18-bit Q14 coefficients, for observation |

The first four features are the current and three earlier receive samples in
signed ADC counts. The remaining twenty are negative local transmit references
and their four preceding symbols. Initial FFE coefficients approximate
`[4/3, -4/9, 4/27, -4/81]`; cancellation coefficients start at zero. The local
histories include the transmit pulse response through learned coefficients.
Passing already shaped replica voltages through the three-bit local-symbol
port would lose their intermediate levels and is incorrect.

With Q14 weights `w`, integer features `x`, training target `d` in ADC counts,
and truncation toward zero `trunc`, one accepted sample computes:

```
y = trunc(sum(w[i] * x[i]) / 16384)
e = clamp(d - y, -255, 255)
E = 2 ^ ceil(log2(max(1, sum(x[i] * x[i]))))
w[i] = clamp(w[i] + trunc(16384 * e * x[i] / E), -131071, 131071)
```

Only training executes the last three lines. A priority encoder and shifts
implement normalization. Output uses the preceding coefficients, then updates
become visible after the edge. Every arithmetic operation fits signed 32 bits;
negative shifts use unsigned magnitudes and explicit signs. Reset initializes
state explicitly in hardware. Native value initialization does not replace reset.

Normalization also uses explicit magnitudes rather than a signed division
operator. The prepared Yosys 0.9 frontend changed negative division by 16,384
into a zero-extending shift, despite matching C++/SystemVerilog/sv2v simulation.
The explicit expression preserves truncation toward zero through that frontend.
The regression now checks its lowered Verilog against every independent vector
before proceeding to technology mapping and gate simulation.

`filter_reference.py` independently derives 5,059 vectors using deque histories,
integer dot products and a power-of-two denominator. The same harness checks
native execution, generated RTL and mapped IHP gates, including every observed
coefficient. Its noisy channel has 2,040 holdout samples: all recover correctly,
versus 914 raw errors. It also exercises all 625 legal local symbol quartets,
stalls, rail samples, illegal codes, sticky failure and reset recovery.

The electrical regression separately runs this hardware class on each solver's
ADC observations, alongside the original floating-point reference. Training
targets stop at the declared boundary; weights must remain exactly unchanged
through holdout. `link_pcs` additionally decodes full Ethernet frames from the
hardware-filtered observations and compares the independently checked GMII
streams. `*-fixed.csv`/`*-fixed.json` preserve this evidence separately from the
floating-point run.

This implementation has parallel arithmetic and is expensive: preliminary
mapping uses about 1.04 mm² per lane in SG13G2. Preliminary slow-corner cell
timing at an 8 ns period, 0.1 ns uncertainty and 0.4 ns I/O delays reports
−66.65 ns setup slack, before wire delay. This version cannot run at 125 MHz.
Mapping equivalence establishes function, not a completed PHY. Pipelining,
power/area optimization and startup-controlled training remain implementation
work. Four FFE/five reference taps qualify the current lumped channel fixture;
they do not establish cancellation or BER for a 100 m cable.

## Pipelined implementation

`PipelinedReceiveFilter` has the same ports and coefficient representation. It
registers features, low/high nine-bit weight products, their combined result,
balanced partial sums, output/error,
two unsigned 4×8 partial correction products, their combined magnitudes,
normalized corrections and bounded weight updates.
It accepts one sample on each clock with `advance_in`. Output latency is six
clocks; coefficient-update latency is ten clocks. A stall inserts an invalid
transaction while older accepted transactions continue through the pipeline.
Reset or invalid codes discard all pending transactions.

ADC-feature weight multipliers use unsigned 9×8 magnitudes and restore their
signs at the existing register boundary. For a signed eight-bit feature,
the low product magnitude is at most 511 × 128 = 65,408 and the high product
magnitude at most 256 × 128 = 32,768. Both fit their signed 17-bit registers.
Their recombination retains the complete signed 18×8 result; the square is
formed from the unsigned feature magnitude. Pipeline latency, weight snapshots
and adaptation arithmetic remain the same.

The twenty local transmit features retain three-bit PAM5 codes through their
history and pipeline registers. Their decoded magnitudes are exactly 0, 32
or 64, so weight and correction products use shifts and selection instead
of general multipliers. Their energy terms are selected constants. Four ADC
features keep their full signed eight-bit arithmetic. The fixed-point scale,
accepted-symbol history, coefficient precision and pipeline schedule remain
the same; illegal codes still flush pending work and latch a fault.

Weights are read one cycle after sample acceptance. A correction is applied to
the then-current weights ten cycles after acceptance. The update uses
`1024 * e * x / E`, giving a 1/16 step before normalization, to stabilize this
delayed feedback. This needs a longer training interval than the single-cycle
reference. In-flight training corrections must drain for ten clocks before
the test freezes and scores holdout coefficients.

The independent oracle schedules transactions by acceptance time, captures the
appropriate weight snapshot, and evaluates whole dot products. It does not
duplicate the hardware reduction tree or its register layout. It checks 37,724
states including stalls, reset with pending work, invalid-code flush and every
coefficient. The synthetic channel has 32,768 training samples and 4,078 scored
holdout samples, all recovered correctly versus 1,831 raw errors. Its target is
deliberately invalid during holdout, proving that the target port is ignored.

`tests/cell_timing.py` repeats the necessary pre-route slow-corner timing check.
`--repair` first buffers/sizes actual cells for fanout/capacitance; it does not
add routed-wire evidence. The preceding signed-product implementation passed
four focused CTests in **598.58 s**: independent oracle, native,
exact RTL/frontend/IHP gates and slow-corner cell timing. It contains 69,488
mapped cells, including 5,727 registers, and has **+1.78 ns setup slack** at
8 ns, with 0.4 ns I/O delays, 0.1 ns input transition and clock uncertainty.
The earlier five/nine-cycle implementation passed functional mapping but missed
placement setup by 0.11 ns in its full-width weight multiplier. Splitting that
product adds the current register stage and improves the cell timing margin.
Actual placement, routing and extraction are separate checks under
`IHPSG13G2/physical_module.py`.
The bounded-magnitude prototype passed 37,724 native and generated-RTL states,
including every coefficient, with exact trace agreement. A separate arithmetic
experiment checked all 67,108,864 weight/feature combinations against independent
64-bit multiplication. For the PHY-facing `ReceiveFilter` wrapper, identical
IHP mapping reduced cells from 70,222 to 56,655 and cell area from 934,007.761
to 786,154.799 µm² (15.83%). Its repaired slow-corner cell estimate has +1.83 ns
slack at 8 ns, compared with +1.78 ns previously. All seven standard production
CTests pass in 1,053.32 s: independent reference, both native interfaces, exact
RTL/frontend/mapped checks, and both slow-corner timing estimates. The interface
with all coefficient probes has 56,637 cells, 783,856.067 µm² cell area and
+1.59 ns slack; the PHY wrapper has the 56,655 cells and +1.83 ns figures above.
These figures exclude routed wires and fill. That 1400 µm physical run reached
its three-hour routing timeout before completing; its placement and unfinished
routing snapshots are retained, with no physical pass claimed.
Both variants still require passing routed timing before use in the complete
PHY.

The newer PAM5-code implementation passes all seven maintained reference,
native, RTL/frontend/IHP and cell-timing checks. Both interfaces use the same
37,724 independent states; the full-state interface checks every coefficient.

| Interface | Mapped cells | Liberty area | LEF footprint | Slow-corner setup at 8 ns |
| --- | ---: | ---: | ---: | ---: |
| Full coefficient probes | 28,074 | 425,089.388 µm² | 425,108.477 µm² | +2.02 ns |
| PHY-facing wrapper | 28,112 | 425,803.770 µm² | 425,823.350 µm² | +1.97 ns |

The wrapper is 45.84% smaller in mapped cell area than the bounded-product
wrapper. A fresh 1100 µm die candidate follows this reduction; it is not a
verified footprint or a full-chip area limit. The first routed candidate passed
postroute vector equality but failed setup by 0.29 ns. Increasing the setup
repair margin produced +0.14 ns setup and +0.04 ns hold at the original clock
rate, again with exact postroute vectors. That candidate still fails the
unchanged 0.5 ns slew bound on seven pins (up to 0.52 ns). Neither run reaches
layout qualification. A further candidate uses a tighter 0.2 ns routing target
and repairs the network after setup/hold edits; its physical checks are running.
Routing has a twelve-hour stage allowance so a short runtime limit cannot
masquerade as a circuit result. Extracted timing, DRC and LVS remain required.

## PHY interface

[`ReceiveFilter`](receive_filter_datapath.hpp) wraps the pipelined class and
exports sample, valid and fault outputs with the same sample/local/target/train/
advance inputs. It preserves the six/ten-cycle schedule. Its child retains all
24 coefficient probes for the full-state unit checker. Those probes total 432
bits; exporting them as physical die pins distorted the first layout attempt.
The production wrapper keeps them internal and has its own independent native,
RTL, synthesis-frontend and gate tests using the same transaction oracle.
`IHPSG13G2/physical_module.py --module ReceiveFilter` implements this interface.
No receive-filter GDS is qualified until its routed checks pass.
