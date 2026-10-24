# Connected trial-to-decision path

[`core_receive_decision`](core_receive_decision.hpp) connects the
[core-controlled DAC](core_receive_dac.md) to a differential preamplifier,
StrongARM comparator, buffered decision latch and high-to-core return
interface. Two additional physical translators receive core-voltage
evaluation and clear controls. All ten translators load the DAC's shared
25 pF replica bias. The class exposes member ports; it contains no simulation
driver or ideal decision function.

The preamplifier uses the cold receiver's smaller input devices and
1 kΩ / 100 Ω / 300 Ω load, degeneration and tail resistors. Its two inputs
load the external input and actual ladder output. Comparator regeneration
therefore influences the preceding circuit. The retained decision drives
the same physical `level_down` interface used by the receiver bank.

The new experiment checks the connected timing budget before integrating
the sampler and routed controller. Sixteen 4 ns trials cover codes 96, 112,
127 and 128, including the major-carry transition. The second pass reverses
the expected decision at every word. Input voltages sit 2.5 nominal ADC
counts above or below each trial threshold. Commands rise in 100 ps through
50 Ω; supplies ramp from zero through finite source impedances. Every
capacitor starts uncharged, followed by an uninterrupted 8 µs warmup.

The evaluation command rises at 2 ns. Its physical translator determines
the comparator's actual clock arrival. The returned decision must be valid
throughout 3.6–3.95 ns: at least 0.84 V for high, at most 0.36 V for low.
The DAC keeps its separate two-count settling limit throughout 1.5–2 ns.
Supply, bias, clear release and core-device terminal exposure are checked
over every recorded state. Core exposure must remain at most 1.65 V.

`tests/core_receive_decision.py` runs the same absolute checker separately
on C++RTL, generated Verilog-A and explicitly bound IHP PSP transistors.
The generic comparison then checks both observed time grids with a 5 mV
bound. The maintained native fixture selects BDF2 with 125 fs steps. Generic
SPICE measurement steps are at most 100 fs; PSP retains its 0.5 ps maximum.
Cold warmup uses coarser continuous integration, refining the last 200 ns to
10 ps in the native flow without resetting state.
The physical fixture contains 174 MOS devices, 37 resistors and 303 explicit
capacitors. Device geometry and important signal connections are validated
before simulation; unknown or altered contributions are rejected.

CTest registers `analog.1g.ihp_core_receive_decision.*`, including independent
contract and mapping guards, native/Verilog-A comparison, and TT/SS-hot/FF-cold
process runs. The six contract guard groups and fourteen negative mapping
mutations pass. Circuit qualification is in progress; the new class is not
yet substituted into the complete receiver.

The input in this experiment is actively driven through 50 Ω and loaded by
1.5 pF. It cannot establish sampled-input acquisition accuracy or held-node
kickback. Those require the actual sampler. Commands also remain testbench
sources: routed clock-to-Q, control routing and clock skew must be included
in the subsequent [connected timing checks](receive_timing_budget.md).

The initial nominal IHP PSP run passes all sixteen decisions. Its maximum
DAC aperture error is 1.369370 counts and maximum core-device exposure is
1.421307 V. The evaluation half-rail arrives 0.479–0.484 ns after the command.
This is a driven-input unit result; routed-controller integration remains
subject to the boundary requirements above. The original source snapshot
and waveform are retained while the current revision exposes the shared
bias through a public DAC port.

A first-word native backend comparison checks all 80,000 electrical states.
Certified sparse LU takes 300.1 CPU seconds versus 639.0 for dense solving;
the maximum observed voltage difference is 7.88e-12 V. The test uses that
optional sparse backend when Eigen headers are available, retains a dense
fallback, and advances in continuous 4 ns segments to respect the engine's
per-call step limit. Native CSV bytes now stream directly into a lossless
archive, which is verified before the test accepts it. The initial CMake target did not propagate
Eigen's include path. That dense, first-order full run was stopped after an
independent first-word comparison showed a 58.138 mV error against the
5 mV limit. Its interrupted receipt and the supersession reason are retained;
it is not a completed circuit verdict. The target now links the optional
Eigen dependency for subsequent builds. The refined experiment uses sparse
solving; the original executable remains part of the recorded process evidence.
The current Verilog-A run passes all sixteen decisions, with 0.347912-count
maximum DAC error and 1.360260 V maximum core exposure. Its return remains
valid from at most 3.295206 ns after each trial command. The current FF/−40 °C
process run also passes all sixteen decisions: maximum DAC error is 1.002301
counts, input error 0.131 mV and core exposure 1.389999 V. The fresh current-
revision TT/27 °C run also passes all sixteen decisions: maximum DAC error
is 1.369370 counts, input error 0.186 mV and core exposure 1.421307 V.
The refined full native/Verilog-A comparison passes for the isolated
second-order solver described below.

The current SS/125 °C run fails the original contract. Fifteen of sixteen
returned decisions miss some part of the 3.6–3.95 ns window, and maximum DAC
error is 2.148684 counts against the unchanged two-count limit. Input error
is 0.297 mV and core exposure is 1.436319 V; supply, bias, release and voltage
range checks pass. The physical comparator clock crosses half-rail at
2.574–2.581 ns. This result rejects the current connected circuit at SS/hot.

The retained waveform separates several delays. At the first code-128 trial,
the preamplifier differential still has the old sign at 2.5 ns, becomes correct
by 3 ns, and the latch output has only fallen to 0.854 V at 3.95 ns. At the
first positive trial the latch reaches 2.990 V at 3.6 ns while the core return
is still 0.076 V. Consequently, changing only the return translator cannot
resolve the entire failure. An isolated sizing experiment strengthens the
two latch-buffer stages while retaining the original waveform, geometry
mapping and acceptance windows. That SS/hot experiment now passes eight
decisions, but the other eight still fail and DAC error remains 2.137775
counts. The first code-128 comparison resolves with the wrong polarity;
another falling comparison resolves correctly but reaches the core too late.
The 100 fF preamplifier experiment makes every decision correct by the trial
endpoint, but seven still miss the original complete return window. Its
latest stable return is 3.9415 ns, and early DAC error worsens to 2.475731
counts. Reducing return-node storage from 80 to 20 fF moves the latest stable
return to 3.818706 ns, but the same seven complete return windows still fail.
Maximum DAC error remains 2.475700 counts. Intrinsic PSP transistor charge
remains present. These capacitor
budgets need extracted-layout checks if the candidates pass; none is a
qualified replacement. The failed
production receipt is preserved under
`tests/results/core_receive_decision_shared_ss_hot/psp/` in the build tree.

A separate topology candidate clears the latch through its input gates,
removing the 120 µm NMOS and 240 µm PMOS clamps from its retained outputs.
This trades their drain charge for a NAND/NOR command pair and an explicit
clear inverter. It contains 176 MOS devices and 307 capacitors. Eleven
clear/set/reset/retention states pass independently in C++RTL and Verilog-A;
the settled outputs agree within the 1 mV unit tolerance. A strict mapping
audit rejects twenty-one altered connections, states and model parameters.
Clear release still requires inactive comparator commands. The complete
SS/125 °C run still fails seven return windows: latest stable return is
3.820588 ns, and DAC error is 2.475319 counts. The settled-state unit result
does not qualify a 4 ns path.

A separate DAC candidate uses independent pull-up/pull-down strengths and a
600/1200 Ω ladder. Its physical drivers are N100/P300 µm, compared with the
preceding N100/P200 µm. An isolated 256-code DC sweep at SS/125 °C reduces
maximum static error from 1.441493 to 0.868139 counts when only the PMOS width
changes and ladder resistance stays at 750 Ω. The connected candidate also
reduces ladder resistance; its increased translator load and changed supply
current must pass the complete transient test. No DC result qualifies that
timing tradeoff. Forty-four negative binding tests check its driver, ladder,
latch and storage topology, and all thirteen source definitions match the
preceding experiment exactly. The complete SS/125 °C waveform passes DAC
accuracy at 1.911558 counts, but seven return windows still fail. Every final
decision is correct; latest stable return is 3.766500 ns, exceeding the
3.6 ns window start by 166.5 ps. Input error is 0.562284 mV and maximum core
exposure is 1.436319 V. The original wrapper failure records a concurrent
shared test-helper edit. A separate dependency audit reproduces the exported
circuit and physical deck byte for byte and verifies that the executed helper
function did not change. It preserves the failed receipt and electrical
failures; it does not issue a fresh qualification pass.

The production Euler solver has not passed the generic waveform comparison. A first-word diagnostic
finds a 58.138 mV maximum difference between the archived 50 fs Euler result
and Verilog-A, concentrated at the latch transition; the core return differs
by 5.647 mV. Increasing the native step to 250 fs worsens the latch difference
against the 50 fs trajectory to 250.610 mV, so that shortcut is rejected.
An isolated second-order integration experiment reduces the Verilog-A
difference to 7.140 mV at 250 fs and 5.529 mV at 125 fs. Resolving the final
200 ns of warmup at 10 ps gives 5.429 mV, still above the existing 5 mV limit.
A finer independent Verilog-A run with a 100 fs measurement bound passes
both first-word absolute checks and comparison at 1.765753 mV. The same
settings now pass all sixteen words in the isolated second-order prototype: The incomplete dense
50 fs Euler job was explicitly stopped because its independently checked
first word already exceeds 5 mV; its supersession record is retained rather
than assigning it a full-sequence electrical verdict.
Analytic RC/RL/LC, event-boundary, continuation and rejected-step tests pass.
An additional regression exposed missing history restarts for analog-sensed
digital changes; the separate corrected prototype passes that post-event
convergence test too. The full native and Verilog-A trajectories each pass the original absolute
electrical checks. Comparing 512,000 native and 3,199,993 Verilog-A states on
both time grids gives a maximum difference of 1.814582 mV, below the unchanged
5 mV limit. The original Verilog-A wrapper confused a long compressed-trace
verification with missing producer EOF. Its failed receipt is preserved. A
separate audit validates the complete gzip stream, simulator row count,
source hashes and byte-identical deck, then runs the unchanged electrical
checks and comparison. Row validation uses the original validator on
overlapping bounded chunks to avoid duplicating millions of states. This
audit did not change the circuit acceptance criteria. The same integration
method is now available through `simulation.method = integration_method::bdf2`;
backward Euler remains the framework default. The maintained fixture adopts
the tested BDF2 settings, and its exported physical equations and complete
host stimulus match the experiment. Fifteen focused maintained CTests pass
for solver behavior, trace handling and decision guards/binding. This
export comparison and regression set are distinct from a new full transient
qualification of the maintained fixture.
The experimental comparison receipt is
`out/core-decision-bdf-audited-v2/comparison.json`.
Independent nonlinear MOS discharge and coupled-capacitor eigenmode tests
also pass: halving the step reduces maximum errors from 0.215 to 0.058 mV
and from 0.877 to 0.236 mV, respectively. Analytical and numerical Jacobians
agree. The prototype still uses a prescribed maximum step; these convergence
tests do not establish automatic local-error control.

The existing standalone `level_down` process test provides a separate sizing
reference: all 150 cases pass across five process corners, three temperatures,
five supply pairs and 20/200 fF loads. Its receipt and all 661 referenced
source/artifact files were rechecked. Measured output half-rail delays span
0.136–0.315 ns rising and 0.223–0.659 ns falling, relative to the start of
each 100 ps input command. The worst falling case is SS at 27 °C, with a
1.08 V core supply, 3.63 V input and 200 fF load. Hot is therefore not
automatically the limiting temperature for this ratioed circuit. These
isolated results do not qualify the connected comparator drive, finite shared
supplies or routed return; they support retaining the current size while
those interactions are tested. The maintained runner is
[`tests/level_down.py`](tests/level_down.py).

A separate driven-boundary diagnostic keeps this IC unchanged and delays
trial commands by 0.9 ns, uses 400 ps full control edges, and loads the return
with 35 fF. In Verilog-A all sixteen decisions are valid in the original
3.6–3.95 ns return window; the latest stable return is 3.549 ns. The unchanged
early 1.5–2.0 ns unit window fails, with 116.082 counts of transient DAC error
and 2.426 mV of input disturbance. In a separately reported 2.4–2.6 ns
diagnostic window, DAC error is at most 0.401 counts and the comparator clock
is still below 0.053 V. Those different windows are retained explicitly.
The same stimulus completes in nominal IHP PSP but fails eight of the
sixteen return windows, including returns still invalid at the trial endpoint.
Its early DAC error reaches 121.436 counts and input disturbance 4.398 mV;
the separately reported 2.4–2.6 ns DAC error is 1.351 counts. The physical
comparator clock crosses half-rail at 2.699–2.702 ns. The generic timing
result therefore does not qualify this physical boundary. This experiment
uses uniform delayed commands and a lumped return load; it does not establish
bit-to-bit routing skew, a physical clock branch, floating sampler behavior
or time available through the controller's return logic.

`tests/core_receive_decision_timing.py` characterizes a passing receipt without
changing its acceptance window. It verifies source and waveform hashes, reruns
the absolute electrical contract, and measures the last invalid return state
for each word. Half-rail transition times are reported as brackets between
observations. An already-correct retained value is identified separately from
a new transition, and late glitches move the measured settling boundary.
These measurements can inform connected I/O constraints only together with
all required process corners and the actual clock paths.

```sh
python3 analog/1G/IHPSG13G2/tests/core_receive_decision_timing.py \
    --receipt /path/to/current/flow/checks.json \
    --output /path/to/new/return_timing.json
```

The output fixture has 20 fF of load. The current routed controller's largest
input load is already 20.793 fF with nominal pin capacitances (21.565 fF at
characterized pin maxima), before connecting external analog wiring. These
unit results are not a qualification of that larger physical load.
