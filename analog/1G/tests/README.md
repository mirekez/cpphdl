# 1G module and system regression

Run `cmake --build analog/build --target check-1g` from the package root.
All tests use CTest and have label `1g`. No test downloads a PDK or tools.
The [2026-10-24 completion audit](audit-2026-10-24.md) records the current
766-test 1G/repeater inventory, 154 passing rechecked tests, and the remaining
physical integration and timing blockers. It is not a complete-suite pass.
See [recorded validation](validation.md) for the measured 87-test run.
The [hardware and MOS-bias extension](hardware-validation.md) records the newer results.
The [pair-acquisition regression](../pair_acquisition.md) checks all wiring
permutations and polarities, then connects acquired streams to the PMA/PCS frame
path in `pma_aligned`.
The [electrical FLP regression](../flp_frontend.md) checks MOS pulse drive and
either-polarity detection in both solvers, step refinement, and reference-clock
decoding of measured detector pulses before comparing passing flow results.

The [coupled receive front end](../receive_frontend.md) has separate acquisition
and closed-conversion fixtures. `receive_frontend` checks one/four/sixteen-slice
acquisition, loading, overload and refinement in both electrical flows.
`receive_conversion_1`, `receive_conversion_4` and `receive_conversion_16_repeat`
close actual SAR feedback through the amplifier, shared buffer, ADC and
core-domain interfaces. Each flow must pass its absolute checks before the
comparison runs. All are registered in CTest and built by `check-1g`.

The [reference PLL](../reference_clock.md) has separate native/Verilog-A
contracts and a subsequent comparison. Its updated integration test advances
the same C++RTL counter from each solver's achieved electrical clock through
the [clock adapters](../../VerilogA/live-clock.md). Both integrated flows and
their comparison pass; the connected high-voltage clock-domain test additionally
checks loading, overload and refinement. The
[ADC normalizer](../receive_normalizer.md) separately checks gain/offset,
signed rounding, saturation and valid-sample control before receive integration.

New [PCS](../pcs.md) and [clock-alignment](../receive_alignment.md) hardware cases
run with label `hardware`. Their `.native` tests check independent contracts;
their `.rtl` tests repeat those checks against converted RTL, compare complete
native/RTL traces, and, when the prepared IHP toolchain is present, synthesize to
actual IHP cells and compare the mapped-gate trace too. Artifacts are in
`analog/build/1G/tests/hardware/BLOCK/`, including
`hardware_checks.json`, mapped netlists, cell counts and all three traces.
The catalog now includes the transmit encoder, composed transmitter, receive
acquisition, framer, branch metrics, Viterbi decoder and composed receiver.
`filter` and `filter_pipeline` check all fixed-point filter coefficients against
independent integer/transaction oracles; see [their interfaces](../receive_filter.md).
`receiver.rtl` also runs the separate fault checker against its native, RTL and
mapped executables, then compares each complete fault trajectory.
It also replays the 59,456 independent receiver observations with 126,996
poisoned pause clocks, adjacent valid clocks and 32 resets while disabled.
`receiver_enable.native` runs this pause contract directly. Every PCS stage,
including trellis draining and frame tags, advances only on `symbol_enable_in`;
`receive_valid_out` identifies output words, including idle words. PMA loss
revokes lock while paused. The enabled receiver needs 4 ns timing because valid
events can be adjacent; earlier 8 ns physical results do not qualify this path.

`hardware_runner` verifies timeout and CTest termination cleanup, including
EDA child processes. Generated gate models disable Verilator output grouping
and split C++ functions to bound compiler memory; the large filters use
unoptimized generated C++ during gate checks to keep compilation manageable.
This changes the test executable's performance, not the simulated gate logic.
The reason for disabling grouping is documented in the
[Verilator argument reference](https://verilator.org/guide/latest/exe_verilator.html#cmdoption-output-groups).

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
System checks also run the actual fixed-point hardware filter on each solver's
ADC observations with local digital transmit references. Separate `*-fixed.*`
artifacts retain coefficients, holdout metrics and recovered GMII. The
single-cycle hardware's failing 125 MHz timing remains an explicit implementation
boundary. `cell_timing.py --repair` can measure the pipelined alternative using
the real slow-corner cells; this is still a cell estimate before routed wires.

## Cases

| Cases | Required evidence |
| --- | --- |
| `dac`, `dac_loaded` | All five levels; output loading changes spacing as predicted |
| `driver`, `driver_loaded` | Differential termination divider and common mode |
| `mos_driver` | Eight-MOS unary output cell; five loaded levels and common mode |
| `memory`, `pulse_shaper`, `transmitter` | Retained previous symbol and the actual 3/4 + 1/4 response |
| `slicer` | Four valid thermometer decisions for every PAM level |
| `hybrid`, `hybrid_bad` | Correct subtraction versus explicitly predicted residual echo |
| `passive_hybrid`, `passive_hybrid_loaded` | Independent resistor-bridge solution, finite source loading and changed differential load |
| `mos_amplifier`, `mos_amplifier_loaded` | MOS/resistor gain and bias against an independent scalar DC solution; explicit parasitic charge and load influence |
| `channel`, `channel_loaded`, `channel_reverse` | Reciprocal direction and changed impedance |
| `ctle_low`, `ctle_nyquist` | Independently derived complex transfer function |
| `dfe`, `dfe_bypass` | Own-decision feedback fixes ISI; disabling it exposes errors |
| `receiver` | Combined hybrid/filter gain including interstage loading |
| `pair_link` | Two simultaneous directions with frozen-holdout adaptive recovery |
| `link`, `link_loaded`, `link_noisy` | Eight simultaneous receivers, coupling, loading/noise stress |
| `link_uncancelled` | Disabled adaptation must expose errors |
| `link_pcs` | Two simultaneous GMII frames from actual C++RTL transmitters; physical waveforms independently equalized and decoded; exact GMII, preamble, FCS and cross-flow agreement |
| `pair_link_refined`, `link_refined` | Halved integration steps, both absolute checks and cross-flow comparison |

`algorithms` exhaustively checks the 512-point normal mapper and subset geometry,
ADC/NLMS on an analytical interference fixture, coefficient freezing, and Gardner
timing at both signs of 100 ppm. These are sampled reference classes, not exported
Verilog-A DSP circuitry. Electrical cases with adaptive recovery execute the same
C++ DSP separately against each solver's waveform; expected data enters only the
declared training window: 160 symbols normally, 416 for the changed-load case.
`link_pcs` also trains for 416 symbols, freezes coefficients, allows the unseeded
receiver to acquire idle, and then sends two independent frames. Its additional
artifacts are `gmii.csv`, `equalized.csv`, `decoded.csv` and `pcs.json`. The
decoder has no expected-data or seed input. The bench's declared PMA-ready
boundary remains a simulation interface, not a physical training controller.

`checker_guards` deliberately damages passing evidence and requires rejection
of truncated native/SPICE traces, nonfinite samples, duplicate timestamps,
incorrect levels and comparison after a failed absolute check. The two
`.refinement` cases also compare coarse/fine trajectories within each solver
and require the discrepancy between solvers to decrease with finer steps.

`generated` checks byte-for-byte freshness of generic and process-specific
exports. `generated_compile` compiles the entire generated catalog. Prepared IHP
models add `ihp_driver.mos_tt`, `.mos_ss`, and `.mos_ff`, documented in
[`IHPSG13G2/`](../IHPSG13G2/README.md).
`generated_rtl` independently regenerates the digital module/package catalog
through the root converter and rejects stale checked-in exports.

`transmit_driver` exercises the physical main/history MOS output banks with all
seventeen weighted levels, finite gate resistance, shared bias charge and
80/100/120 Ohm loading. Its open-load and reduced-bias-storage cases require
the corresponding electrical disturbances. Both solver flows pass the same
absolute contract before plateau comparison and independent time-step
refinement. Its checker guards remove history, flatten the output, erase supply
current or ignore external loading and require rejection. The separate
`ihp_emphasized_driver.nominal` uses explicit PSP geometry on the exported
connectivity; it makes no same-equation comparison to the compact MOS laws.

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

`analog.1g.pma_test_pattern.{native,rtl,mapped}` checks the test-symbol class
against independent pulse lists and scrambler vectors. The matching
`pma_transmit_lane` tests check normal/test selection, PCS polarity translation,
main/history banks, boundaries and fault recovery. Each hardware run checks its
own expected outputs before exact comparison with the native trace. These
digital tests do not replace the loaded transmitter's electrical checks.

`receive_gain_control.{native,rtl,mapped,cells}` checks ordered analog selection,
settling, accepted-sample windows, pipeline overlap, training pauses and latched
faults against an independent controller reference. `receive_range_calibration`
checks rounded range-to-Q8.8 conversion, sixteen-clock latency and replacement
of pending requests. Both cell tests repeat the complete functional vectors
after timing repair. Their slow-corner setup estimates do not include routed
wire, hold or chip-level qualification.
`receive_acquisition` checks their connected handoff in 400,993 independently
specified clocks, including frozen gain, captured calibration inputs and
reset/restart/disable at the handoff boundaries.

`ihp_programmable_receive.{cpp,veriloga,compare}` checks all five gain settings
with a physical sampler; each flow passes the absolute contract before a
separate 10 mV peak waveform comparison. The corresponding `.process` test
checks explicit IHP PSP transistors against the same electrical purpose.
`ihp_programmable_array.{cpp,veriloga,compare}` extends shared loading to sixteen
physical samplers, 64 acquisitions and the lowest/highest gains. Its ten
checker tests reject a broken final slice, signed-error cancellation, hold
drift, missing windows, stuck tracks, collapsed supplies, nonfinite samples,
backward time and a changed state at a repeated timestamp. Equal-time SPICE
states are retained; absolute checks and the comparison check every state.
The process flows independently check the actual IHP devices with two warmup
steps, followed by a 1 mV comparison that requires both electrical contracts to
pass. Failed cases retain measured errors and original waveform hashes. Five
report/refinement guards reject missing or stale evidence, changed grids,
bad acquisition hidden by matching hashes and loss of failed-case diagnostics.
These array acquisition tests are under qualification and do not perform ADC
bit conversion. See the [receiver boundary](../IHPSG13G2/programmable_receive_frontend.md).

`ihp_programmable_conversion_{1,4,16}_gain{4,8}.{cpp,veriloga,psp,compare}`
performs closed ADC conversion with each flow's own electrical decisions.
The sixteen-slice case converts two full banks at 250 MS/s after continuous
warmup. Independent electrical/code checks precede the one-count generic code
comparison. Eight negative/positive contract tests and seven binding tests guard
cadence, unstable comparator decisions, bad codes, hold drift, supplies and
exact host-control topology. See the [conversion boundary](../IHPSG13G2/programmable_receive_frontend.md#closed-conversion).

`ihp_wideband_receive.{cpp,veriloga,compare}` reuses the same single-slice
fixture and contracts for the inherited receiver with 0.25 pF gain-stage
compensation. `.process_tt`, `.process_ss_hot` and `.process_ff_cold` check all
five gains at nominal supplies with actual IHP transistors. `.bindings` rejects
altered topology and device/storage profiles. This candidate addresses the
preceding variant's slow/hot high-gain settling failure; full-bank and physical
qualification are still required.
