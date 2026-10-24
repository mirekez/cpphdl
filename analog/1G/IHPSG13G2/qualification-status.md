# Working circuit and area priorities

The two-port repeater is unfinished. Electrical correctness has priority; area
reductions are accepted only after the connected process circuit passes the
same limits. Preset trim values characterize a circuit and do not implement
calibration. Generic C++RTL/Verilog-A agreement tests the exported equations;
IHP PSP tests additionally exercise intrinsic transistor charge and body effects.

## Receiver

The recorded precharged baseline is `charge_balanced_receive_frontend<16>`.
The current cold-start candidate is described below. The baseline retains 1.5 pF
hold storage, a 0.25 pF compensation step and a 30-micrometre PMOS sampling
dummy. It removes eighty provisional 1-gigaohm returns and retains floating
latch-node charge with thirty-two explicit 10 fF stack capacitors. Basic
classes preserve their previous defaults; this receiver explicitly selects
charge-defined nodes. The process binder checks the resulting topology.

The complete fixed-control bank passes C++RTL and Verilog-A at both gain
settings and their independent comparison. The 2.5 ns acquisition stress
passes gain 4 at 18.329 mV but fails gain 8 at 21.968 mV against the unchanged
20 mV limit. This failure remains a characterization result. Inspection of the
actual SAR sequencer shows two tracking clocks: 8 ns at 250 MHz. The closed
conversion tests, which use that real controller, determine operating acceptance.
A 1.75 pF sampling-storage trial worsened the short-window error to 23.259 mV;
it is rejected. Increasing dummy width helps one gain setting and harms the
other, so that change is not selected either.

The full sixteen-slice, two-burst gain-4 closed test passes C++RTL, Verilog-A,
IHP PSP and comparison. Maximum code errors are 1, 1 and 2 counts; process
acquisition and hold errors are 11.272 and 14.572 mV. Gain-8 also passes all
three flows and generic comparison: maximum code errors are 2, 1 and 2 counts;
PSP acquisition and hold errors are 7.176 and 14.887 mV. These are recorded
precharged-bank baselines, before the subsequent cold-start source change. Receipts are under `tests/results/charge_balanced_conversion_16_gain*`
in the build tree. No isolated or short initialized diagnostic qualifies the
complete bank.

The passive MDI input network passes independent DC/transient oracles in
C++RTL and Verilog-A, including all 625 two-driver level combinations, common
mode transitions and timestep refinement. Its common mode is about 1.91 V.
Connecting that mean directly to the existing single-ended PGA shifts its
output bias; integration therefore remains open. The [differential gain stage](differential_receive_gain.md) now passes the full
nominal process matrix, both generic flows and their comparison. NMOS tap
selectors avoid the PMOS-only version's low-common-mode failure. A 500-ohm
follower return reduces DC current and improves headroom. The slow/hot 1.65 V
unit stress still fails linearity; coupled process sweeps must establish the
actual operating range before that boundary can be accepted.

The [shared physical reference](receive_reference.md) passes generic flows,
comparison and all three declared process corners under the equivalent static
load of eight channels. The new `differential_receive_preamplifier` connects
that reference, the passive MDI network, both outputs of one input amplifier,
two buffers and the PGA. Both generic flows and comparison pass; its complete
50-MOS process circuit passes TT/27 C, SS/125 C and FF/−40 C. At a 2.3 V
line common mode, achieved PGA input means range from 1.238 to 1.274 V across
those conditions.
These checks still use finite linear line-source impedances and nominal
passives; no physical TX/ADC/echo-cancellation integration is claimed.

The preceding receiver's sixteen-slice generic closed test passed with one
count maximum error in each simulator. Its process fixed-control stress test
failed at 21.645 mV against 20 mV. Increasing the output follower to 1200 or
1600 micrometres still failed the short acquisition diagnostic, so those larger
followers were rejected. Its full closed PSP run lost controller-event evidence
and remains failed, even though a subsequent electrical diagnostic found a
maximum two-count code error. Missing evidence is never reconstructed into a
passing receipt. Controller trace writes now fail immediately on storage errors.

## Transmitter

The earlier 296-MOS programmable driver passed isolated process transients,
but failed when two drivers shared the line. Its maximum per-driver static
error at nominal TT was 42.278 mV against 20 mV. A correct summed line voltage
can hide opposite errors from the two output stages, so the new regression
checks each stage's terminal currents separately.

The candidate [cascoded transmitter](cascoded-transmitter.md) has 304 MOS devices.
Its extra cascodes preserve current when the remote end changes the achieved
line voltage. The nominal process transient passes: maximum error 11.308 mV,
bias ripple 7.523 mV, and active supply current 42.04–43.55 mA. Its four generic
Verilog-A cases pass, including 80/100/120-ohm loads and timestep refinement;
maximum error is 2.818 mV. All three declared process corners now pass: TT/27 C, SS/125 C and FF/−40 C.
SS maximum error is 8.158 mV. The four C++RTL cases and direct generic waveform comparison also pass;
maximum comparison difference is 11.295 microvolts.
Receipts live under `build/1G/tests/results/cascoded_core_transmit_driver/` and
`build/1G/IHPSG13G2/tests/results/cascoded_core_transmit_*`.

The maintained two-driver DC test exports the actual C++ circuit, checks its
strict process binding, then connects both output stages to one differential
line and a shared 0.25-ohm supply impedance. All 625 current/history combinations
are tested. All three corners pass: TT 11.171 mV, SS/125 C 11.635 mV and FF/−40 C
10.501 mV maximum per-driver error. The original stage is rejected by a
required negative regression. This is
a static output-stage test with ideal gate controls and nominal passives, not
a replacement for the complete translator/controller transient tests.

## Area and physical boundary

The selected transmitter needs 400 pF bias storage and 50 pF cascode-bias
storage. The 100 pF variant failed generic ripple; the 200 pF variant passed
generic tests but failed PSP at 22.497 mV error and 15.771 mV ripple. Neither
smaller variant is accepted. The compact 400 pF MIM bank exceeds the PDK whole-chip recommended MIM
area and remains a recorded negative chip-budget case. Actual MOS storage
provides a smaller alternative. Both banks together occupy 177,703.5074 square
micrometres, 52.17% below the two-bank MIM reference footprint. The complete
process transmitter with both replacements passes TT/27 C, SS/125 C and
FF/−40 C: maximum errors 9.410, 13.190 and 11.993 mV. Shared-line static tests
also pass at all three conditions. Preset trim codes remain characterization,
not automatic calibration. [MOS storage and its explicit LVS correction](mos-storage.md)
document charge/leakage characterization, local geometry, the dedicated-MOSCAP
LVS adapter, retained global-density failures and integration limits.
Footprints remain distinct from routed macro or accepted die area.

The standalone receive filter's recorded GDS is 1.21 square millimetres;
mapped cell area decreased by 45.84 percent. Its DRC/LVS evidence applies to
the recorded source version. After the shared runtime-header merge, fresh alignment and receive-filter
native/RTL/IHP-mapped functional comparisons pass. Those functional runs do not
renew physical provenance automatically. Post-fill coupling extraction remains
outstanding.

Local MOS, MIM and polysilicon-resistor generators provide building blocks,
not an extracted AFE. Tiny coupling capacitors, resistor PVT/current limits,
common mode and termination at the actual line interface, physical clock
recovery, complete PHY integration, pads/ESD integration, full-chip DRC/LVS and
extracted performance remain open. There is no complete-chip GDS qualification.

## ADC slice-count study

The [parameterized SAR bank controller](../adc_interleaved_bank.md) passes native
and converted-RTL checks with exact complete trajectories for 12, 13 and 16
slices. Thirteen and sixteen each deliver all 4,096 independently checked
samples at one result per controller clock. The twelve-slice negative case
reports premature reuse. A thirteen-slice analog bank would reduce slice count
by 18.75%; the physical receiver remains at sixteen until closed electrical and
routed timing checks establish that saving. No count ratio is reported as GDS
area.


## Cold differential receiver

The new [cold-start differential receiver](differential_receive_frontend.md)
connects the passive input network, both polarities of one amplifier, the PGA,
a physical ADC-range reference and real charge-balanced SAR slices. Every
explicitly stored capacitor voltage starts at zero and both supply rails ramp
up. The thirteen-slice controller has a separate native/RTL/IHP mapped proof.
The default analog class remains at sixteen slices during qualification.

The precharge-only revision precharges real storage through its tracking switches
while the rails rise. Its twenty-micrometre cancellation dummy replaces the
thirty-micrometre cold-start trial. The latter failed the single-slice gain-8
PSP acquisition bound at 23.215 mV; a retained continuous diagnostic reproduced
that failure before testing smaller dimensions. That revision's one-slice gain-8
native and PSP runs pass. The thirteen-slice gain-4 native and Verilog-A runs
pass with zero maximum code error. The complete gain-4 PSP run passes with 12.836 mV acquisition error,
14.018 mV hold variation and two-count maximum code error. These runs still
use the earlier 100 ps host edge. The complete gain-8 PSP run fails: acquisition
23.817 mV, hold variation 24.347 mV and three-count maximum code error.
Historical source snapshots and original waveforms are retained.

Its gain-8 native test fails acquisition at 25.352 mV. Investigation
found an inconsistent control edge: the live SPICE bridge uses 100 ps while
the experiment exports 2 fs and native switching is instantaneous. Matching
the exported edge in a separate copied Verilog-A deck exposes 23.256 mV
acquisition error. Refining its grid to 2 ps gives 23.149 mV, confirming that
the grid difference does not explain the failure. The earlier 100 ps passing
waveform cannot qualify equivalent stimulus. The maintained live bridge now uses and validates the exported edge; its
regression checks both 2 fs and 100 ps declarations and rejects invalid values.
Earlier passing receipts do not qualify the revised bridge. Neither copied
experiment changes the maintained circuit or constitutes a physical
control-driver proof.

The contract now checks the captured input and every held conversion state
against the ADC input window, and all PGA states against achieved supply
rails. Between-aperture conversion-window excursions are retained as
characterization. Seven negative/boundary tests enforce that distinction,
the final-decision hold interval, cold precharge and the original 20 mV
sampling/hold and two-count code limits. The analog default remains sixteen
slices until the complete candidate is qualified.

The preceding low-kickback revision combines thirty-percent stronger sampling switches
with smaller conversion preamplifiers. It keeps 1.5 pF hold storage and both
dummies unchanged. The two preamp input transistors are halved in width;
load, degeneration and tail resistances are doubled, reducing intrinsic
input charge and current while preserving approximate DC gain. A matched
one-slice 2 fs process diagnostic passes at 16.976 mV acquisition, 10.531 mV
hold variation and one-count code error. A 1 pF storage-only diagnostic fails
hold at 22.580 mV and is rejected. The native full-bank 1 pF trial was stopped
when this process rejection made it unnecessary; no native verdict is claimed.
The declared one-slice circuit now passes fresh native, Verilog-A and PSP
flows; generic codes agree exactly. PSP acquisition/hold errors are
16.976/10.531 mV, with one-count maximum code error. Complete thirteen-slice
Verilog-A tests pass both gain settings. The restarted complete native tests
also pass: gain 4 has 1.994 mV acquisition error, 3.024 mV hold variation and
zero-count code error; gain 8 has 19.607 mV acquisition error, 6.070 mV hold
variation and two-count code error. All 26 codes agree exactly with Verilog-A
at each gain. Gain 8's native acquisition margin is only 0.393 mV against
the unchanged 20 mV bound. Both complete PSP reruns finished. Gain 4 passes:
12.805 mV acquisition error, 11.835 mV hold variation and two-count maximum
code error. Gain 8 fails acquisition and conversion accuracy: 26.329 mV
acquisition error and three-count code error; hold variation is 17.431 mV.

The current sampler candidate uses main NMOS/PMOS widths 37.5/75 µm and
dummy widths 20/42 µm, with explicit finger counts 5/10/2/5. Hold storage
remains 1.5 pF and the smaller conversion preamplifier is unchanged. All five
isolated input phases pass the original limits: worst acquisition 17.479 mV,
hold variation 15.964 mV and code error two counts. Both dummy polarities
matter; improving only the earlier worst phase left another phase failing.
The complete thirteen-slice, two-burst tests now pass at gains 4 and 8 in
C++RTL, Verilog-A and IHP PSP under `balanced_sampler_13_gain{4,8}`. Each
flow independently passes the original 20 mV acquisition/hold and two-count
conversion bounds. PSP acquisition/hold maxima are 8.700/13.562 mV at gain 4
and 17.775/19.477 mV at gain 8. Both generic comparisons pass the unchanged
one-count tolerance: gain 4 agrees exactly; six gain-8 codes differ by one
count. The source, artifact and complete-waveform hashes were independently
audited, and the acceptance limits were recomputed.

This qualifies the declared nominal fixed-control bank fixture. The gain-8
hold margin is only 0.523 mV; real clock controls, process corners, mismatch
and extracted loading remain separate requirements. The default analog slice
count remains sixteen. Source and geometry audits preserve the earlier failed
revision. The [circuit explanation](differential_receive_frontend.md) describes
the measured DAC-to-held coupling and exact selected geometry.

The four full-bank native/PSP runners were interrupted before producing
verdicts. Their orphan simulator processes could no longer reach a live
waveform collector. Those run directories are retained with `interrupted`
status; they are neither electrical failures nor passing evidence. Fresh
reruns used the same frozen circuit and checks and have now completed as
reported above. The completed Verilog-A receipts retain valid source hashes
and were reused for comparison.

The ADC-range reference uses a physical 1275/1000-ohm divider, targeting
1000/2275 of achieved Vdd (about 1.45055 V at 3.3 V). Under an eight-channel
equivalent 112.5-ohm load stepped through 0.9..1.5 V, it passes both generic
flows, their 5 mV comparison and TT/27 C, SS/125 C and FF/−40 C PSP. Settled
process errors are 8.445, 8.289 and 7.648 mV against the original 15 mV limit.
Its broader 1.1..1.9 V load characterization failed at 16.252 mV and remains
recorded. The connected midrail-reference input-range sweep passes all four
nominal cases (line means 2.1/2.5 V, gain codes 4/8); PGA terminals stay within
the stated 0.9..1.5 V reference load envelope. The corresponding nominal/slow/fast connected input sweeps also pass.
These unit conditions do not qualify an eight-lane assembled reference.

The complete thirteen-slice controller plus calibration and tags passes 8,320
native/RTL clocks with every observed output equal: continuous and gapped
requests, changing calibration, clipping, reset/restart, precharge and disable
flush. Mapped and fanout-repaired IHP gates pass the same independent tests.
Before placement, the revised precharge-capable controller has slow-corner
cell-only setup slack +0.51 ns at 4 ns, including 0.16 pF analog-control
loads, 0.4 ns input/output delays and 0.1 ns uncertainty. Its repaired LEF
cell footprint is 76,917.8592 µm². These figures do not establish routed
full-lane timing or die area.

The [composed receive lane](../adc_receive_lane.md) adds real digital startup
sequencing: 4096 precharge clocks, four opening clocks, then conversion.
Native, converted RTL and IHP-mapped gates pass 15,468 clocks and 2,914
independently checked samples. Two 500 µm routed candidates failed setup
by 0.06 ns on `valid_out` and 0.09 ns on `acquire_out`. The current composition
removes sequentially redundant ready gating from those outputs; its full
trace remains unchanged, and 60,000 additional native clocks compare reset
and enable boundaries against the original nested bank. Fresh native/RTL/
mapped checks pass. That direct composition's route then failed setup by
0.04 ns inside the normalizer, with +0.04 ns hold slack. Its partial-product
adder now uses carry selection; 409,477 independent unit clocks and the
unchanged full lane trace pass all digital flows. That route passed exact
gate replay but failed setup by 0.173278 ns on the rounding/clipping path;
hold passed at +0.053446 ns. Direct carry/sign decoding now shortens that
path without changing latency. The unit and full lane pass fresh native,
RTL and mapped checks, and the native boundary test passes. The new route
passes setup at +0.077566847 ns and hold at +0.053977489 ns, with exact routed
gate replay. Full DRC passes 868 categories with zero violations. Strict LVS
on the same filled geometry passes 238 ports, 45 circuits and 67,292 devices
when the three proven disconnected compatibility pins are preserved through
simplification. The fresh complete physical run including this verification
adapter now passes extraction, exact replay, fill, DRC and LVS with the same
timing margins. Its 500 × 500 µm filled digital module occupies 0.25 mm².
Measured comparator-return hold buffers
prevent claiming connected analog timing under the current uniform I/O
constraints. Actual analog startup integration and post-fill coupling
extraction remain open.

The standalone raw thirteen-slice controller has a recorded 400 × 400 µm
layout with exact native/post-route trajectories, +1.24/+0.08 ns extracted
setup/hold slack, strict transistor LVS and zero violations across 868 DRC
categories. This excludes calibration, startup, analog circuitry and chip
integration. The corrected 360 × 360 µm controller now also passes the complete flow:
exact 8,256-clock trajectory, +1.10/+0.06 ns extracted setup/hold slack,
zero violations across all 868 DRC categories, and strict LVS (39 matched
circuits, 163 top ports and 34,832 instantiated devices). Its 0.1296 mm²
layout is 19% smaller than the 400 µm layout. Metal2/3/4 global densities
are 36.45/36.99/37.49%. The initial failed result remains retained. The
accepted correction adds exactly 0.01 µm² of Metal3 at one routing corner,
uses legal fill, and restores array bit identity after DEF import through
a separately tested simulation adapter. This remains a standalone raw
controller result; full-lane and chip qualification are open. See the
[physical flow](adc_bank_physical.md) for reproduction and evidence boundaries.


## Shared input common-mode candidate

The [lead-compensated reference](RECEIVE_COMMON_REFERENCE.md),
`lead_receive_common_reference`, passes fresh native, Verilog-A, and IHP PSP
TT/27 °C, SS/125 °C and FF/−40 °C checks with eight finite-source input
networks. Its physical 10 kΩ/12.15 kΩ divider declares VDD × 243/443
(1.810158 V nominal). The 150 µm HV sink uses a 1000 Ω/2 pF compensation
branch and **96 pF total output storage** (80 pF internal plus 16 pF external).

All original limits remain: 15 mV settled, 100 mV transient, 2 mV divider
tracking, and 1.80–2.00 V input common mode. Worst PSP results are 11.957 mV
settled, 89.368 mV transient, and 1.804613–1.996397 V input range across
those corners. Every-state source-profile, exact passive RC, rail, drive,
excitation and 6–20 mA loading checks pass. Native/Verilog-A maximum waveform
difference is 0.574 mV against the unchanged 5 mV bound.

Receipts are under `build/1G/IHPSG13G2/tests/results/lead_receive_common_centered96/`,
with corner receipts in the sibling `_ss_hot/psp/` and `_ff_cold/psp/` directories.
`qualification_audit.json` verifies all five source-hash sets and byte-identical
physical decks against passing retained diagnostics. `official_target_audit/checks.json`
connects those receipts to the normal CMake bench: stimulus, conditions and
mapped deck match byte-for-byte; generic contributions match after source-path
comment normalization. The normal generators updated only the two lead-reference
models. All four CTest contract, source-profile, explicit-target and physical-binding
guards pass; the binding test rejects twelve deliberate mutations.

Earlier 6/11-target failures remain preserved, including the original shunt
regulator and the nominal-only 200 µm/84 pF candidate. Their source snapshots
and diagnostic receipts are retained. The centered96 alternative improves
SS input margin from 0.276 mV for original-target108 pF to 3.603 mV while
saving 12 pF.

This is **not yet integrated into the receiver**. The bench contains passive
100 Ω/150 fF input loads, not eight active amplifier/PGA/ADC chains. It covers
MOS process/temperature corners with nominal R/C, not resistor/capacitor
variation, mismatch, extracted distribution, pads/ESD, or complete PHY behavior.
The established passive input network remains the selected baseline.


## Core-to-DAC interface candidate

The [translated receive DAC](core_receive_dac.md) connects eight 1.2 V trial
controls to actual level shifters and a 3.3 V R/2R ladder. Complete refined
sixteen-word runs pass C++RTL, Verilog-A and IHP TT/SS/FF, including the
expanded input-PMOS exposure check. Maximum aperture errors are 0.492464,
0.491783, 0.953715, 1.889146 and 0.672082 counts, respectively, against two
counts. Worst checked core-device exposure is 1.437201 V against 1.65 V.

The comparison now checks both simulators' observed grids and passes at
3.192400 mV against 5 mV. It independently rechecks the absolute results
and source/waveform hashes. A regression rejects a spike visible only on
the SPICE grid. The earlier 119.577 mV switching discrepancy remains
recorded; timestep refinement resolved it without changing the circuit,
excitation or limits. Native switching uses 50 fs steps and SPICE's declared
measurement maximum is 0.5 ps. This qualifies the declared DAC unit fixture;
the receiver still needs the connected control and comparator paths and the
[physical half-cycle budget](receive_timing_budget.md).

The new [connected decision circuit](core_receive_decision.md) adds real
evaluation/clear translators, the preamplifier, comparator, decision latch
and core return. Its generic Verilog-A unit, current TT/27 °C and FF/−40 °C
process units pass all sixteen decisions. TT and FF have 1.369 and 1.002
counts maximum DAC error, respectively.
The current SS/125 C process run fails fifteen return windows and reaches
2.149 counts of DAC error against the two-count bound. A separate TT
diagnostic with delayed controls, finite slew and 35 fF return loading fails
eight return windows. Stronger latch buffers and reduced preamplifier storage
are isolated sizing experiments, not qualified replacements. Contract and
physical mapping guard tests pass, but there is no connected timing
qualification yet. The second-order native solver experiment passes the full generic
native/Verilog-A comparison with the original 5 mV bound. The maintained
decision test now selects that integration method and refined time grids;
the physical slow-corner circuit remains unqualified for this path.

The reduced-storage sizing experiment makes all sixteen final decisions
correct, but seven remain late: its latest stable return is 3.819 ns after
reducing return-node capacitance, against the 3.6 ns window start. DAC error
remains 2.476 counts. A new input-clear latch removes large reset clamps from
the retained outputs; its settled logic and topology tests pass, but its
complete slow/hot result still has seven late decisions (latest 3.821 ns) and
2.475 counts of DAC error. Its wrapper also flags a shared digital test-runner
edit; the executed dependency helper is unchanged, and the original failure
receipt is retained. A separate artifact audit reproduces both candidates
byte for byte. The asymmetric DAC candidate passes the threshold limit at
1.911558 counts, but still misses seven return windows (latest 3.766500 ns).
Reducing explicit evaluation-translator storage to 50 fF and retained latch
storage to 20 fF improves the latest return to 3.721029 ns and DAC error to
1.881594 counts. All sixteen final decisions are correct, but seven still
miss the unchanged 3.6 ns window start. The complete 640,179-state SS/125 C
run finished with intact source and waveform checks. Intrinsic PSP charge
remains present. These candidates do not replace the maintained circuit.

The experimental second-order native integrator passes exact RC/RL/LC,
nonlinear MOS discharge and coupled-capacitor solutions, plus event,
continuation and rejected-step regressions. A refined first-word comparison
passes at 1.766 mV. The complete sixteen-word native run now passes all
absolute checks. The refined Verilog-A simulation completed, but its wrapper
timed out while verifying the 1.1 GB compressed trace. The complete gzip stream
and simulator row count are retained. A separate bounded-memory audit
verifies the complete archive and original dependencies; its absolute checks
and full comparison pass. Maximum difference is 1.814582 mV on both grids,
within 5 mV. The original wrapper failure remains recorded. The maintained
solver now exposes [explicit BDF2 selection](../../doc/integration.md), while
backward Euler remains its default. The maintained decision fixture uses
125 fs BDF2 steps after a refined final warmup and a 100 fs maximum generic
SPICE step. Trace capture now waits for complete archive verification, and
row validation avoids duplicating millions of states. The historical full
comparison receipt belongs to the recorded experiment; it is not relabeled
as a fresh run of the maintained fixture.

The [shared sampling-control prototype](shared_receive_track.md) fixes
premature/repeated selection events in its second generic version. Its full
32-event edge audit passes, but the physical gate path closes about 0.84 ns
after the declared aperture and reaches 78.313 mV sampling error. Its process
run also fails: all thirteen physical closing edges occur, but the closing
delay is 1.322828–1.334000 ns and maximum held-target error is 111.214 mV.
EVENT also remains above 20% of VDD into its required idle window. The
complete process trace passes startup and exposure checks but cannot qualify
the original 20 mV sampling bound. A common sampling delay may be absorbed by data-driven receive timing,
but that requires a hardware resampler fed only by fixed-rate ADC samples.
The new resampler and tagged symbol-valid interface pass 504,533 exact
native/RTL clock states across twelve fixed-sample phase/rate cases: zero
symbol errors/slips, maximum phase error 0.0442814 UI and interpolation
departure 0.883707 ADC count. This is a digital unit result, not an analog
aperture qualification. PCS clock enables now pass the independent frame,
fault and poisoned-pause regressions. The clock-alignment FIFO also passes
720 frames in native/RTL with fixed-rate enabled input. Composed fixed-sample Ethernet testing now passes 18 phase/rate/role cases,
36 frames and 28,764 bytes, with 796,260 exact native/RTL clock states. This
fixture uses a declared idle-training interval. The first 64-clean-idle
readiness candidate was insufficient: retained earliest-link tests lost both
frames while receive timing still had about 1426 ppm period error. The
[measured timing-confidence gate](../receive_timing_confidence.md) now requires
bounded detector bias, period movement, excitation and absolute error over
accepted observation windows before the PCS idle qualifier can assert ready.
Its standalone native/RTL proof passes 28,429 exact clock states. The maintained
confidence-qualified composition passes all 796,260 clocks and the original
36 frames without an elapsed PMA-enable timer; its receipt is
`out/receive-sample-pcs-qualified/v4/qualification/checks.json`.

The separate earliest-legal-peer regression now passes all thirty rate/phase/noise
cases with native and RTL receive paths: 60 long frames, 91,560 bytes and
1,962,253 identical complete-clock states. Rates cover −1900/−1500/0/+1500/+1900
ppm, delays 0/0.105/0.9 UI, and zero or bounded ±2-count ADC noise. Actual PMA
handshake and the earliest clean SEND_N arm event determine frame launch; no
training timer delays transmission. Independent auditing verifies all 981,134
transmit symbols against the previously IEEE-replayed source stream, including
source index, side, frame-enable and source-byte equality. Receipts and archive
hash audits are in `out/receive-earliest-peer/maintained-v2/`. The transmitter,
PMA control and causal channel remain native test fixtures in this comparison;
it is not a complete RTL PHY or physical clock qualification. The asymmetric
free-running slave clock is a stress stimulus, not recovered transmit-clock
implementation. Seven current targeted digital CTests pass, and the immutable
converter catalog check passes all 48 RTL/package files. Mapped PCS frame,
fault and pause traces also match, with 42,388 IHP cells. Its 4 ns slow-corner cell timing fails at
−3.38 ns slack; the forced-metric selection and trellis arithmetic lie on the
critical path. A retimed selection experiment keeps the existing pipeline
latency and matches all native, RTL and mapped traces with 42,212 cells.
It still fails 4 ns timing at −2.39 ns slack. A separate 7 ns clock study
passes cell-only timing at +0.61 ns slack; that feasibility result does not
waive the failed 4 ns contract or establish routed timing. The new
[symbol clock crossing](../receive_symbol_crossing.md) passes 70 phase/rate
cases and 236,950 exact native/RTL event states, including 70,047 complete
bundles. Its 4 ns ADC / 7 ns PCS boundary is a unit result; clock generation,
physical CDC constraints and full receiver composition remain outstanding.
Slice mismatch,
jitter and SAR setup/hold remain separate physical requirements. A physical
edge-receiver diagnostic exists, but additional PLL feedback circuitry is
deferred until the fixed-sample recovery path is tested.
