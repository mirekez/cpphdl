# Independent native / Verilog-A regression

[Measured results](validation.md) record the checked prototype behavior and toolchain.

Run from the C++HDL package root, after configuring OpenVAF and an OSDI-enabled
ngspice as described in [the parent README](../README.md):

```bash
cmake --build analog/build --target check-10g
ctest --test-dir analog/build -R 'analog.10g.link' --output-on-failure -j 2
```

A missing external tool must not be mistaken for a second-flow pass. Set
`CPPHDL_ANALOG_VERILOGA_TESTS=ON` to require both tools at configuration. Without
them, native tests remain available and external-flow tests are not registered.

## What runs

[bench.cpp](bench.cpp) elaborates the actual IC hierarchy. Native mode solves its
coupled terminal equations and writes CSV. Export mode emits the same electrical
network as Verilog-A and the stimulus/initial-state SPICE deck. OpenVAF compiles
that source to OSDI; ngspice independently integrates it. There is no replay of
native receiver decisions, clock, lock or equalizer outputs in the external flow.
Only bench sources, model parameters and explicit initial states are shared.
The integrated link stamps its two large linear DC-block capacitors as native SPICE
capacitors; all other device contributions remain in the generated Verilog-A.
The standalone SFI tests use the complete Verilog-A capacitor laws. This explicit
experiment option avoids a startup-conditioning failure in the flattened
OpenVAF/ngspice compact model when 100 nF and fF state coexist. Each large capacitor
is stamped exactly once; its value and initial voltage are preserved. Reusable
Verilog-A modules always retain every capacitor. The threshold is recorded in
`conditions.json`, with each transferred device marked in `design.va` and `bench.cir`.

[RunFlow.cmake](RunFlow.cmake) runs the identical [verify.py](verify.py) absolute
checker separately on each flow. CTest fixtures permit `.compare` only after both
independent checks pass. Matching wrong outputs cannot satisfy the analytic laws,
known bits, recovered-clock bounds or independent block descrambler.

| Cases | Independent checks |
|---|---|
| `primitives` | Analytic RL ramp response, VCCS DC gain, diode load line, PWL ramp |
| `gm_stage`, `limiter`, `rectifier` | Gain/swing and independent rectifier load line |
| `sample_hold`, `dff`, `mux2`, `xor` | Known electrical bits and held values between sampling windows |
| `history`, `serializer`, `deserializer` | Symbol ordering and every history/parallel output |
| `emphasis`, `transmitter` | Loaded analytic pre/main/post sum, including stored TX history |
| `channel` | Known differential load-divider ratio after settling |
| `sfi`, `sfi_decay` | 10G symbol transmission, receiver common mode, independent high-pass exponential and DC rejection |
| `ctle_low`, `ctle_nyquist` | Complex gain against the independent R/C transfer function at 1 and 5.15625 GHz |
| `hogge` | Reference-pulse activity and expected sign/range of early-clock pulse imbalance |
| `vco` | Oscillation count, nominal period and high/low plateaus |
| `lock_monitor`, `lock_monitor_bad` | Matching detector pulses qualify lock; persistent mismatch prevents it |
| `cdr`, `cdr_phase` | Data-driven recovered frequency, sample phase, activity and qualified lock; second case shifts input by 0.23 UI |
| `cdr_offset`, `cdr_offset_negative`, `cdr_ppm` | Both signs of 100 ppm input-rate offset; longer nominal steady-rate measurement |
| `cdr_noisy` | Bounded 20 mUI edge displacement and 15 mV differential amplitude disturbance |
| `cdr_no_data`, `cdr_gap` | Constant data does not assert lock; 66 UI transition gap loses lock; phase and lock must recover within 80 UI after transitions resume |
| `dfe`, `dfe_bypass` | Actual prior-decision cancellation; the bypass must demonstrate errors on deliberately strong postcursor ISI |
| `receiver`, `link`, `link_loaded` | Recovered serial and all four parallel streams, header and independently descrambled complete 64-bit data payload |

`link` starts at four parallel electrical inputs and includes serialization, three
tap registers, FFE, RC fixture, SFI blocking/termination, CTLE, autonomous PLL CDR,
DFE and deserialization. `link_loaded` changes the SFI termination to 80 Ohm.
`TX` probes the emphasis output; `RX` probes the end of the RC fixture before
the DC blocks. The receiver itself connects to the DC-block outputs.
It must still recover the finite vector. The separate `loading` test requires both
TX and RX RMS signal amplitudes to decrease by at least 1% and at most 20% in each
solver, and compares their loaded/nominal ratios within 0.005. This checks that an
external receiver load influences the transmitter through the connected network.
The integrated cases transmit 192 UI and include a complete scrambled 66-bit data
block at symbols 112–177. Acquisition is excluded for the first 80 UI; every checked
recovered decision and each corresponding four-bit history must be correct. The
checker uses the declared 1.5 UI TX history latency, without fitting an output delay.

All traces must be finite, monotonic, cover the requested duration and contain no
sampling gap above UI/32. A SPICE exit code of zero with an aborted transient still
fails coverage. Clock/lock/decision/detector voltages are checked against electrical
rail bounds, including values between observation centers.

## Comparison tolerances

Comparison uses a fixed UI/128 grid, independently interpolated from each trace,
and the same acquisition window. `comparison.json` records every metric and bound.

| Measurement | Maximum difference |
|---|---|
| Generic analog voltage RMS | 6 mV |
| Differential signal RMS | 15 mV |
| Analog `OUT` RMS | 20 mV |
| Logic `OUT` RMS | 60 mV |
| Stored decision / parallel logic RMS | 100 mV; independent bit and rail checks must also pass |
| Loop control / steady LOCK RMS | 25 mV |
| Gap-test LOCK transition timing | 1 UI; equal transition count and polarity |
| Matched recovered-clock rising edge | 0.035 UI (3.39 ps at nominal rate) |
| Mean recovered period / phase | 0.003 UI / 0.035 UI |
| Autonomous free-running oscillator period | 0.015 UI; absolute phase is free |
| Detector average pulse contribution / imbalance | 15 mV / 5 mV |
| CTLE measured gain / phase | 0.025 absolute gain / 0.02 rad |

A free-running oscillator accumulates phase from integration error; its frequency
and electrical plateaus are compared. Recovered edges are instead matched to their
known symbol indices. Pulse means compare the phase detector's loop-driving charge
contribution. Voltage RMS remains diagnostic for those pulse/clock signals. During gap recovery,
LOCK transition times are compared within one UI, including every rise/fall; its
steady idle and reacquired windows retain the 25 mV RMS bound. An
endpoint can add one counted cycle to a fixed window; absolute period checks and
symbol-indexed edge matching reject missing internal cycles.

Absolute checks additionally require zero recovered data errors, sample phase
between 0.28 and 0.85 UI, mean-period error at most 0.005 UI, and worst-cycle error
at most 0.15 UI. The long `cdr_ppm` nominal-rate case requires mean rate error below
100 ppm. CTLE complex-gain error must independently remain below 2.5% in each flow.
These are declared prototype bounds, not a full SFI jitter-tolerance mask or BER proof.

## Numerical settings and refinement

The native flow uses backward Euler, analytic piecewise Jacobians and a regular
UI/256 step (UI/512 for standalone VCO and no-data holdover). All PWL corners are
included. Optional failed-Newton bisection has a minimum UI/65536 step and no
local-error estimator. Capacitor residuals use an equivalent integral-voltage form
with a roundoff floor in their scaling; this preserves slow DC-block decay without
amplifying floating-point noise by C/dt. The `solver` test compares analytic and finite-difference
Newton solutions, including reversed MOS terminals, RL, gm and diode laws. It also
checks inductor initial energy and rejects silently losing nonzero inductor state
when exporting an experiment.

SPICE uses Gear order two, maximum UI/512 step, relative tolerance 1e-5, current
tolerance 1e-12 A, voltage tolerance 1e-8 V and charge tolerance 1e-16 C. Charge
settings matter for femtofarad storage. Each bundle's `conditions.json` and
`bench.cir` record its numerical conditions. Absolute checks and timing comparison
remain required regardless of solver convergence.

The `ctle_nyquist_refined`, `cdr_refined` and `link_refined` cases halve both native
regular steps and SPICE maximum steps, rerun both absolute checkers and compare the
two flows. Separate `.refinement` tests compare coarse/fine results within each
solver using the documented bounds. `sfi_decay` changes the bench time unit to
1 us to exercise the physical 5.01 us time constant over 96 us; it is an analog
component test, separate from the `sfi` 10G test.

## Artifacts and generated modules

Results are under `analog/build/10G/tests/results/CASE/`:

- `cpp/` and `veriloga/`: source/deck, contract, conditions, raw and normalized
  traces, `checks.json` and stage logs; native solver equations/report in `cpp/cpp/`.
- `comparison.json`: cross-solver values, tolerances and pass/fail.
- `difference.csv`: native minus external voltage on the common measurement grid.
- Refined cases also contain `convergence/{cpp,veriloga}/` with coarse/fine results.

`generated` regenerates all 15 reusable 10G modules into a temporary directory and
requires byte-for-byte freshness. `generated_compile` compiles each with OpenVAF.
`generate-10g-veriloga` updates the checked-in modules in `10G/generated/`.
Build outputs and waveform result folders are ignored by Git.
