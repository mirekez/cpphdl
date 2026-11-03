# Receiver buffer repair

For a changed receiver implementation, validate its native/RTL behavior and
map fresh gates before reusing any physical flow. `receiver_mapped.py` accepts
explicit ABC delay and load targets; they request timing-driven mapping without
changing the physical clock period. For example, from `analog/`, after
`hardware.py --case receiver` has produced a passing `native_rtl_checks.json`
in `build/receiver-candidate/`:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/tests/receiver_mapped.py \
    --work build/receiver-candidate --pdk pdk/IHPSG13G2 \
    --tools out/repeater-toolchain --verilator ../.conda/bin/verilator \
    --abc-delay-ps 3000 --abc-load-ff 15
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/tests/cell_timing.py \
    --module PCSReceiver --period 7.352941176 --repair --require-functional \
    --work build/receiver-candidate --pdk pdk/IHPSG13G2 \
    --tools out/repeater-toolchain
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/physical_module.py \
    --module PCSReceiver --mapped-work build/receiver-candidate \
    --work build/receiver-physical --die-size 2400 --global-route-only \
    --pdk pdk/IHPSG13G2 --tools out/repeater-toolchain \
    --verilator ../.conda/bin/verilator
```

Run each stage only after the preceding checks pass. ABC uses picoseconds for
the delay target and femtofarads for output load, with a characterized
`sg13g2_buf_4` input driver. The generated constraints and synthesis script are
bound to the mapped replay receipt. Omitting the delay target retains the
previous mapping behavior. Neither ABC's target nor a passing cell-only timing
estimate establishes routed timing closure. A changed netlist requires fresh
placement, clock-tree synthesis, routing, extraction, replay and layout checks.

`receiver_buffers.tcl` generates a limited physical ECO candidate for
`PCSReceiver`. It removes only short, unbranched `wire<number>` buffers with
an upstream buffer of at least the same drive strength and a downstream
buffer. Clock nets, external ports, special nets, fixed cells and objects
marked `do_not_touch` are excluded. The maximum span is 20 µm, with at most
eight passes. The original checkpoint must be retained.

After loading the receiver database, Liberty models, timing constraints and
propagated clocks in OpenROAD:

```tcl
source 1G/IHPSG13G2/receiver_buffers.tcl
write_verilog before.v
remove_receiver_series_buffers 20 8 removed_buffers.csv
write_verilog after.v
```

The span measures the Manhattan distance between neighboring buffer centers.
It is a candidate-selection bound, not a bound on the eventual routed wire.
Removing delay can damage hold timing, even when connectivity is unchanged.
This helper is therefore not enabled in the production physical flow.

Check every candidate independently:

```sh
python3 1G/IHPSG13G2/buffer_equivalence.py \
    --before before.v --after after.v --output equivalence.json
```

This restricted flat-netlist check requires unchanged surviving instances,
cell masters, pins and top-level ports. It contracts only removed IHP buffers
or the three `sg13g2_dlygate4sd*_1` cells whose Liberty function is `X = A`,
then compares every surviving connection. Unsupported syntax and any other
removed cell fail the check. Net renaming is allowed. The proof covers Boolean
connectivity; it does not qualify timing, electrical limits, routing or layout.

Before integration, run setup and hold analysis at the required corners,
electrical checks, routing and extraction, physical netlist equivalence,
DRC and LVS. Keep those acceptance limits unchanged.

The regression tests exercise incorrect logic rewiring, interface changes,
inverter removal, delay-cell scope, invalid netlists and real OpenDB selection
guards. From the analog directory:

```sh
cmake -S . -B build/behavior
ctest --test-dir build/behavior -R 'ihp_receiver.buffer_' --output-on-failure
```

The OpenDB guard test is registered when the local OpenROAD tool is available.

## Diagnosing a failed global-route checkpoint

`receiver_global_unbuffer.py` starts from the retained `finished_global.odb`
and SDC of a receiver physical run. It rebuilds nominal global routes, checks
the baseline against the recorded timing within 1 ps, then tries four bounded
rounds of data-buffer removal by default (`--rounds` accepts 1–16). Each round retains its timing/electrical reports
and must preserve exact surviving endpoints. Route coverage warnings reject
the diagnostic. No extracted timing or layout pass is inferred.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_global_unbuffer.py \
    --seed build/receiver-physical/PCSReceiver-RUN \
    --output build/receiver-global-unbuffer --span-um 150
ctest --test-dir build/behavior \
    -R 'ihp_receiver.(global_unbuffer_native|local_buffer_guards|buffer_equivalence)' \
    --output-on-failure
```

The default neighbor span remains 60 µm. This diagnostic can explicitly select
up to 200 µm; existing callers keep their 60 µm bound. Clock nets, ports,
register loading, protected cells and branched inputs remain excluded. The
larger span is only a candidate selection limit: fresh routing, both timing
corners and electrical checks determine whether the result is useful. The
native regression checks a real register loop, exact endpoints, setup gain,
hold/electrical limits and an independent reload of routed RC.

`--target-slack 0.5` includes paths below 0.5 ns estimated setup in selection.
`--electrical-repair` repairs violations after the removal rounds, without extra
slew/capacitance margins, then legalizes and rebuilds all global routes. This
mode proves inserted identity buffers and equivalent drive changes against the
retained cell models. `global_unbuffer_repair_native` exercises removal and
repair of a real overloaded output. A later invocation may use a completed
diagnostic as its seed; it verifies the saved checkpoint hashes and netlist
before continuing. Neither a positive estimate nor a completed diagnostic
substitutes for detailed routing, extraction, replay, DRC or LVS.

`receiver_route_probe.py` accepts a completed diagnostic only when its global
setup and hold each exceed 0.1 ns and its electrical report passes. It performs
early detailed routing through iteration 1 (initial pass 0 and repair pass 1),
restores physical fillers, and measures
fresh OpenRCX timing in a separate process. Exact signal endpoints must survive.
The receipt always leaves timing, layout and chip qualification false, including
when the router report happens to be empty. This avoids spending a full routing
closure run on a candidate whose actual wire loads already miss timing.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_route_probe.py \
    --seed build/receiver-global-unbuffer --output build/receiver-route-probe
ctest --test-dir build/behavior -R 'ihp_receiver.route_probe_' --output-on-failure
```

To continue a diagnostic candidate after this extracted screen:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_continue.py \
    --repair build/receiver-global-unbuffer \
    --route-probe build/receiver-route-probe \
    --constraints build/receiver-global-unbuffer/candidate.sdc \
    --mapped-work build/behavior/1G/tests/hardware/receiver \
    --output build/receiver-continuation
```

The probe may still be running; the continuation waits outside the EDA slot.
It verifies that the measured reports, netlist, constraints, libraries and
checkpoint belong to the same candidate. Negative extracted setup/hold or
electrical violations stop the sequence. The full route starts from the
unchanged global checkpoint, reproduces its timing within 1 ps, and disables
the early-stop option explicitly. It then runs complete detailed routing,
new extraction, exact normal/fault/enable replay and layout verification.
An early probe never supplies the final routing pass. The `route_handoff_native`
CTest exercises both routes and extraction with actual IHP cells;
`route_resume_guards` and `continuation_guards` cover rejected handoffs.

## Deferring physical fillers

Detailed routing can defer the four physical filler masters `sg13g2_decap_8`,
`sg13g2_decap_4`, `sg13g2_fill_2` and `sg13g2_fill_1` to reduce memory use.
The explicit Python API `compare(before, after, defer_fillers=True)` accepts
only removal of those masters with empty pin lists in the signal-netlist
export. Functional-cell removal, added cells, changed masters and rewiring
are rejected. The ordinary buffer check still rejects filler removal.

The candidate must separately preserve every functional instance's location,
orientation and placement status. Restore fillers before extraction and full
physical verification: signal-netlist equality cannot prove the supply
connections or layout legality of decoupling cells. Interface checks preserve
port order and bus direction as well as individual bit connections.

## Cell sizing

Timing closure takes priority over area. `sizing_equivalence.py` checks a
different, sizing-only ECO: every instance and connection must survive, while
drive variants may change if their retained Liberty-derived Verilog models
have exactly identical bodies. OpenROAD can also replace a delay cell with
a buffer; those replacements need identical models and fresh hold analysis.
Scalar output aliases are checked as directed identity connections.

```sh
python3 1G/IHPSG13G2/sizing_equivalence.py \
    --before before.v --after after.v --models cells.v \
    --output sizing_equivalence.json
ctest --test-dir build/behavior -R 'ihp_receiver.sizing_equivalence' --output-on-failure
```

Use the `cells.v` retained by the mapped functional qualification, and verify
its recorded hash. This test does not treat a cell's name as proof of its
function, and rejects changed pin connectivity or a missing model. Timing,
electrical and complete physical acceptance are still separate requirements.

For a completed routed receiver extraction, evaluate sizing using its real
SPEF rather than the global-route estimate:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_extracted_sizing.py \
    --routed build/receiver-physical/PCSReceiver-RUN \
    --output build/receiver-extracted-sizing
```

The input directory must contain `physical_module.json`, `core.odb`,
`core.spef` and the retained `cells.v`. The output directory must be new.
The command restores the original timing contract at full precision, records
input/tool hashes, serializes its EDA work and runs the sizing-equivalence
check. `checks.json` reports before/after estimates and the resized cell count.
The existing wire RC is held fixed during this experiment: changed pin shapes,
legalization and rerouting require new parasitic extraction before acceptance.
Neither `diagnostic_complete` nor functional sizing equivalence qualifies the
candidate for integration.

The flow restores the saved guides before resizing, to initialize OpenROAD's
router net map. Without that step the pinned router crashes in `addDirtyNet`
when resizing a freshly loaded detailed-routed database. The self-contained
regression generates and routes a three-buffer fixture, then reloads it in a
separate process and checks sizing and signal endpoints:

```sh
ctest --test-dir build/behavior -R 'ihp_receiver.routed_sizing' --output-on-failure
```

Its estimated fixture SPEF tests tool-state recovery, not physical timing
accuracy. The production candidate uses the receiver's extracted SPEF.

## Guarded clock and hold-delay sizing

`receiver_tradeoffs.py` takes a completed extracted-sizing candidate and an
explicit JSON list of `[instance, target_master]` trials. It permits only
transparent buffer/delay replacements with identical retained Boolean models.
Every accepted trial must preserve at least 0.1 ns hold margin, improve setup
or total negative slack, and worsen neither measure by more than 1 ps.
Rejected trials restore the previous master. No endpoints or constraints change.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_tradeoffs.py \
    --routed build/receiver-physical/PCSReceiver-RUN \
    --sized build/receiver-extracted-sizing \
    --trials build/receiver-trials.json \
    --output build/receiver-timing-tradeoffs
ctest --test-dir build/behavior -R 'ihp_receiver.timing_tradeoffs' --output-on-failure
```

The trial process keeps the extracted networks in memory and updates cell/pin
loads. Repeated SPEF reloads exhaust the pinned OpenROAD memory budget on the
full receiver. A separate process reloads the final ODB and original SPEF;
both setup and hold must reproduce within 1 ps before the diagnostic completes.
The output includes each trial decision, input hashes, final functional
equivalence and timing reports. These are fixed-wire estimates; rerouting,
fresh extraction, replay, electrical checks, DRC and LVS remain mandatory.

## Iterating data-path sizing

`receiver_path_sizing.py` starts with an independently rechecked trial candidate.
It selects register-to-register data-path cells from the retained timing report,
excluding both clock trees. Replacement masters must be in the same cell family
and have exactly identical retained Boolean models. Each round considers at most
200 replacements under the existing setup, total-negative-slack and 0.1 ns hold
guards. A fresh OpenROAD process reloads the candidate and original extracted
wires between rounds; timing must reproduce within 1 ps. Final acceptance also
requires independent sizing equivalence against the original seed.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_path_sizing.py \
    --seed build/receiver-timing-tradeoffs \
    --routed build/receiver-physical/PCSReceiver-RUN \
    --constraints build/receiver-extracted-sizing/input.sdc \
    --output build/receiver-path-sizing --rounds 4
ctest --test-dir build/behavior -R 'ihp_receiver.path_sizing_guards' --output-on-failure
```

The output receipt identifies the final round's candidate and keeps physical
and timing qualification false. This flow evaluates cell models on retained
wires. It does not repair overlapping resized cells or prove routed timing.

## Routing a limited buffer-removal candidate

`receiver_local_route.py` consumes the completed data-path sizing receipt.
It considers at most 200 explicit `place`/`wire` buffers on reported data paths.
The OpenDB selector accepts a buffer only when its input has a single driver
and no other load, its output has one or two sinks, and every sink lies within
60 µm Manhattan distance of the driver. Clock nets, external ports, register
drivers, fixed/protected objects and overlapping edits are excluded. This
distance is a candidate bound; routed electrical checks still determine acceptance.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_local_route.py \
    --seed build/receiver-path-sizing \
    --constraints build/receiver-extracted-sizing/input.sdc \
    --output build/receiver-local-route
ctest --test-dir build/behavior -R 'ihp_receiver.local_buffer_guards' --output-on-failure
```

The flow checks buffer contraction and filler deferral independently, legalizes
all resized cells, and rebuilds the signal routing. Detailed routing runs in a
fresh process without STA caches. Fillers return before new RC extraction and
slow/fast timing checks. The original SPEF is never loaded into the changed
topology. Functional endpoints must survive routing and filler restoration.
Detailed-router violations fail the flow; negative setup/hold or electrical
violations produce `routed_timing_fail`. Even `routed_timing_candidate_pass`
still requires replay, full KLayout DRC/LVS and chip-level integration.

An interrupted local-ECO run can continue from its completed global checkpoint
only after all four endpoint proofs have passed. The continuation checks all
recorded source hashes, binds the adopted ODB bytes, reloads the database,
verifies the signal netlist and placement, and rebuilds the global estimates.
Setup, hold, slew, capacitance and fanout must pass before detailed routing.
The saved checkpoint remains immutable; each stage writes a distinct database.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/resume_receiver_route.py \
    --origin build/receiver-local-route \
    --constraints build/receiver-extracted-sizing/input.sdc \
    --output build/receiver-local-route-resumed
ctest --test-dir build/behavior \
    -R 'ihp_receiver.(local_path_guards|local_buffer_guards|route_resume_guards)' \
    --output-on-failure
```

The global timing result is a preflight estimate. Final acceptance still reads
the newly extracted routed SPEF and the original electrical limits. The
continuation does not inherit a timing or physical pass from its checkpoint.

## Independent gate replay after routing

The original receiver gate-model build exceeded its 1800-second limit before
producing an executable. `receiver_gate_replay.py` separates that work from
routing, uses one compiler worker, and allows 7200 seconds for the build. Each
complete normal, fault, and clock-enable replay retains the existing 900-second
budget. Every trace must match the independently verified native C++ oracle
byte for byte; no vectors or held clocks are removed.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_gate_replay.py \
    --route build/clock-integration/extracted-repair/receiver-local-route-v2-resumed \
    --mapped-work build/clock-integration/extracted-repair/receiver-physical-2400/PCSReceiver-oi7jjhnp \
    --output build/clock-integration/extracted-repair/receiver-local-route-v2-replay
```

The job waits for the shared EDA memory slot, then requires a completed route,
passing fresh extracted timing/electrical checks, an endpoint proof, and unchanged
route/oracle artifacts. Add `--resume` to reuse a completed, hash-verified
executable after interruption; all three replays still run again. A failed build
is retried. Changed inputs or an altered executable reject resumption.

Its receipt is functional evidence for this routed receiver only. Full KLayout
DRC/LVS, fill coupling, corner coverage and complete chip integration remain
separate requirements. The physical implementation driver does not yet import
this independent replay receipt.

## Repair after a failed extracted route

`receiver_routed_seed.py` requires a completed receiver route and endpoint proof,
checks all recorded inputs/artifacts, and reloads the actual SPEF in a fresh
process. Both slacks must agree within 1 ps. It hardlinks immutable artifacts;
the sizing flow writes new files and must never overwrite those links.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_routed_seed.py \
    --route build/clock-integration/extracted-repair/receiver-local-route-v2-resumed \
    --cells build/clock-integration/extracted-repair/receiver-deferred-fill-v5-qualification/PCSReceiver-vhbw4zu2/cells.v \
    --constraints build/clock-integration/extracted-repair/receiver-extracted-sizing-v3/input.sdc \
    --output build/receiver-fresh-seed
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_path_sizing.py \
    --seed build/receiver-fresh-seed --routed build/receiver-fresh-seed \
    --constraints build/clock-integration/extracted-repair/receiver-extracted-sizing-v3/input.sdc \
    --output build/receiver-fresh-sizing --rounds 2
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_electrical_repair.py \
    --seed build/receiver-fresh-sizing --routed build/receiver-fresh-seed \
    --constraints build/clock-integration/extracted-repair/receiver-extracted-sizing-v3/input.sdc \
    --output build/receiver-electrical-repair
```

Electrical repair targets 0.30 ns slew and 0.07 pF load while acceptance stays
at 0.50 ns and 0.10 pF. The checker separately proves filler removal, inserted
non-inverting buffers, same-function drive changes and unchanged endpoints after
legalization. New global routes replace the invalid old SPEF after topology
changes. A completed `global_repair_candidate` can be passed to
`resume_receiver_route.py --origin build/receiver-electrical-repair` with the
same constraints and a fresh output directory. That runner first repeats
global endpoint/electrical/timing checks, then runs detailed routing and fresh
extraction. Only a passing extracted candidate may enter gate replay.

For the current receiver, nominal global timing has been optimistic. The
maintained margin flow adds a 1.5x wire-capacitance optimization screen and
same-function sizing before committing to another detailed route:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_margin_route.py \
    --origin build/receiver-electrical-repair \
    --cells build/receiver-fresh-seed/cells.v \
    --constraints build/clock-integration/extracted-repair/receiver-extracted-sizing-v3/input.sdc \
    --output build/receiver-margin-route
```

After legalization, nominal and enlarged-C setup/hold estimates must each meet
the optimization screen: by default +0.1 ns for both. The flow also trials same-function drive variants of the two
clock-buffer levels nearest reported critical registers, capped at 200 trials.
It accepts only setup/TNS improvements that preserve at least 0.1 ns hold,
proves the clock replacements, then relegalizes and rebuilds the routing
estimates. It does not remove the short data buffers: that experiment worsened
the current candidate's timing. Electrical limits remain unchanged. Failure retains a
`global_margin_fail` diagnostic without launching detailed routing. Success
continues through detailed routing (up to five hours), filler restoration and
fresh nominal OpenRCX extraction. The margin is not a PVT corner; exact replay,
full layout verification and chip integration remain required afterward.

`--minimum-optimization-setup 0` explicitly permits a nonnegative setup estimate
in both models when deciding to measure the actual detailed route. It does not
alter the 0.1 ns estimated hold screen, final extracted acceptance, electrical
limits or clock constraints. Negative/nonfinite targets are rejected and the
chosen value is recorded in the receipt. The current v5 run uses this option
after the v4 candidate reached +0.014392043 ns in the enlarged-C model.

The v5 detailed route still fails actual setup and capacitance. For its next
candidate, `receiver_final_repair.py` applies electrical repair again after
drive sizing and clock trials. This accounts for the changed pin loads before
accepting a new global checkpoint. It uses a 1.75x wire-C optimization probe,
then requires at least 0.1 ns setup/hold and no electrical violations in both
enlarged-C and nominal estimates. The initial input must be a completed,
independently reproduced `receiver_path_sizing.py` result:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_final_repair.py \
    --seed build/clock-integration/extracted-repair/receiver-fresh-path-sizing-v2 \
    --routed build/clock-integration/extracted-repair/receiver-fresh-route-seed-v2 \
    --constraints build/clock-integration/extracted-repair/receiver-extracted-sizing-v3/input.sdc \
    --slew-margin 0 --cap-margin 0 \
    --output build/receiver-final-repair
ctest --test-dir build/behavior -R '^analog\.1g\.ihp_receiver\.final_repair_(guards|native)$' --output-on-failure
```

Only a successful `global_repair_candidate` receipt is ready for
`resume_receiver_route.py --origin build/receiver-final-repair` with the same
constraints and a fresh output directory. This repeats checkpoint checks,
detailed routing and nominal RC extraction; it does not inherit a timing pass
from the optimization probe. Failed candidates retain diagnostic reports.
Extra repair margins are explicit percentages from 0 to 40 and default to zero;
the enlarged-C probe already adds interconnect pessimism. The first full trial
combined that probe with 40% slew and 30% capacitance margins, added excessive
buffering and failed timing. Removing those extra targets does not relax the
original 0.50 ns/0.10 pF acceptance limits or either final timing screen.

Use `--rebuffer` when resizing alone cannot close the critical path. This
enables the bounded short-data-buffer selector, followed by setup and hold
repair on rebuilt routes. Every removed or inserted buffer must have a proven
identity model and a valid directed connection; every surviving logic function
and endpoint must agree. The selector excludes clock nets and delay cells.
Its estimates remain candidates for new detailed routing and extraction.

`--data-sizing` additionally checks equivalent data-path drive
variants after each repair round, including smaller cells that reduce loading
on the preceding gate. It reuses the function-matching and clock-exclusion
rules from `receiver_path_sizing.py`. Accepted trials improve setup/TNS and
retain hold margin, then undergo fresh routing and electrical repair.
`--trial-limit` bounds both data and clock passes, defaults to 12, and accepts
1 to 200. Each change and rollback rebuilds global-route RC before the next
timing decision. This is necessary because the pinned tool retains stale
reduced loads after `replace_cell`; invalidating timing alone does not correct
them. Reloading exported estimated SPEF also changes the baseline and is not
used as a shortcut. Imported actual extracted RC follows its separate,
independently reloaded sizing flow. The native redundant-chain test reproduces
the stale-load difference, checks the trial against a fresh RC estimate, forces
rejection through its hold guard, and verifies restored-cell timing.
Use `--rebuffer --data-sizing --trial-limit 12 --cap-margin 5` for the v6
experiment. Final acceptance thresholds remain unchanged.

`receiver_pin_stage.py` provides a separate equivalent-input repair stage for
a completed global checkpoint. It uses OpenROAD's `swap` move only; the
preceding topology/drive proofs remain bound to their original files.
`pin_swap_equivalence.py` requires unchanged instances, masters, outputs,
aliases and input-net multiplicities. For each changed gate it checks every
input combination of the actual retained scalar combinational model, including
all outputs. Sequential rewiring and unsupported expressions are rejected.
Native pin repair is tested separately before use on the full receiver.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_pin_stage.py \
    --origin build/receiver-final-repair \
    --constraints build/clock-integration/extracted-repair/receiver-extracted-sizing-v3/input.sdc \
    --models build/clock-integration/extracted-repair/receiver-fresh-route-seed-v2/cells.v \
    --output build/receiver-pin-repair
```

A completed producer that failed only its timing screen may supply diagnostic
geometry, provided all preceding connectivity proofs and nominal electrical
checks pass and every source/artifact hash still agrees. Native failures and
incomplete proofs are rejected. Pin repair rebuilds the routes and checks both
the nominal and 1.75x-C timing/electrical screens. Only a new successful
`global_repair_candidate` receipt can continue through detailed routing,
extraction, exact replay and layout; an input timing failure is never inherited
as a pass. This stage does not insert buffers to conceal loading failures.

After every legalization, the flow rebuilds global routes and resets stale
estimator bookkeeping through placement estimation before restoring global-RC
estimates. This avoids the pinned tool's `EST-0104` invalid-net-state failure
after explicit clock-cell trials. Final reports must come from a complete,
marked route-check section without missing-pin or fallback-model warnings.
The [pinned estimator implementation](https://github.com/The-OpenROAD-Project/OpenROAD/blob/08f67ee5ec/src/est/src/EstimateParasitics.cpp)
defines these state transitions. The `rebuffer_equivalence` and `rebuffer_native`
CTest cases cover topology validation and actual native buffer replacement.

To queue all subsequent receiver stages while repair is still running:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_continue.py \
    --repair build/receiver-final-repair \
    --mapped-work build/clock-integration/extracted-repair/receiver-physical-2400/PCSReceiver-oi7jjhnp \
    --constraints build/clock-integration/extracted-repair/receiver-extracted-sizing-v3/input.sdc \
    --output build/receiver-continuation
```

The output directory must be fresh and the producer receipt must already exist.
The continuation waits up to two hours for a live repair producer and then up
to 24 hours for at least 3 GiB free. It holds no EDA slot during these waits.
It revalidates the producer after waiting, and runs `route/`, `replay/` and
`layout/` in order. Each stage requires its completed pass receipt, not just a
successful process exit. Failed repair, extracted timing or replay stops the
sequence. `receiver_standalone_checks_pass` still excludes post-fill timing and
complete-chip integration/electrical signoff.

After routed timing and all three exact gate replays pass, continue to physical
layout verification without repeating the gate build:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_layout.py \
    --route build/clock-integration/extracted-repair/receiver-margin-route-v5 \
    --replay build/clock-integration/extracted-repair/receiver-margin-route-v5-replay \
    --mapped-work build/clock-integration/extracted-repair/receiver-physical-2400/PCSReceiver-oi7jjhnp \
    --output build/receiver-layout --wait
```

Use a fresh output directory. `--wait` allows live producers to finish before
taking the shared EDA slot; failed or abandoned producers stop the continuation.
The handoff verifies all route/replay provenance, the compiled executable,
normal/fault/clock-enable traces and the producer's bound DEF/CDL. It rejects a
stale or unrelated replay. Streaming uses the real IHP GDS cells; fill, full DRC
and persisted transistor-level LVS all run on the same hashed layout. DRC uses
one worker and includes density, maximum and antenna checks. The generated GDS
is `module_filled.gds`; only a completed `receiver_geometry_pass` receipt
establishes this standalone layout check. Post-fill coupling, electrical
characterization and complete-chip integration remain separate requirements.

The lightweight handoff regressions run with:

```sh
ctest --test-dir build/behavior -R '^analog\.1g\.ihp_receiver\.layout_handoff_guards$' --output-on-failure
```

Single-strength output buffering is a separate diagnostic stage. It selects up
to 128 unique xor/xnor, o21ai and a221oi outputs per round from the critical-path
report, requiring at least 10 fF load and 250 ps slew. The 500 ps electrical
limit remains unchanged: selection targets delay before that limit is exceeded.
The selector supports the library's `X` and `Y` output names. Clock nets,
protected nets/cells and unsupported functions are rejected by the native helper.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_output_stage.py \
    --origin build/clock-integration/receiver-onehot-revision/extracted-pin-hold-repair \
    --constraints build/clock-integration/receiver-onehot-revision/branched-buffer-repair/candidate.sdc \
    --models build/clock-integration/receiver-onehot-revision/powered-branches-routed-seed/cells.v \
    --output build/receiver-output-repair
ctest --test-dir build/behavior --output-on-failure \
    -R '^analog\.1g\.ihp_receiver\.(output_selection|output_native)$'
```

Each selected output gets a real `sg13g2_buf_2` near the source. Legalization and
full global-route RC rebuilding follow each pass; subsequent selections use the
new paths and skip previously selected outputs. `--rounds` defaults to three
and is bounded to four. Electrical repair and hold repair run after the bounded
setup optimization; intermediate rounds cannot qualify the candidate.
Admission verifies the preceding completed native stage, endpoint proofs,
electrical pass, immutable artifacts and tool/library hashes. The initial
independent reload must reproduce both enlarged-C slacks within 1 ps.
The stage proves inserted delays and any drive resizing before screening both
nominal and 1.75x wire-C estimates. Only `global_repair_candidate` permits the
checked continuation above to start detailed routing. A saved failing diagnostic
does not qualify GDSII. Each round has a reserved buffer namespace; an existing
namespace rejects accidental rerunning of the stage into the same candidate.
The native regression starts with two loaded loops and only one selected gate,
requires the other gate to be selected on the next round, and checks exact
endpoints and both final timing/electrical screens.

An unbuffered global diagnostic can also supply this repair using
`--origin <unbuffer-run> --margin-measurement <independent-measurement>`.
Create the measurement with the reusable command below; it exits successfully
for a completed measurement even when `optimization_screen_pass` is false.
That flag is a conservative optimization screen, not extracted qualification.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_global_measurement.py \
    --origin build/receiver-unbuffer \
    --output build/receiver-global-measurement
```

Use that producer's `candidate.sdc` as `--constraints`. The measurement must
bind the producer receipt, ODB, SDC and netlist, preserve every endpoint and
cell, and reproduce passing nominal timing/electrical checks before and after
the 1.75x wire-C measurement. Enlarged-C failures are repair inputs, not passes.
The runner records this distinct origin and proves that no fillers changed
during the reload. All subsequent routing, extraction and acceptance checks
are the same as for a preceding global repair.

`receiver_branch_stage.py` addresses serial buffers whose source also feeds
sibling branches. It accepts the same `--origin`, `--constraints`, `--models`
and fresh `--output` arguments. A source must be a buf8 or buf16, and merging
the selected branch must leave at most eight input loads. The default neighbor
span is 120 um, explicitly bounded to 200 um. It excludes clock/protected nets,
port nets, fixed/protected cells and overlapping selections. Up to 32 explicitly
named `place`/`wire` buffers can be removed per pass; `--rounds` defaults to six
and is bounded to eight. Every pass uses fresh critical paths and rebuilt RC.
Final electrical/hold repair, exact endpoint checks and both timing screens
remain mandatory. The common `receiver_checkpoint_stage.py` binds source and
artifact hashes and independently checks the initial timing reload.

```sh
ctest --test-dir build/behavior --output-on-failure \
    -R '^analog\.1g\.ihp_receiver\.branch_native$'
```

The native regression uses a branched register-feedback chain, verifies setup
improvement and preserved sibling endpoints, and rejects protected cells,
clock nets and excessive merged fanout before exercising the actual repair.

`receiver_skew_stage.py` uses the same checkpoint arguments to evaluate bounded
same-function clock-buffer sizing near critical registers. `--trials` defaults
to 16 and is bounded to 200. It reuses the clock-path selector and guarded
timing-tradeoff engine: every trial and rejected rollback refreshes RC, accepted
trials preserve at least 0.1 ns hold and cannot worsen either setup WNS or TNS.
Sizing must improve at least one of those metrics. Final legalization and
rerouting are checked independently of the trial decisions. The period,
uncertainty, library corners and final acceptance limits remain unchanged.
Run `analog.1g.ihp_receiver.skew_native` through CTest to exercise the complete
native stage on two register loops with unequal clock loading, verify a retained
drive change and unchanged endpoints, and check both timing/electrical screens.

`receiver_data_stage.py` evaluates bounded same-function data-path drive changes
from a completed global repair. Its arguments match the output/skew stages;
`--trials` defaults to 48 and is bounded to 200. Data paths are selected between
register Q and data-arrival rows, excluding the launch/capture clock trees.
After the trials, it legalizes, rebuilds global routes and checks both nominal
and 1.75x wire-C estimates. It saves `retained.odb`/`retained.sdc` and replaces
them only when the fresh physical estimates satisfy the selection rule:

- Both electrical screens pass and both hold estimates retain 0.1 ns.
- Nominal setup retains 0.1 ns and does not regress by more than 1 ps.
- Enlarged-C setup/TNS do not regress by more than 1 ps, and one improves by
  more than 1 ps.

If selection fails, the stage restores original cell masters, orientations,
positions and placement states, then reroutes and requires the original
enlarged-C setup/hold values within 1 ps. This stage performs no topology edits.
The native fixture exercises both improvement and rejection. A separate
process reloads the retained ODB and checks its actual cells and timing.
The common stage checker still proves exact endpoints and rejects a failing
final optimization screen; retaining a diagnostic improvement is not physical
qualification. This checkpoint policy currently applies to this data-sizing
stage, not every older repair loop.

```sh
ctest --test-dir build/behavior --output-on-failure \
    -R '^analog\.1g\.ihp_receiver\.(checkpoint_choice|data_native)$'
```
