# Recorded validation — 2026-10-17

The `1g` CTest suite passed **87/87 tests**, with zero failures, in 376.41 s
using two parallel jobs. It contains 26 electrical cases, each with a native
run, an independent Verilog-A/ngspice run and comparison; algorithm, export
freshness/compilation, checker-integrity and refinement tests; and three IHP
compact-model corner tests. Reproduce with the [build instructions](../README.md).

Environment: GCC 11.4.0, C++20, Release; OpenVAF 23.5.0 from the repository's
compatible toolchain; ngspice 47 with KLU. The prepared public IHP PDK was pinned
to `89d42ee6d2fd9b50b86a1aada6fee6673c3e6008`, with its recorded submodules and
compiled PSP models. Tests perform no downloads.

## Coupled bidirectional link

Every receive channel was checked separately, after training was disabled and
coefficients frozen. The eight-channel cases score 88 symbols per receiver,
704 decisions per flow. The two-channel pair cases score 176 per flow.

| Case | Holdout symbol errors C++ / VA | Minimum improvement across both flows | Largest waveform RMS difference |
| --- | --- | --- | --- |
| One bidirectional pair | 0 / 0 | 29.47 dB | 8.913 mV |
| Four simultaneous pairs | 0 / 0 | 14.07 dB | 9.296 mV |
| Remote driver impedance changed from 50 to 35 Ohm per leg | 0 / 0 | 23.90 dB | 9.709 mV |
| Four pairs with injected voltage disturbance | 0 / 0 | 14.08 dB | 9.296 mV |
| Pair with halved steps | 0 / 0 | Checked against the same 10 dB minimum | 4.821 mV |
| Four pairs with halved steps | 0 / 0 | Checked against the same 10 dB minimum | 5.037 mV |

The nominal case's worst normalized holdout MSE is 0.014435; the changed-load
case's is 0.003090. The required bound is 0.025, and no ADC clips occurred.
Training is 160 of 256 symbols normally, and 416 of 512 under changed load.
The longer changed-load training window was required to converge; the scoring
requirements were retained. Sources use separately seeded normal-data
constellation sequences (`0x13579b` and `0xa91e37`), not identical end-to-end data.

Waveform comparison uses a fixed UI/64 grid after four startup UIs. It performs
no output-delay fitting. The 12 mV bound applies to RMS, **not peak voltage**:
fast-edge peak differences reach 109.18 mV in the changed-load case. With finer
steps, the nominal four-pair peak reduces from 104.52 to 57.37 mV and RMS from
9.296 to 5.037 mV. Within-backend coarse/fine differences are at most 4.425 mV
RMS for C++ and 0.047 mV for ngspice in that case. Both refinement tests require
cross-backend discrepancies to decrease for every observed signal. The largest
trained-coefficient difference across all system cases is 0.005534, below 0.02.

The Verilog-A transient flow uses unchanged capacitor laws stamped by the SPICE
host to avoid flattened OSDI charge-conditioning failures. All remaining circuit
laws run in the compiled Verilog-A instance. All 15 reusable exports retain their
complete charge equations and pass freshness and OpenVAF compilation checks.
See [integration conditions](../timing.md) for the exact partition boundary.

## Absolute checks and negative controls

Isolated DAC/driver tests verify all five levels, common mode and changed loads.
Channel tests verify both directions and changed impedance. CTLE tests check the
complex transfer at 7.8125 and 62.5 MHz against an independent expression.
The combined receiver includes the hybrid's output loading. The full coupled
network is checked against a separate DC nodal solve, in addition to symbol
recovery and solver comparison.

The DFE recovers every scored symbol using its own held decisions; disabling
its feedback exposes errors. An imbalanced hybrid leaves its predicted residual.
Disabling system adaptation causes **289 C++ and 291 Verilog-A holdout errors**,
so the passing trained link is not an already-correct uncancelled detector.
The integrity test rejects truncated output from both simulators, nonfinite
samples, duplicate times, incorrect output levels and comparison after a failed
absolute check.

Sampled algorithm tests exhaustively check the 512-point normal mapper and subset
geometry. A separate analytical interference fixture checks the quantized ADC
and frozen-holdout NLMS (zero errors in 1008 scored symbols). Gardner timing
tracks both signs of 100 ppm in its sampled fixture and holds frequency without
data. That timing test does not close the full electrical link's clock loop.

## IHP steering-driver topology

The process-specific source and test are in
[`IHPSG13G2/`](../IHPSG13G2/README.md). Actual `sg13_hv_nmos` PSP devices use
W = 100 um and L = 0.45 um. All three corners pass the common five-level contract,
2.8 V common-mode check, 20 mA supply-current check, tail-headroom and 1 ns
settling checks.

| Corner | Observed settled tail-voltage range |
| --- | --- |
| `mos_tt` | 1.741–1.756 V |
| `mos_ss` | 1.660–1.674 V |
| `mos_ff` | 1.821–1.837 V |

These are functional topology checks with ideal bias and gate sources. The
generic square-law model and IHP PSP model have different physics and are
checked separately against the same requirements, not asserted to have identical
transistor waveforms.

## Evidence boundary and artifacts

The analog solver outputs independently feed the same C++ sampled DSP reference.
This establishes two-backend analog checks and the sampled interface; it does
not establish independent Verilog-A DSP hardware equivalence. The electrical
system uses supplied clocks and a lumped short-channel/coupling fixture.

Complete Clause 40 PCS, auto-negotiation, a closed slave timing loop, a qualified
100 m channel, magnetics, silicon layout/DRC/LVS and interoperability remain
outside this prototype. See the [requirements](../requirements.md).

Detailed evidence remains in ignored build artifacts:

- `analog/build/1G/tests/results/CASE/{cpp,veriloga}/`: contract, numerical
  conditions, observation columns, sources/deck, stage logs, traces, absolute
  checks, sampled ADC inputs and separately trained DSP results.
- `analog/build/1G/tests/results/CASE/`: differences and comparison report.
- `analog/build/1G/tests/results/{pair_link,link}-refinement.json`: convergence.
- `analog/build/1G/IHPSG13G2/tests/results/CORNER/`: actual PDK deck, bias
  observations, ngspice log and common-contract results.
- `analog/out/1g-regression.log`: complete CTest output for this run.
