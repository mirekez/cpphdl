# 1G module and system regression

Run `cmake --build analog/build --target check-1g` from the package root.
All tests use CTest and have label `1g`. No test downloads a PDK or tools.
See [recorded validation](validation.md) for the measured 87-test run.
The [hardware and MOS-bias extension](hardware-validation.md) records the newer results.

New [PCS](../pcs.md) and [clock-alignment](../receive_alignment.md) hardware cases
run with label `hardware`. Their `.native` tests check independent contracts;
their `.rtl` tests repeat those checks against converted RTL, compare complete
native/RTL traces, and, when the prepared IHP toolchain is present, synthesize to
actual IHP cells and compare the mapped-gate trace too. Artifacts are in
`analog/build/1G/tests/hardware/{alignment,pcs,scrambler}/`, including
`hardware_checks.json`, mapped netlists, cell counts and all three traces.

`biased_driver` and `trimmed_driver` use the ordinary electrical three-stage
flow below. Separate `ihp_biased_driver.mos_tt` and
`ihp_trimmed_driver.{mos_tt,mos_ss,mos_ff}` cases use actual IHP PSP devices;
read [the bias and trim contract](../IHPSG13G2/biased_driver.md) for the externally
selected trim settings and the remaining characterization boundaries.

For each electrical case there are three stages:

1. `.cpp`: elaborate C++ IC classes, run the native solver, pass the absolute checker.
2. `.veriloga`: export that elaboration, compile with OpenVAF, integrate independently
   in ngspice, pass the same absolute checker.
3. `.compare`: require both stages to have passed, then compare observations and,
   for mixed system cases, separately trained DSP coefficients.

Artifacts are in `analog/build/1G/tests/results/CASE/{cpp,veriloga}/`. Each contains
the selected contract, sources/deck, numerical conditions, observation columns,
traces, stage logs and `checks.json`. System cases also contain `adc-input.csv`
and `dsp.json`; the parent contains `comparison.json` and `difference.csv`.

## Cases

| Cases | Required evidence |
| --- | --- |
| `dac`, `dac_loaded` | All five levels; output loading changes spacing as predicted |
| `driver`, `driver_loaded` | Differential termination divider and common mode |
| `mos_driver` | Eight-MOS unary output cell; five loaded levels and common mode |
| `memory`, `pulse_shaper`, `transmitter` | Retained previous symbol and the actual 3/4 + 1/4 response |
| `slicer` | Four valid thermometer decisions for every PAM level |
| `hybrid`, `hybrid_bad` | Correct subtraction versus explicitly predicted residual echo |
| `channel`, `channel_loaded`, `channel_reverse` | Reciprocal direction and changed impedance |
| `ctle_low`, `ctle_nyquist` | Independently derived complex transfer function |
| `dfe`, `dfe_bypass` | Own-decision feedback fixes ISI; disabling it exposes errors |
| `receiver` | Combined hybrid/filter gain including interstage loading |
| `pair_link` | Two simultaneous directions with frozen-holdout adaptive recovery |
| `link`, `link_loaded`, `link_noisy` | Eight simultaneous receivers, coupling, loading/noise stress |
| `link_uncancelled` | Disabled adaptation must expose errors |
| `pair_link_refined`, `link_refined` | Halved integration steps, both absolute checks and cross-flow comparison |

`algorithms` exhaustively checks the 512-point normal mapper and subset geometry,
ADC/NLMS on an analytical interference fixture, coefficient freezing, and Gardner
timing at both signs of 100 ppm. These are sampled reference classes, not exported
Verilog-A DSP circuitry. Electrical cases with adaptive recovery execute the same
C++ DSP separately against each solver's waveform; expected data enters only the
declared training window: 160 symbols normally, 416 for the changed-load case.

`checker_guards` deliberately damages passing evidence and requires rejection
of truncated native/SPICE traces, nonfinite samples, duplicate timestamps,
incorrect levels and comparison after a failed absolute check. The two
`.refinement` cases also compare coarse/fine trajectories within each solver
and require the discrepancy between solvers to decrease with finer steps.

`generated` checks byte-for-byte freshness of generic and process-specific
exports. `generated_compile` compiles the entire generated catalog. Prepared IHP
models add `ihp_driver.mos_tt`, `.mos_ss`, and `.mos_ff`, documented in
[`IHPSG13G2/`](../IHPSG13G2/README.md).

Experiment capacitors use native SPICE stamps to avoid flattened OSDI charge
conditioning failures. The complete reusable Verilog-A exports retain their
charge laws. See the exact [integration and partition boundary](../timing.md).

## Declared bounds

- Isolated plateau/transfer error: at most 6 mV; all five symbols must occur.
- Loaded cable error against a separate DC nodal solution: at most 4 mV.
- Combined loaded receiver error: at most 12 mV, including the 3 mV noise fixture.
- CTLE complex-gain relative error: at most 2%.
- Holdout: zero PAM5 symbol errors, no ADC clipping, worst normalized MSE at most
  0.025 and at least 10 dB improvement in every receiver. Negative controls use
  explicit nonzero-error bounds instead.
- Fixed-grid waveform RMS difference: at most 12 mV; comparator rails use 60 mV
  and DFE state/transition observations use 20 mV. Peak differences are recorded.
- Corresponding trained coefficients: maximum difference 0.02.

Comparison uses a fixed UI/64 grid after four startup UIs, without output delay
fitting or solver-sample-density weighting. A decision passing at 0.8 UI is not a
jitter tolerance or BER qualification. See [requirements](../requirements.md).
