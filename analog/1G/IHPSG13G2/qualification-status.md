# Working circuit and timing priorities

The two-port repeater is unfinished. Electrical correctness and timing closure
take priority over area. Larger cells, buffering and floorplans are acceptable
when they improve timing without changing the required data rate or electrical
limits. Area reductions are accepted only after those checks pass. Preset trim
values characterize a circuit and do not implement
calibration. Generic C++RTL/Verilog-A agreement tests the exported equations;
IHP PSP tests additionally exercise intrinsic transistor charge and body effects.

## November 3 measured receiver paths and clock load

The allocator-backed early route completed in 9,740 seconds, followed by
93 seconds of extraction. Peak router RSS was 3,808,412 KiB (about 3.63 GiB).
It still has 17,335 router violations after the two diagnostic iterations.
Measured setup is -0.928154469 ns; hold is +0.120607637 ns; slew violations
reach 0.68 ns against the 0.50 ns limit. Full routing/replay/layout continuation
correctly stopped at the failed setup screen. This is diagnostic geometry,
not a completed receiver or chip.

Of the fifty reported worst paths, 26 end at `decoder.selected_bytes`, 14
at `decoder.costs`, four at `decoder.data_reg`, and three each at selectors
and reachability. Byte selection previously encoded the one-hot winner and
then decoded it again in a four-way mux. The first November 3 revision applied
the existing one-hot decision directly to candidate bytes and reachability, as
it already did for metric selection. Tie priority, latency, symbol enable and
draining behavior are unchanged. All six native/reference CTests pass in
6.14 seconds. Trellis native/RTL/IHP-mapped trajectories match exactly, including
25,863 full-path oracle steps and the noisy-channel cases. Its slow-corner
cell-only setup estimate is +2.77 ns at 8 ns, versus the previous +2.91 ns;
that estimate does not demonstrate an improvement. The full receiver passes
native/RTL/IHP-mapped normal, fault and symbol-enable comparisons. Its slow-corner
cell-only setup estimate improves from +1.51 to +1.76 ns at the unchanged
7.352941176 ns period. The new mapping contains 71,198 cells versus 71,441.
All seven validation stages complete successfully in
`build/clock-integration/receiver-onehot-revision/`. Fresh 2400 um placement
and global routing completed under `physical-global/`, but setup failed at
-0.526136458 ns while hold was +0.122228563 ns. Electrical checks passed.
The retained checkpoint then underwent bounded data-buffer removal in
`critical-buffer-repair/`. All endpoint comparisons pass; fresh global estimates
improve to +0.078034468 ns setup and +0.122228563 ns hold, with electrical checks
still passing. This is below the 0.1 ns setup admission margin and is not
extracted timing closure. The subsequent `critical-hold-repair/` diagnostic
completed with no accepted delay-cell changes. No new routed qualification
or complete-chip GDSII follows from these runs. The previous source and mapping
evidence are retained.

`physical_module.py --global-route-only` records failed estimates as well as
passing ones and stops before detailed routing. Missing-route warnings reject
the checkpoint; no qualification flag becomes true. All 21 physical-driver
guard cases pass through CTest in 0.56 seconds. Old forwarding traces were
losslessly compressed with SHA256 verification, recovering 303.4 MiB; restoration
commands are retained in `receiver-onehot-revision/retired-forwarding-traces.json`.

The standard receiver route flow also now rebuilds routes after topology
repairs. Starting incremental tracking after those edits could miss newly
added pins. Receiver setup/hold and final-network updates now legalize, rebuild
global routes, clear invalid RC entries, and refresh global parasitics. Other
modules retain their existing update policy. The real IHP loaded-gate/hold
regression exercises this receiver path and its written routing guide;
receiver, generic and clock repair CTests all pass in 2.50 seconds. This
repairs route-estimation consistency; it does not itself prove timing closure.

`receiver_routed_seed.py --probe-origin` can explicitly adopt early extracted
wires for further diagnostics. It binds the originating candidate and cell
models, rechecks timing in a fresh process, and keeps all qualification flags
false. The actual IHP routing/extraction/handoff regression passes, including
this new diagnostic path (374.46 seconds including shared-slot waits).
Three sizing/route/seed guard CTests pass in 0.59 seconds.
The full-receiver independent reload also completes and reproduces both
extracted slacks exactly under `trellis-compressor-revision/measured-route-seed/`.

The extra `sg13g2_buf_16` output stage fails SS delay at 427.627 ps against
400 ps, despite 53.552 ps rise and 49.277 ps fall. It is not adopted. The
original extracted gate instead passes TT/SS/FF pulse, slew and delay checks
at an explicit 55 fF final load: rise/fall maxima are 68.940/50.394,
98.893/70.426 and 53.807/38.886 ps respectively. The unchanged limit is 100 ps;
SS maximum edge delay is 300.074 ps against 400 ps. This is a conditional
load screen, not connected-clock qualification.

The early-route receiver root measures 47.200 fF with maximum characterized
pin loading and doubled coupling. Only 7.800 fF remains for external wiring
within the passing 55 fF budget. The new one-hot implementation will need
its own extracted root-load check. Actual inter-macro RC, physical oscillator
drive, reset distribution, post-fill effects and full-chip verification remain
open. See [clock-input-timing.md](clock-input-timing.md).

## November 2 receiver timing and final electrical repair

The revised receiver's `early-route-200` diagnostic was killed by the kernel
OOM handler after 10,392 seconds. Initial pass 0 had 25,963 violations; repair
pass 1 reached 60%. The kernel records approximately 3.79 GiB router RSS and
a concurrent C++ compiler using approximately 1.13 GiB. No routed checkpoint,
extraction or timing result was produced. The continuation correctly refused
the failed probe. `end_iter = 1` includes initial pass 0 and repair pass 1.

`early-route-200-jemalloc` retried the unchanged candidate with an explicitly
selected, locally installed allocator and per-stage memory measurements. Its
library/configuration are recorded with the run; physical acceptance is unchanged.
The native allocator regression exercises early routing, extraction, full routing,
endpoint equivalence and final timing. It and three admission guards pass in
12.21 seconds. Two new allocator/clock-buffer guard tests pass in 0.18 seconds.
The earlier five handoff checks also pass, including the real IHP native fixture
(8,975.80 seconds including waiting for the EDA slot). None qualifies the full
receiver. See [receiver-routing-memory.md](receiver-routing-memory.md).

The checked continuation subsequently rejected the probe's measured setup failure.
Full routing retains a bounded
six-hour stage limit and an eight-hour continuation budget. It requires fresh
extracted timing/electrical checks before replay and physical layout checks.

A separate clock-interface check finds that the retained extracted gate's
200 fF pulse-only pass does not meet the receiver's 100 ps input-transition
constraint. Measured SS rise/fall are 141.066/106.913 ps; TT rise is 101.784 ps.
`processing_clock_slew.py` now binds that SDC, actual Liberty slew thresholds,
and all original PVT traces/decks. The failed recheck is retained under
`clock-gate-input-slew-200/`. The 100 fF experiment also fails: SS rise is
112.491 ps despite passing the pulse checks. A PDK buffer candidate is now
queued at a 100 fF final load under `clock-gate-buffer16-100/`; slew remains limited to 100 ps and
edge delay to 400 ps. No connected-clock pass is claimed.

`receiver_clock_load.py` applies the existing wire/pin accounting to `clk` and
reports the remaining inter-macro wire budget. The preceding routed receiver
root totals 33.887 fF with maximum characterized pins and doubled coupling,
leaving 66.113 fF within a 100 fF budget. This historical result is not reused
for the revised receiver: its measurement must use the new actual SPEF.
Slew and load-accounting CTests pass in 14.38 seconds; receiver clock-load,
route-resume and continuation guards pass in 0.76 seconds. These checks expose
an integration constraint and do not establish connected-clock or ASIC closure.
See [clock-input-timing.md](clock-input-timing.md).

`receiver-margin-route-v5/` completes detailed routing in 13341 seconds with
zero router violations. Fresh OpenRCX takes 95 seconds. Actual setup improves
from -0.591841280 ns to -0.266305208 ns, but still fails; hold is +0.123447925 ns.
Capacitance violations remain, reaching approximately 0.12 pF against the
unchanged 0.10 pF limit. Replay and layout continuation correctly reject this
candidate before building or streaming. This is not a qualified receiver.

`receiver-fresh-route-seed-v2/` reproduces the new extracted timing in an
independent process. `receiver-fresh-path-sizing-v2/` continues same-function
data-path trials against those actual wires. Four rounds estimate -0.152044609,
-0.124997795, -0.091624491 and -0.082707174 ns setup, with hold unchanged. The
final independent reload agrees exactly, and all 61 cell changes pass functional
equivalence. These are fixed-wire diagnostics; physical closure requires
legalization and new routes.

`receiver_final_repair.py` and `receiver_final_repair.tcl` address loading added
by subsequent drive-strength changes. They first repair measured electrical
violations, then alternate sizing, legalization/routing and electrical repair
using a 1.75x wire-capacitance optimization probe. They repeat electrical repair
after the final clock-drive trials. Both nominal and enlarged-C estimates must
retain at least 0.1 ns setup/hold and pass electrical checks before the candidate
can continue. All device/function and endpoint checks remain required. The
five screen regression cases and a native routed register-loop regression pass.
The native test checks both optimization rounds, real electrical repair,
unchanged endpoints and the final timing/electrical screens. Its CTest elapsed
time includes waiting for the shared EDA slot. The full candidate is running
under `receiver-final-repair-v1/`. The factor is not a characterized process corner and
does not change final nominal extraction or the physical acceptance limits.

That first full run finishes its native stages in 998 seconds but fails the
optimization screen: nominal setup is +0.156283885 ns, enlarged-C setup is
-0.806275964 ns. The extra 40% slew / 30% capacitance repair margins introduce
substantial buffering and do not close timing. The failed run's original
driver/Tcl sources are saved with verified hashes in `source_snapshot/`;
large generated diagnostics have verified gzip archives and restoration records.

`receiver-final-repair-v2/` retries from the same independently checked sizing
seed with explicit `--slew-margin 0 --cap-margin 0`. This removes the extra
repair targets, not the original 0.50 ns/0.10 pF limits or the 1.75x wire-C
probe. Both timing screens still require 0.1 ns setup/hold. Seven guard cases
and the native register-loop regression pass with this configuration; the
native test takes 3.32 seconds without slot contention. The full run fails:
nominal setup/hold are +0.487450093 / +0.133639097 ns, but enlarged-C setup is
-0.494750023 ns and six capacitance violations remain. The log also reports a
missing route to `place61510/X`; these estimates do not establish complete
routing. Its original driver/Tcl sources are retained in `source_snapshot/`.

`receiver_continue.py` rejects that failed producer under
`receiver-final-repair-v2-continuation/`.
It waits outside the EDA memory slot for a successful repair receipt, verifies
all bound inputs/artifacts and the final screen, then waits for at least 3 GiB
free before launching routing, exact replay and layout verification in sequence.
Every child must publish its expected completed pass receipt: a zero process
exit with failed extracted timing cannot launch replay. Five continuation guard
cases pass. This automation establishes only the standalone receiver checks;
whole-chip qualification remains false.

The v3 repair rebuilds global routing after every legalization. Previously,
incremental tracking started after buffer repair, so newly changed topology
could be omitted from route updates. Missing-pin parasitic warnings now reject
the candidate explicitly. Eight guard cases and the native OpenROAD regression
pass (3.40 seconds total CTest time). The full v3 run reaches -0.481026798 ns
enlarged-C setup and +0.154201880 ns hold after round two, then fails with
`EST-0104` during electrical repair after clock trials. It produces no qualified
checkpoint. Its original sources are retained in `source_snapshot/`.
Cleanup removes 149 obsolete run directories and associated reports, recovering
3.0 GiB while retaining current proof inputs and reference waveforms. The old
v1 final-repair diagnostics described above were included in that cleanup.

The pinned OpenROAD global-RC refresh does not clear the estimator's invalid-net
set after explicit cell changes. The flow now rebuilds global routes, performs
placement estimation to clear that bookkeeping, then replaces it with global-RC
estimates. Final timing/electrical reports are enclosed by explicit route-check
markers. Missing-pin, uncovered-guide, or wire-load-fallback warnings in that
final section reject the candidate; earlier optimizer warnings cannot qualify
or invalidate the independently rebuilt final routes by themselves.

The `--rebuffer` mode additionally tries nonoverlapping short data buffers on
critical paths, with at most 200 named candidates and a 60 um neighbor span.
The existing selector protects clock nets, register loading, ports, fixed cells,
and delay cells. Setup repair can resize/rebuffer the result, and hold repair
must restore margin before acceptance. A new equivalence proof permits only
identity-buffer removal/insertion and identical-function drive changes; it
checks unique drivers and rejects reversed buffers, floating inputs, cycles,
changed logic, and changed surviving endpoints. Nine proof cases pass.

Both native fixtures pass: the original electrical-repair loop and a redundant
buffer chain that must undergo an actual topology change. Final-screen,
continuation, and local-buffer selection regressions also pass. The full
`receiver-final-repair-v4/` run uses `--rebuffer --slew-margin 0 --cap-margin 5`;
5% is an optimization target, with the original final electrical and timing
limits unchanged. Full receiver and whole-chip qualification remain open.

The full v4 native run completes in 1253 seconds without `EST-0104`. Its final
route-check section has no missing-pin warnings. Nominal setup/hold are
+0.529169619 / +0.133639216 ns; enlarged-C setup/hold are -0.234948963 /
+0.153782323 ns. Both electrical reports are clear. Setup still fails the
optimization screen, so detailed routing is not justified by this result.
The new connectivity checker initially rejects the deliberately open output of
clock-load cell `clkload0`. The corrected checker permits unchanged unconnected
outputs while retaining exact surviving-pin/connectivity checks. Ten guard
cases pass. Independent rechecking of the saved netlists passes with 37 removed
buffers, 647 inserted buffers, and 710 equivalent cell replacements; that receipt
is `receiver-final-repair-v4-rebuffer-recheck.json`. The original failed producer
receipt remains unchanged, with its original sources saved separately.

The native redundant-chain regression now includes real clock-buffer trials,
so it exercises the stale-estimator transition as well as buffer topology
changes. An optional `--data-sizing` step tries up to 200 proven equivalent
drive variants in both directions after each electrical-repair round. Each
accepted trial must improve setup/TNS without losing 0.1 ns hold margin;
routes and electrical loads are rebuilt and checked again afterward. Six
relevant CTests pass in 6.96 seconds. The new full v5 run enables this option
alongside v4's rebuffering and 5% capacitance optimization margin.

The v5 native run completes in 1488 seconds but fails enlarged-C setup at
-0.390538275 ns; nominal setup is +0.530198097 ns. Connectivity passes and
continuation correctly refuses to launch detailed routing. A saved-v4
diagnostic identifies stale reduced RC during manual drive trials: weakening
`place53935` from buffer-4 to buffer-1 appears to improve setup from
-0.234948963 to -0.211922929 ns. Rebuilding global RC instead gives
-0.364480019 ns; fresh routing agrees at -0.362275541 ns. Timing invalidation
alone retains the false improvement. Exporting/reloading estimated SPEF changes
the baseline to -0.042584606 ns and is rejected as an equivalent shortcut.
These diagnostic results are under `receiver-global-trial-refresh-v1/` and
`receiver-global-trial-spef-v1/`; neither qualifies a physical design.

Manual global-route trials now rebuild RC after every candidate and rollback.
The separate actual-SPEF sizing path keeps its independent final reload.
A native regression reproduces stale loading, compares the corrected journal
against fresh RC, and verifies rollback. Five targeted CTests pass in 8.48
seconds. `receiver-final-repair-v6/` uses 12 trials per data/clock pass to bound
the cost of full RC refresh, with rebuffering and the same 5% capacitance target.
Its full-receiver result is pending. No timing or electrical limit is relaxed.

The corrected v6 first round finishes at -0.226325631 ns enlarged-C setup;
all twelve data-drive trials are rejected using fresh RC. The second round is
still running. `receiver_pin_stage.py` prepares a separate equivalent-input
repair for the remaining trellis-cost path. Its exact truth-table checker
passes ten guard cases; four stage guards reject incomplete provenance and
electrical failures. Evaluating the retained IHP model library finds symmetric
input pairs in twenty gate families. The native routed pin-repair fixture is
queued behind v6; no full-receiver improvement from pin repair is claimed yet.
The pin stage may continue only from completed native work with every preceding
logical proof and the same inputs/libraries. It rebuilds RC and requires the
unchanged nominal/enlarged-C screens before downstream physical qualification.

The v6 native run completes in 2665 seconds. Final enlarged-C setup/hold are
-0.242954999 / +0.154201984 ns; nominal setup/hold are +0.529223800 /
+0.133864701 ns. Both electrical reports and all four connectivity proofs pass.
All 24 data trials are rejected; one of 12 clock trials is retained. The final
setup screen fails and continuation correctly stops before detailed routing.
The native pin-repair and refreshed-RC/rollback regressions subsequently pass;
the first test's 1948-second CTest duration includes waiting for the EDA slot.
`receiver-pin-repair-v1/` now applies the separate equivalent-input stage to
this completed checkpoint. Its full-receiver result is pending.

The pin stage completes in 197 seconds and proves all 421 changed cells exactly
equivalent. Both electrical screens pass, but enlarged-C setup/hold are
-0.244331673 / +0.154201984 ns; nominal setup is +0.529431641 ns. Continuation
again stops. The critical reports include a violating path from `_134645_`
back to the same register, so clock skew cannot repair that recurrence.

The next revision changes `PCSTrellisDecoder::branch_better` itself. Each
carry-save result has a shifted carry with a zero low bit. Injecting one into
that bit after each of two reductions supplies the two's-complement correction
for `a + b + ~c + ~d + 2`; it removes the third compressor without changing
latency, tie ordering or the 21-bit circular comparison. The independent
full-path oracle adds 8192 alternating extreme/tie cases. Native and generated
RTL and mapped IHP gates agree exactly for all 25863 steps. The full receiver
also passes native, generated-RTL and mapped-gate checks, including fault and
clock-enable traces. All six functional CTests pass in 1077 seconds. Both
registered cell-timing tests pass: trellis setup is +2.91 ns at an 8 ns period;
receiver setup is +1.51 ns at 7.352941176 ns, using the slow/hot IHP corner.
These estimates exclude routed wires and hold. The mapped receiver decreases
from 73085 to 71441 cells; XOR/XNOR cells decrease from 6171 to 5235.
Physical timing improvement remains unmeasured.

Original C++/test sources are retained under
`build/clock-integration/trellis-compressor-revision/source_snapshot/`.
Old physical receiver checkpoints are historical diagnostics; this revision
requires a fresh mapped netlist and physical build. With those runs finished,
the older `receiver_margin_sizing.tcl` caller is also updated to explicit fresh
global RC and a bounded clock-trial count. Its previous sources are archived
alongside the C++ snapshot. Extracted-SPEF callers retain independent reloads.

The fresh physical run is
`build/clock-integration/trellis-compressor-revision/physical/PCSReceiver-j2la16et/`.
It uses the new mapped proof, a 2400 um die and one routing/DRC worker, with
the existing 136 MHz, setup/hold and electrical acceptance constraints.
Optional clock NDR is disabled; routed extraction and layout checks remain
mandatory. Placement/routing and subsequent verification are running, not
qualified. Cleanup retires nine older intermediate receiver placement databases
(2.15 GiB), retaining their netlists/reports, the final old sizing checkpoint
and the actual routed v5 baseline. Their hashes and regeneration boundary are
recorded in `trellis-compressor-revision/retired-checkpoint-cleanup.json`.

The revised decoder also passes the connected `QualifiedADCReceivePHY`
regression in 260 seconds. Native C++ and generated RTL agree exactly over
1,876,382 events, 16 frames and 12,784 bytes, including both link roles and
clock/reset interruptions. Its receipt is bound in the revision's `checks.json`.
This extends functional coverage through ADC capture, DSP, clock crossing and
PCS; it does not establish physical clock/reset or complete-chip timing.
The fresh receiver placement finishes with +0.009166001 ns setup and
+0.132829860 ns hold. Preparation with the existing 2.5x wire-capacitance
optimization model still fails setup at -1.966396928 ns, with +0.153269619 ns
hold. Global-route repair is running; these placement estimates are not
extracted timing or a qualified GDSII.

A separate slow-corner, 4 ns-target mapping experiment is queued behind the
shared EDA lock under `trellis-compressor-revision/gate-timing-mapping-serialized/`.
The two preceding 1 GiB-bounded attempts exhausted their address-space limit
before producing a mapped result. The queued attempt uses a 3 GiB bound while
owning the EDA slot; no candidate is accepted without subsequent functional
proof. Five retired pre-revision receiver databases are now verified gzip
archives, recovering 687.6 MiB. Their original and archive hashes and restoration
boundary are in `trellis-compressor-revision/retired-checkpoint-archives.json`.
The active placement, latest routed baseline and analog reference waveforms
are retained.

The first revised physical run is stopped during global load repair with its
placement checkpoint and original sources intact. Preparation previously kept
the 0.3 ns slew target while routing introduced a 0.2 ns target and enlarged
wire capacitance. `route_extract.tcl` now applies the same routing electrical
targets to the receiver during placement preparation, before setup/hold repair
and the fresh global-routing process. The native regression repairs an actual
IHP slew violation, independently reloads RC, checks the saved 0.2 ns constraint,
and proves unchanged functional endpoints. It passes in 0.98 seconds; physical
checkpoint guards also pass. No final electrical or timing limit is relaxed.
The resumed run is
`trellis-compressor-revision/electrical-prepared/physical/PCSReceiver-q8aa9j0o/`.
Its receipt verifies the original placement and records the changed routing
script; physical closure is still pending.

The serialized mapping probe reaches the old Yosys checkpoint writer and fails
because that version supports `write_ilang`, not `write_rtlil`. The corrected
probe is queued under `trellis-compressor-revision/gate-timing-mapping-v2/`.
Neither failed probe supplies a replacement netlist or qualification evidence.

The first electrical-preparation candidate completes in 206.5 seconds but is
rejected before routing. The 0.2 ns optimization target inserts 34,251 buffers,
including long serial chains, and worsens enlarged-C setup to -14.900566101 ns.
That prepared network is not reused. The receiver optimization target is now
0.4 ns; the accepted 0.5 ns slew limit, 136 MHz timing, 2.5x optimization wire C
and independent final extraction are unchanged. The updated real-cell
preparation regression and physical checkpoint guards both pass in 1.54 seconds.
`electrical-040/physical/PCSReceiver-dbadqhh7/` resumes the original placement
with this target. Preparation takes 156.407 seconds and inserts 7,773 buffers.
Its enlarged-C setup/hold are -2.110733986 / +0.148921773 ns. Global-route repair
finishes in 1279.137 seconds, but fails final setup: nominal setup is
-0.468782365 ns, hold is +0.120968238 ns and the electrical report is clean.
The enlarged-C setup estimate is -2.567480564 ns. The failed candidate and its
`finished_global.odb` checkpoint are retained; no detailed route or layout
qualification is claimed.

`receiver_global_unbuffer.py` evaluates short data-buffer removal directly on
that retained global checkpoint. Each of at most four rounds considers 200
critical-path buffer names and uses the existing protected-neighbor/60 um
selector. Routes and RC are rebuilt after every topology change, and each
round must preserve exact surviving endpoints. The flow records timing and
electrical violations without treating a diagnostic result as qualification.
An independent initial reload reproduces nominal setup within 0.5 ps. The
full run is `trellis-compressor-revision/global-unbuffer-v1/`.

The 60 um trial completes with 95 buffers removed and exact endpoint checks
passing. Nominal setup improves to -0.269404054 ns, hold stays +0.120968238 ns,
and electrical checks pass. The remaining path contains single-sink chains
spanning 63–138 um. An explicitly bounded 150 um trial removes 203 buffers,
reaching +0.008783197 ns setup with the same hold margin. It fails electrical
checks at three drivers (maximum slew 0.60 ns against 0.50 ns); this candidate
is not adopted. The selector retains its 60 um default and all clock, port,
register-load and protected-neighbor exclusions. Extended-span and native
reload regressions pass.

The diagnostic now permits bounded additional rounds and an explicit final
electrical repair using the original interface limits with no extra repair
margins. Its identity-buffer/drive equivalence checks cover the inserted cells
as well as removed ones. Both native variants pass (2.66 seconds total).
`global-unbuffer-repair-150/` runs eight rounds toward 0.5 ns estimated setup
before repairing actual electrical violations; qualification remains false.

That run finishes at +0.092641450 ns setup and +0.120968238 ns hold. Generic
electrical repair inserts one buffer but leaves two single-strength `a221oi`
drivers above the 0.50 ns slew limit. The diagnostic now uses the maintained
output-isolation helper for those cells, followed by legalization and complete
route/RC rebuilding. Four related CTests pass in 7.05 seconds. A continuation
from this exact checkpoint uses an explicitly bounded 200 um selection span
under `global-unbuffer-repair-200/`; it is still a diagnostic.

The continuation completes with +0.184150472 ns nominal setup, +0.120968238 ns
hold and a clean electrical report. It removes another 194 buffers and inserts
four, with exact endpoint/identity-buffer equivalence passing. Both native
early-routing regressions pass, including fresh OpenRCX extraction and explicit
rejection of qualification claims (415.51 seconds including EDA-slot waiting).
`early-route-200/` now measures the full receiver through early detailed-route
iteration 1, followed by new extraction. Its result cannot qualify routing or
layout, even if timing and the router report pass. Full detailed routing,
independent gate replay, DRC/LVS and chip integration remain required.

The probe is still running its first detailed-routing iteration. The existing
receiver continuation now accepts this diagnostic path only after its extracted
setup, hold, electrical and endpoint checks pass. It binds the probe to the
exact candidate/SDC and cell models, reloads global timing within 1 ps, forces
full routing, then repeats extraction before exact gate replay and layout.
Four CTests for route admission, replay, layout and continuation pass (1.86
seconds). The new real-IHP `route_handoff_native` regression is queued behind
the live probe. A dependent command launches `route-continuation-200/` only
if all five CTests pass; the continuation itself rejects a failing probe.
The batch log is `trellis-compressor-revision/handoff-continuation-ctest.log`.
These pending runs do not establish receiver or complete-chip qualification.

Twelve additional superseded receiver checkpoints/intermediates have verified
gzip archives, recovering 840 MiB. The manifest is
`trellis-compressor-revision/retired-route-preparation-archives.json` and includes
original/archive hashes and restoration commands. Current inputs, the latest
extracted core database, all SPEF measurements and analog waveforms are retained.

Completed historical diagnostics and unused failed intermediate ODBs have
verified gzip archives. Eighteen files recover 492 MiB; original and archive
hashes plus restoration commands are recorded in
`trellis-compressor-revision/retired-unbuffer-archives.json`. The original
`finished_global.odb`, active inputs and reference analog waveforms remain
uncompressed and available.

The production hold-cell trimmer in `finish_timing.tcl` also refreshes reduced
global RC before evaluating a replacement and after rolling it back. Its new
native regression checks every decision against an independent RC refresh,
forces rejection with an impossible hold requirement, and confirms that the
original cell and timing return. The combined rebuffer regression passes in
4.36 seconds. It does not claim closure of the full receiver.

The mapping retry also corrects the old Yosys constraint-file quoting behavior
and reuses its saved pre-ABC intermediate. `gate-timing-mapping-v3/` completes
mapping in 41.74 seconds. Its cell-only setup is +2.54 ns versus +2.91 ns for
the existing decoder, so the alternative is rejected without adoption or
additional gate replay. The original functionally verified mapping remains
the physical implementation input.

## November 1 routed receiver and completed standalone SRAM checks

The deferred-filler receiver route completes with zero detailed-router
violations. Fillers are restored before extraction. Extracted slow-corner
setup is -1.186617970 ns; fast-corner hold is +0.125671804 ns. Slew and
capacitance violations remain. The subsequent gate-model build times out
after 1800 seconds. This candidate is not qualified. The maintained physical
flow now checks extracted timing before spending time compiling the gate model;
passing candidates still require replay, full DRC and LVS. The updated physical
guard CTest passes, including rejection before compilation and mandatory
compilation after a timing pass.

`receiver_extracted_sizing.py` evaluates sizing against this actual routed
SPEF and both timing corners, retaining the existing full-precision constraints,
input hashes and full path reports. The first attempt confirms 268 failing
setup endpoints, then OpenROAD crashes in `GlobalRouter::addDirtyNet` during
its first resize. Restoring the saved guides initializes the router net map
before its callbacks. That workaround completes on a small routed clock-control
fixture, with 57 final cell replacements passing independent sizing equivalence.
The registered `ihp_receiver.routed_sizing` regression generates a fresh tiny
circuit, detailed-routes it, reloads its database/SPEF in another OpenROAD
process and checks resizing plus unchanged endpoints. It passes in 1.5 seconds;
removing guide restoration from the same fixture reproduces the native crash.
The `receiver-extracted-sizing-v3/` retry completes with 342 final cell
replacements and passing independent sizing equivalence. After reloading the
original extracted wires, setup is -0.848399580 ns and hold is +0.125671804 ns.
This remains a failing candidate estimate: cell changes require legalization,
routing, new extraction, functional and layout checks. The optimizer's internal
-0.609 ns estimate is not the extracted-wire result.

The compact filled SRAM passes full DRC with zero violations across 868
categories, including density, maximum and antenna rules. A fresh maintained
filled SRAM run also passes complete graph correspondence and its independent
device/connection audit. These are layout/connectivity results; SRAM electrical
characterization and full-chip qualification remain open.

The first assembled SRAM/control retry stops because the isolated rule deck
lacks `pdk/versions.txt`. Preparation now copies that required runner metadata
and hashes it with the fixture artifacts. Missing metadata fails preparation
explicitly. Nineteen embedded-audit/packaging cases pass under the registered
CTest. The `embedded-sram-v4-candidate/` run passes preparation, but is stopped
when review of the retained extraction shows its 20-minute budget is too short:
that earlier complete assembly extraction took 3313 seconds. The maintained
flow now allows 7200 seconds, with unchanged memory and physical limits.
`embedded-sram-v5-candidate/` completes extraction in 3405 seconds. Its native
database reader then rejects a malformed `N(23462)` comparison record. The
layout and both netlists precede that record and remain retained. The new
comparison-reset regression passes seven cases, including byte-identical native
circuit preservation and rejection of wiring/parameter defects after reset.
The original database is immutable; `resume_embedded_sram.py` writes separate
receipts and reruns the comparison and independent audit.

The resumed comparison exposes 21 physically labeled but unexported unused
SRAM outputs: all A data outputs and B bits 11 through 15. Each reference
connection is a one-terminal unused stub in all eight banks. The guarded
interface promotion preserves the extracted internal nets and adds only the
21 hierarchy ports and 168 private parent stubs. Seven regression cases pass,
including rejection of externally used/shorted outputs and missing drivers.

Leaf-first correspondence avoids native KLayout mismatch-sort crashes and an
assertion during unseeded matching. Its eight regression cases require the
final complete comparison and reject changed enclosing connections, devices,
ports and resistance. The final `embedded-sram-v5-recomparison-v8/` run passes
the complete assembly graph comparison and both independent audits: 94 circuit
definitions, 8179 devices in definitions, 47662 nets, 1258 pins and 65429 child
instances, including 327 derived metal resistors and two antenna diodes.
Refinement uses 25663 unique net hints; the remaining 68 repeated instances
resolve in 69 search steps. Each SRAM still contains 221880 recursively
instantiated devices. These counts describe hierarchy, not total chip transistor
count. Full DRC on that exact assembly using KLayout 0.30.12 completes with
24 `NW.d` violations at row-end corners beside the SRAM banks. A cropped
corner reproduces one violation with 0.30.12 and none with 0.30.5. The old
geometry pass therefore does not establish a pass with the newer checker.
The unmodified rule deck remains authoritative for the new run.

`row_end_wells.py` creates a separate candidate with twelve 80 nm NWell
extensions, removing that violation in the cropped diagnostic without adding
other crop violations. Its complete-assembly geometry guards retain all other
masks, hierarchy and boundary, preserve all 1397 connected well components,
and reject coverage of device geometry. The added well area is 405.6192 µm².
Nine guard cases pass; four flow tests prove that a changed well mask must run
fresh DRC and extraction/LVS and cannot reuse the old graph pass.
`embedded-sram-row-wells-v2-qualification/` now completes full DRC with zero
violations across all 868 categories and 37 main rule tables, including density,
maximum and antenna rules. Fresh extraction and complete graph comparison also
pass, with unchanged audited counts: 94 circuit definitions, 8179 devices in
definitions, 47662 nets, 1258 pins, 65429 children, 327 derived metal resistors
and two antenna diodes. The full DRC, extraction and comparison take 4904,
3198 and 120 seconds respectively. This qualifies assembly geometry/connectivity;
electrical and whole-chip qualification remain open.
See [the assembly workflow](sram-assembly.md) for the regeneration commands and
the limits of unused-interface normalization.

Receiver clock/delay trials use the original constraints, require at least
0.1 ns hold margin and reject worsening setup/total negative slack. The first
run improves worst setup to -0.787555798 ns before repeated SPEF reloads exhaust
the OpenROAD address-space limit; that run fails and has no qualified output.
The revised trial flow retains the existing RC while replacing cell models,
then reloads the resulting ODB and original SPEF in an independent process.
Setup/hold results must agree within 1 ps. Its generated-fixture CTest passes
acceptance, rejection and restoration checks. The 25-trial retry completes with
nine final replacements, independent functional equivalence, and a fresh-process
result of -0.661511779 ns setup / +0.120649830 ns hold. A 146-trial extension
completes with 23 final replacements relative to the sized seed and improves
that to -0.635514796 ns / +0.139985144 ns, again independently reproduced.
The worst remaining path is a decoder metric-register self-loop, so useful
clock skew cannot close it. The buffer-removal optimizer completes without
removing any cells or improving timing. Its independent connectivity check
passes with zero removals. A subsequent 200-trial data-path sizing diagnostic
estimates -0.549208701 ns setup and +0.139985368 ns hold; this preliminary
result still lacks an independent reload and functional proof.
`receiver_path_sizing.py` now iterates same-function data-path sizing with
fresh process reloads between rounds, a final independent recheck and exact
endpoint/Boolean-model comparison. Seven selection guards pass. Four rounds of
200 trials complete, with 90 final cell changes relative to the seed, independent
functional equivalence and fresh-process timing reproduction. Setup improves to
-0.448443085 ns, with +0.139985368 ns hold. These fixed-wire estimates still fail
setup and require legalization, new routing and extraction.

The next limited ECO selects only explicit critical-path `place`/`wire` buffers
with a single-fanout input, one or two output sinks, and at most 60 µm from the
driver to each sink. Clock nets, ports, register drivers, protected cells/nets,
fixed neighbors and overlapping edits are excluded. A real OpenDB regression
checks these boundaries. A 20 µm diagnostic selected only one buffer and was
stopped before routing; an independent inventory found 24 topology-compatible
candidates within 60 µm. `receiver-local-route-v2/` removes 19 non-overlapping
buffers. Legalization passes, with 9.1 µm maximum displacement. Independent
seed, buffer-contraction, filler-deferral and legalized-network endpoint checks
all pass. The global-route estimate is +0.353774786 ns setup and +0.144349426 ns
hold. Detailed routing was stopped during pin access to check electrical limits
before committing to the full route. An independent reload reproduces both
slacks and reports no global slew, capacitance or fanout violations.

`resume_receiver_route.py` adopts the immutable global checkpoint, repeats its
endpoint/placement/electrical checks, and then performs detailed routing and
fresh extraction in separate processes. Five failure-gate regressions pass,
including rejection of changed connectivity, negative setup and slew failures
before routing. Four data-path selection cases and the native OpenDB selector
test also pass. The continuation is under `receiver-local-route-v2-resumed/`.
It restores fillers before extraction and rechecks all functional endpoints.
Full-chip and physical qualification remain false; receiver replay, full
DRC/LVS, extracted electrical limits and integration remain required.

The independent `receiver_gate_replay.py` job is queued behind that continuation
under `receiver-local-route-v2-replay/`. It allows a 7200-second gate build with
one compiler worker, then performs all three original normal/fault/clock-enable
replays with exact native-trace comparison. The 46 hashed native-oracle inputs
are current. Seven registered guard cases pass, including changed inputs,
unfinished routing, electrical failures, altered executables and a failed
clock-enable trace followed by a verified executable reuse. A replay pass is
still pending. This job does not change the running route or its qualification
criteria and does not establish DRC/LVS or a complete ASIC.

The resumed detailed route subsequently completes in 13600 seconds with zero
router violations. Fresh extraction takes 99 seconds and reports -0.591841280 ns
setup and +0.123347111 ns hold. Slew reaches approximately 0.68 ns against the
unchanged 0.50 ns limit; net capacitance also exceeds 0.10 pF. The independent
replay job rejects this failed timing candidate before compilation. The global
estimate was optimistic and does not establish routed timing closure.

`receiver_routed_seed.py` adopts the immutable new route and reproduces both
slacks exactly in a fresh OpenROAD process before further optimization. Four
guard cases pass; its receipt remains diagnostic only. Two new 200-trial sizing
rounds under `receiver-fresh-path-sizing-v1/` estimate -0.443105131 ns and then
-0.412551790 ns setup, with hold unchanged at +0.123347111 ns. Their final
independent reload reproduces both slacks exactly, and the 36 final cell changes
pass sizing equivalence. These remain fixed-wire estimates, not physical closure.

`receiver_electrical_repair.py` repairs the measured overloaded nets, verifies
same-function sizing and newly inserted buffer connectivity, legalizes and
rebuilds global routing. Its targets are 0.30 ns slew and 0.07 pF load; final
acceptance remains 0.50 ns and 0.10 pF with fresh extracted RC. Seven combined
repair-equivalence regression cases pass, including rejection of a corrupted
common buffer model and explicit identity proof for a delay-cell replacement.
The first native run stops at that formerly unsupported identity replacement;
its receipt and original checker snapshot remain retained. The complete
`receiver-electrical-repair-v2/` rerun passes all four endpoint checks, with
286 inserted buffers and 723 same-function replacements. Legalization has no
failures and at most 15.1 µm movement. Global setup/hold estimates are
+0.306714654/+0.133639216 ns, with no reported electrical violations.
Old SPEF is never used to qualify
the changed topology. Detailed routing, fresh extraction, exact replay and
full layout verification remain required after this candidate stage.

A wire-capacitance sensitivity diagnostic reproduces nominal +0.306714654 ns
setup, then estimates -0.426508605 ns at 1.5x wire C and -1.149842978 ns at 2x.
These factors are optimization probes, not characterized process corners.
Rather than immediately spending another long detailed route on the nominal
pass, `receiver_margin_route.py` sizes against 1.5x wire C and relegalizes/rebuilds
the route estimates. Both nominal and enlarged-C setup/hold must exceed 0.1 ns,
and nominal electrical constraints must pass before detailed routing starts.
Five margin-screen regressions and the native clock-path selection test pass.
The sizing-only
`receiver-margin-route-v1/` run performs 514 resize operations, leaving 469
changed instances and passing sizing/placement equivalence. After legalization,
the enlarged-C setup estimate improves to -0.039351635 ns, so the margin gate
correctly stops detailed routing. A follow-up attempt is stopped early to fix
the valid zero-removal case. The current run, `receiver-margin-route-v3/`,
additionally selects at most 200 explicit critical-path buffer names and uses
the existing guarded 60 µm selector. It proves buffer contraction separately,
then relegalizes and checks both timing estimates again. An empty selection
retains the sized topology and still reaches the margin gate. That experiment
removes twelve buffers but worsens enlarged-C setup to -0.179490209 ns; the
candidate is rejected before routing. It is not adopted by the next run.

The `receiver-margin-route-v4/` run starts again from the verified electrical
repair. After data-path sizing, it trials drive variants of the two clock-buffer
levels nearest reported critical registers. Each trial must preserve at least
0.1 ns hold and improve setup or total negative slack without worsening either.
The native OpenDB selector test covers deduplication, fixed/protected drivers,
protected nets and missing endpoints. Both data and clock replacements require
independent model/connectivity proofs, followed by legalization and new routing
estimates before the margin gate. Clock/period constraints remain unchanged.
It passes equivalence for 469 data-path and four clock-buffer replacements.
After legalization, nominal setup/hold are +0.530870497/+0.133639097 ns; the
1.5x-C estimates are +0.014392043/+0.147574186 ns. Nominal electrical checks
pass. It misses only the extra 0.1 ns optimization setup target, so that run
does not proceed to detailed routing.

`receiver-margin-route-v5/` explicitly selects
`--minimum-optimization-setup 0` to measure this positive-estimate candidate's
actual routed RC. This changes an optimization screen, not final acceptance:
negative estimated setup is still rejected, the estimated hold screen stays at
0.1 ns, and final extracted timing/electrical checks use the original limits.
The selected target is recorded in the receipt. The default remains 0.1 ns.
Seven margin-screen tests cover the default, explicit zero, rejection of negative
setup, unchanged hold margin and nonfinite/negative target rejection.
Fresh final OpenRCX uses the original technology values; the optimization
factor cannot establish extracted timing or full-chip qualification.

The v5 sizing and independent endpoint checks complete successfully and the
candidate is now in detailed routing. `receiver-margin-route-v5-replay/` waits
for a fresh extracted timing pass before its exact normal/fault/clock-enable
replays. No routed timing pass has been established for v5 yet.

`receiver_layout.py` continues from those two completed receipts without
recompiling the gate model. It checks their complete input/artifact hashes,
the executable and all three native-reference trace comparisons, and requires
the DEF/CDL to be bound by the same routed producer. It then streams physical
cells, fills, runs full KLayout DRC (including density, maximum and antenna
rules), and reads the persisted transistor-level LVS comparison with the full
52-port receiver interface. Its 14 registered handoff regression cases pass.
Preflight locates all 123 physical-flow inputs; regenerated routing technology
matches the original mapped work's LEF byte for byte. Live dependencies are
waited on outside the shared EDA slot. A geometry pass still leaves post-fill
coupling and complete-chip integration/electrical qualification open.
The continuation is queued under `receiver-margin-route-v5-layout/`. The route
has completed pin access and track assignment and entered detailed wire routing.
Superseded v4 routing and pre-well-repair SRAM comparison databases are retained
as verified lossless gzip archives with adjacent restoration records, recovering
about 488 MiB without changing active inputs or current accepted SRAM evidence.

## October 31 SRAM assembly and refreshed frame-buffer implementation

The frame-buffer stream now requires the exact filled SRAM GDS and physical-width
schematic from a completed maintained SRAM LVS flow. `sram_library.py` verifies
the source/artifact hashes, independent audit and native comparison database.
The stream checks that the replacement retains the original boundary and top
pin locations, then inserts that same macro in all eight banks. It no longer
silently selects the original unfilled library and original resistor widths.
Both registered CTests pass: eight provenance guards and eleven stream/interface
cases. The real compact filled SRAM also passes the placement-interface check.
Assembled-macro DRC/LVS and extracted electrical acceptance remain required.

The current frame-buffer cell/SRAM proofs have unchanged input/artifact hashes.
Older placement and routing receipts are stale, but a subsequent audit finds
the preserved `flow-clock-leaf-eco/run-4slxmuha/routed-proof.json` and its crossing
proof still current: all 137 routed and 541 crossing input/artifact hashes match.
That route has +95.416 ps setup and +113.400 ps hold slack, exact frame replay
and passing extracted crossing checks. The redundant fresh-placement queue is
stopped before execution; its stopped receipt remains in `flow-control-refresh-v1/`.

`embedded_sram_lvs.py` now applies SRAM refinement inside the full assembly
and independently audits every enclosing instance connection and top port.
Seventeen tests pass, including changed bank connections, devices, interface
contracts, geometry, placement arrays, conflicting provenance and antenna diodes.
The assembly audit explicitly supports `dantenna`/`dpantenna`: both terminal
polarities and multiplicities must match, positive A/P must remain within the
foundry EnDiode's 1% tolerance. The standalone SRAM audit still rejects diodes
unless enabled for an assembly. Its 27 existing audit/correspondence cases pass.
The original standalone checker source is retained by its hash; the queued
maintained SRAM flow will freshly verify this checker revision.
The preparation preserves
every nontext shape and placement from the retained DRC-clean frame-buffer GDS;
only SRAM hierarchy names and internal labels are normalized. Its real
extraction/comparison/full-DRC run waits for maintained SRAM regeneration under
`build/clock-integration/extracted-repair/embedded-sram-v3-candidate/`, using the
maintained `flow_control_sram_flow.py` command.
Receiver detailed routing continues in `receiver-deferred-fill-v5-candidate/`;
the compact SRAM full DRC and maintained SRAM regeneration are also queued.
These queued or running jobs supply no completed physical verdict yet.

## October 31 completed transmitter qualification and receiver retry

The repaired trim-27 transmitter completes the full 1.544 µs physical transient
and passes the original independent electrical checks and schematic comparison.
The maximum checked plateau difference is 8.770312 mV against the unchanged
10 mV comparison limit. All 1603 child inputs and 1600 supervisor inputs verify.
Receipts are `charge-trim27-reference-access/checks.json` and
`charge-trim27-reference-access.json` under the recovery directory. The scope
retains component-level extracted RC and ideal inter-macro wires at TT/27°C;
routed interconnect, calibration, PVT and full-chip qualification remain open.

The queued 200 µm receiver placement trial completes legalization and preserves
the logical netlist, but global routing fails with 60311 overflow units after
five diagnostic iterations. Estimated setup slack is −29.899874 ns and hold
slack is −1.469185 ns. This candidate is rejected for integration. The original
checkpoint is preserved.

Critical-path diagnosis finds 149 buffers on the worst data path. Two isolated
buffer-removal candidates preserve Boolean connectivity: timing-driven removal
of 703 buffers improves estimated setup from −24.060932 ns to −19.590942 ns;
short-chain removal of 4170 buffers improves it to −16.552114 ns. Both retain
−1.469185 ns hold slack and fail timing. Neither is accepted for integration.
The new buffer-selection guards and independent connectivity checker pass
their two registered CTests. See [receiver-buffers.md](receiver-buffers.md).

The original routing repair inserted 38678 buffers, grew area by 29.8% and
increased legalized wirelength. A separate diagnostic now returns to the
retained pre-expansion checkpoint, preserves its topology, and measures global
routing and timing before any further electrical repair. Initial global
routing has zero overflow. Sizing 63 cells on that checkpoint passes independent
cell-model/connectivity equivalence and nominal global-route estimates:
setup +0.015996 ns, hold +0.142064 ns, no reported slew/capacitance/fanout
violations. A 648-cell sizing trial under the conservative 2.5× capacitance
estimate still fails that estimate with −2.331374 ns setup. These estimates
are not extracted timing closure. The 63-cell candidate is proceeding through
antenna preparation, detailed routing, extraction and the maintained module
qualification flow, with no change to final acceptance limits.

Antenna insertion initially creates three fanout violations. Three buffers
and one additional antenna diode repair them at the final electrical limits;
nominal setup/hold remain +0.015996/+0.142064 ns and antenna/electrical reports
are clear. Detailed routing exhausts its initial 4 GiB virtual-memory limit.
A second attempt stops at the resident-memory watchdog before system memory
exhaustion. The prepared checkpoint is preserved. The next candidate defers
402502 filler cells during signal routing and restores them before extraction
and the complete module checks; functional placement/connectivity must remain
identical across that deferral.

The first deferral attempt preserves placement but stops because the signal
netlist checker does not accept pinless filler instances. An explicit filler
mode now verifies removal of exactly 402502 fillers, no functional cells and
unchanged signal endpoints; its negative regression cases and sizing checks
pass under CTest. Detailed routing has restarted with 110870 remaining
instances. Fillers must be restored before physical qualification.
The selected 63-cell sizing proof also passes again with explicit preservation
of module port order and bus direction.
With fillers deferred, track assignment completes at about 3 GiB resident
memory and the first detailed-routing iteration starts. This crosses the
previous memory failure, but supplies no completed routing or timing verdict.
That attempt later stops when available system memory falls below 256 MiB;
its own peak remains about 3 GiB. The next retry moves the large connectivity
proof into a child process, retains a compact summary in the supervisor, and
waits for 4 GiB available before starting. It keeps the existing runtime
memory watchdog and all physical acceptance limits.
The idle 4 GiB preflight is subsequently replaced with a 3.25 GiB start
threshold, based on the measured 3.13 GB router peak plus its unchanged
256 MiB runtime reserve. The retry remains queued until that memory is
available; no routing or timing success is implied.
That retry subsequently passes preparation and filler-removal equivalence,
completes pin access and track assignment, and enters detailed-routing
optimization. Peak resident memory is approximately 3.14 GB. Detailed routing,
fill restoration and extracted timing acceptance remain pending.

An explicit SRAM reference-circuit variant now uses the measured 0.20 µm
bitcell resistor widths while preserving the original GDS geometry. Its array
LVS matches 82 ports and 388 devices. All 90 local PSP sensitivity cases pass
write/read/retention checks across five process corners, three temperatures,
three supplies and two write polarities; maximum waveform difference is
0.392 mV. Full-macro geometry also identifies 0.24 µm peripheral Metal3 bodies.
The maintained generator checks those dimensions and retains 28 indented dummy
cell instances that an earlier diagnostic hierarchy traversal missed. Its two
CTests pass. Full-macro LVS times out after one hour in strict comparison;
this supplies no matching verdict. The next run performs full DRC first,
persists extraction separately, then allows four hours for strict comparison.
The full DRC run completes with three minimum-density violations: M5.j,
TM1.c and TM2.c. Extraction is retained independently. The unrestricted LVS
search runs for more than three hours without a verdict and is stopped in
favor of a diagnostic search limited to depth 8 and branch complexity 500;
device, connectivity and strict port checks are unchanged. No full SRAM
qualification is claimed.
See [sram-reference.md](sram-reference.md) for the intentional schematic
revision and the electrical experiment's limits.

The new `sram_fill.py` candidate adds only isolated Metal5/TopMetal1/TopMetal2
filler shapes. Its registered CTest passes, and an independent full-layout
region comparison confirms unchanged original geometry, boundary and top
labels. Measured coverage is 44.03%, 44.19% and 44.19%. The initial 2 GiB
virtual-memory limit stops the angle checks with `std::bad_alloc`. The retry
uses a 4 GiB virtual-memory cap and a 512 MiB available-memory watchdog. It
completes the full DRC in 2069.3 seconds: zero violations, 868 rule categories,
all 37 main tables, density, antenna and maximum rules. All recorded input
hashes still match. The receipt is
`build/clock-integration/extracted-repair/sram-upper-fill-v2-candidate/drc_checks.json`.
LVS, post-fill extraction and electrical qualification remain required.

The bounded flat LVS comparison completes without a match. Hierarchical
diagnostics retain complete array tiles and expand boundaries crossed by
shared diffusion. Local supply-label normalization preserves port IDs and
every net endpoint; it never joins nets. Independent region, shape and
placement comparisons confirm unchanged physical geometry and top labels.
The ten-minute comparison still times out. A smaller diagnostic search now
requires matching named ports and retains the prepared netlists for local
analysis. LVS remains unresolved, so this hierarchy preparation is not
enabled in the production flow.

Guided diagnostics subsequently match all twelve retained subcircuits and
all 223 top ports. The top still has 184 mismatched MOS devices, concentrated
in tie cells and nearby logic. Retaining previously matched logic hierarchy
exposes an incomplete `DFNQMX2IX1` boundary: its schematic QIN pin lacks a
local extracted counterpart. The next comparison expands that boundary
while preserving all devices and parent connections. A new independent graph
audit requires complete net/device/pin/instance bijections and checks actual
endpoints and parameters rather than trusting the tool's match flags. Its
fifteen regression cases pass in `analog.1g.ihp_sram.lvs_graph_audit`. It also
passes the real 32-bit array's 388-device/238-net/82-port mapping, deriving
geometry-defined resistor values from the unchanged extraction deck. No guided
diagnostic has yet passed complete-macro LVS and that independent audit.

The pinned KLayout 0.30.5 crashes while constructing some guided comparison
reports. An isolated official 0.30.12 installation completes the same case
and produces a failure report; the production toolchain is unchanged. All
38 retained subcircuits match in that diagnostic. At the top, physical
extraction has already expanded part of the INVX2 and NAND2X2 column logic;
the different child-instance counts account for exactly 1792 direct devices.
The next comparison expands those instance boundaries without removing devices.
Its fresh extraction attempt stops at the memory guard while receiver routing
continues. The subsequent retry reuses the preserved extracted/reference
netlists and verifies the earlier run's source hashes.

The cached schematic export exposes a separate writer defect: uppercase metal
resistor model names receive numeric instance prefixes, which the SPICE reader
ignores. A diagnostic copy restores the `R` prefix on exactly 327 records,
preserving every node, model and parameter. Comparison then matches all 6568
direct top-level devices and all 223 ports. It still leaves 88 child instances
unpaired on each side; this is not a full-macro LVS pass. A broader bounded
search stops at its available-memory guard while receiver routing continues.
The independent graph-audit regression also passes with the isolated KLayout
0.30.12 Python library. Connectivity refinement is being investigated before
another full comparison; the production SRAM flow is not yet changed.

SRAM integration, receiver electrical acceptance, routing/timing and complete
chip verification remain unfinished. No final ASIC GDSII is qualified.

## October 31 native SRAM correspondence and serialized routing retry

The maintained `sram_lvs_compare.py` resolves repeated structures through joint
graph refinement and a bounded residual bijection search. The independent
`sram_lvs_audit.py` checks actual netlist endpoints, including stale-snapshot
negative cases and rejection of missing extracted resistor values. The two
registered CTests cover 27 cases and pass with KLayout 0.30.5 and isolated
0.30.12. `qualify_sram_lvs.py` checks extraction provenance and the saved native
database hash before comparison; it never marks the SRAM or chip qualified.

Native full-macro extraction passes the complete correspondence audit in
`sram-native-lvs-v30-candidate/checks.json`: 39 circuit definitions, 7775 devices
within those definitions, 27488 nets, 814 pins and 2116 child instances. The top
contains 223 ports and recursively instantiates 221880 devices. All 327 metal
resistors are checked against the extraction values and sheet-resistance model.
The 68 remaining symmetric tie/buffer instances resolve in 69 search steps.
This uses the original extraction database directly. A diagnostic SPICE cache
had discarded resistance values and class metadata; its earlier graph-only
pass is superseded by the native result. The intentional physical-width
reference revision and its electrical limits remain as described separately.

The filled hierarchical candidate in `sram-filled-lvs-v4-diagnostic/` preserves
the previously DRC-clean macro's complete flattened geometry on every layer and
its top labels. It retains 178 cells and occupies about 629 KiB, compared with
the 223 MiB flattened candidate. Its fresh filled extraction and native graph
audit run before the next receiver attempt; PEX/electrical qualification and
chip integration remain open.

The fresh filled-macro extraction subsequently passes the same complete native
graph audit in `sram-filled-lvs-v1-candidate/checks.json`. The source-controlled
preparation, isolated-tool setup and end-to-end `sram_lvs_flow.py` now reproduce
the required inputs from the PDK. Five additional preparation cases pass in
CTest, bringing the preparation/correspondence/audit total to 32. Full DRC of
the compact hierarchy and an end-to-end regeneration replay are queued behind
the receiver; the replay is not yet a completed result.

Receiver routing reaches 50% of its first iteration at approximately 4.125 GB
peak resident memory, then stops when system available memory drops below
256 MiB. No complete routing iteration or restart snapshot was produced.
The concurrent SRAM diagnostic reduced available headroom. The next retry
serializes owned heavy jobs, waits for 4.25 GiB available before launch, allows
4.5 GiB resident memory and retains the 256 MiB runtime reserve. Preparation,
filler restoration and final physical/timing acceptance limits are unchanged.

## October 31 overnight qualification watch

At 07:12 UTC the repaired transmitter transient reaches approximately
1.194 µs of 1.544 µs. Its complete log contains no error or warning markers;
all 1603 child inputs, 1600 supervisor inputs and 194 queued receiver inputs
still match their recorded hashes. The 512 MiB waveform reservation remains
allocated, with more than 10 GiB additionally free. There is no completed
candidate electrical result yet. The receiver's 200 µm placement trial still
waits behind the transmitter and will acquire the shared memory slot after
that run finishes. Neither job's source, stimulus or acceptance limits change.

A read-only observer records these jobs every 45 seconds and verifies their
inputs at state changes. Its latest snapshot and event history are under
`build/clock-integration/extracted-repair/qualification-observer-20261031/`;
the observer stops after both jobs finish or its 24-hour watch limit. It does
not restart tools or qualify the ASIC. The full-log health audit is
`reference-access-active-health-20261031.json` in the recovery directory.
SRAM integration, receiver routing/electrical limits, timing and full-chip
qualification remain open; no complete ASIC GDS is qualified.

## October 30 completed transmitter and receiver diagnostics

The full trim-27 physical transmitter run completed its 1.544 µs transient
with 216236 retained records, but failed the original weighted-level contract:
maximum plateau error 107.678 mV exceeds 20 mV. The worst +1 V target reaches
1.107678 V. The full trace is preserved in a verified lossless archive under
`charge-trim27-physical-recovery-v2/`; the replay diagnosis is
`completed-trim27-physical-diagnosis.json`. The passing schematic run does not
qualify the physical transmitter.

Isolated DC replay of the same bias subcircuit gives 0.969505 V physically
versus 0.946182 V schematically. A diagnostic reducing only extracted wire
resistances by 1000 gives 0.946206 V; reducing only ground-wire resistance
also removes most of the offset. These counterfactuals identify a routing
sensitivity, not a valid physical implementation. Original PDK devices and
resistor-body models are retained. Receipts are `tx-bias-dc-diagnosis/` and
`tx-bias-selected-wire-diagnosis/` under the recovery directory. A private
rail-only candidate passed local checks but barely reduced the bias offset.
Adding direct source/drain access and parallel contacts across all reference
fingers reduces the trim-27 difference from 23.433 mV to 4.862 mV at 3.3 V.
The maintained optional `transmit_bias_reference_access.py` and registered
`analog.1g.ihp_transmit.bias_reference_access` CTest pass local DRC/LVS,
missing-switch rejection and all 64 DC codes. The default macro is unchanged.
The receipt is under `build/regeneration-audit/1G/IHPSG13G2/tests/results/`.

A complete-transmitter DC diagnostic with KLU fails its operating-point
solve. The alternate sparse solver completes the original circuit at
1.101957 V for a 1 V target. Unassisted repaired-circuit solves time out at
240 and 600 seconds. A retry using 51 baseline voltages as numerical starting
guesses completes with the exact maintained extraction: 0.998986 V output,
−1.014 mV target error, 0.950644 V BIAS and 41.8214 mA supply current.
The circuit equations and limits are unchanged. Solver diagnostics remain
recorded; DC screening does not establish transient or model-range qualification.
The receipt is `tx-bias-reference-access-loaded-nodeset/checks.json`.

The full physical transient now runs under CTest in
`charge-trim27-reference-access/`, supervised by
`qualify-charge-trim27-reference-access.py`. Only the current-bias macro's
extracted network changes. The original complete passing schematic, stimulus,
1.544 µs duration, electrical limits and comparison are retained. A 512 MiB
trace reservation and 24-hour execution limit protect the run. Ten preflight/
substitution regression cases pass; five registered routing guards also pass,
including exact device/port preservation and rejection before mutation.
No completed candidate transient result is available yet.

The receiver spreading diagnostic v3 retains the byte-identical logical
netlist and reaches zero global-route overflow on the same die, but estimated
timing fails: setup slack −16.22646 ns and hold slack −0.89022 ns, with slew
violations. It is not an acceptable replacement for the original checkpoint.
The first trial rejected an unsupported OpenROAD flag; the second performed
no movement and was stopped before redundant routing. All three attempts
remain recorded. The v3 receipt is `receiver-spread-diagnostic-v3.json`.
Two local-padding trials add one site of spacing on each side of cells in
the two measured hotspot bins. With every cell limited to 50 µm of movement,
945 instances fail legalization; a 100 µm bound leaves 278 unlegalized.
Both attempts are rejected before routing or timing qualification. Their
receipts are `receiver-local-padding-diagnostic.json` and
`receiver-local-padding-100-diagnostic.json`. No new receiver candidate is
accepted, and the original checkpoint remains preserved. A 200 µm trial is
queued behind the transmitter, with the same logical netlist, die, routing
resources and final checks. The supervisor requires 512 MiB free after the
transmitter finishes; the earlier complete spreading diagnostic occupied
164 MiB. Its receipt is `receiver-local-padding-200-v2-diagnostic.json`.
Full receiver route/timing closure, SRAM LVS, receive electrical limits and
full-chip qualification remain open. No complete ASIC GDS is qualified.

An isolated SRAM bitcell repair now physically widens all six metal resistors
from 0.20 to 0.26 µm, retaining their 0.60 µm extracted length. The v3 candidate
passes the metal-rule table and preserves extracted MOS devices and resistor
connections. The first candidate failed spacing; the second passed spacing
but extracted incorrect end dimensions. Both failures remain recorded.
The complete isolated-cell DRC comparison covers 879 categories, including
density and antenna: both original and candidate retain 17 violations across
11 categories. Only the numeric Metal2/Metal3 density markers change; neither
isolated cell passes full DRC. Receipts are `sram-resistor-width-v3-diagnostic/`
and `sram-resistor-width-full-drc/` under the recovery directory. This private
candidate has not been integrated into the SRAM or production stream; array
abutment, complete SRAM LVS/DRC and timing remain required. Original PDK,
schematic and active transmitter/receiver inputs are unchanged.

The original 32-bit array, including its four real tap cells and unchanged
placements, rejects this widening candidate: full array DRC finds 80 new
Metal3 spacing violations. Its original six density violations remain.
The receipt is `sram-width-array-v2-diagnostic/checks.json`. Flat extraction
initially exposed internal labels as 234 top ports instead of the array's 82;
deep extraction instead left unmatched child supply interfaces. Both failed
diagnostics are retained. A reproducible fixture preserves every physical
polygon and exactly the array's original 82 top port labels. Strict flat LVS
then rejects the original widths and matches the widened candidate's 82 ports
and 388 simplified devices. The adapter changes only legacy resistor model
aliases and injective supply-name spelling, retaining all original dimensions
and connections. See `sram-width-array-top-ports-lvs/checks.json` and
`sram-array-top-port-regeneration-comparison.json`. This LVS result does not
override the array spacing failure: the candidate remains rejected for
integration and requires a different wordline resistor layout.

An upper-metal trial moves even-row wordline resistors to Metal4 and odd-row
resistors to Metal5, with real Via3/Via4 connections and unchanged schematic
dimensions. Its first version fails 64 minimum-area checks on isolated Metal4
landing pads. Extending those pads outward produces the v2 candidate: strict
array LVS matches all 82 ports and 388 simplified devices, and all 876 DRC
categories retain only the original six density violations. Two CTests also
confirm rejection of an intentionally missing via and a shorted resistor.
Receipts are `sram-wordline-upper-metal-v2/checks.json` and
`sram-upper-metal-faults/`; these are local diagnostics, not SRAM qualification.

The complete-macro context screen rejects that candidate for integration:
4859.4944 µm² of its new Metal4 overlaps existing wiring in
`RM_IHPSG13_2P_BITKIT_16x2_POWER_ramblk`. A read-only screen of hypothetical
clearance cuts would remove 18901.6064 µm² of the original Metal4 and intersect
6144 existing Via3 contacts. No such cuts are applied. Original PDK and power
planes remain unchanged. See `sram-upper-metal-context/checks.json` and
`sram-upper-metal-power-clearance-screen.json`. A repair must preserve power
distribution and pass complete SRAM DRC/LVS and timing before integration.

## October 30 overnight physical simulation watch

At 15:08 UTC the physical transient advances to 1.46267 µs of 1.544 µs.
Both the transmitter's 1593 and the queued receiver trial's 194 input hashes
still match (`active-job-health-20261030T1508.json`). Neither job has produced
a new completed electrical or routing qualification.

The verified SRAM pin-label conversion is now part of `flow_control_stream.py`.
It copies the four known legacy cells' 12 pin texts from Metal1 datatype 2 to
datatype 25 at their original locations. All interfaces and conductor overlaps
are checked before mutation; conflicting labels are rejected, and repeated
application adds nothing. The registered stream-guard CTest passes all ten
cases, including exact conductor/hierarchy preservation and malformed-label
rejection. A new registered `analog.1g.ihp_flow_control.leaf_lvs` CTest also
passes strict transistor and port comparison for all four cells through the
maintained adapter. Its reproducible runner is `tests/flow_control_leaf_lvs.py`;
the current receipt is under
`build/regeneration-audit/1G/IHPSG13G2/tests/results/flow_control_leaf_lvs/`.
It limits each extraction to 1 GiB and terminates the child process group on
timeout. Shared frozen PDK/checker inputs and existing GDS artifacts are unchanged.
The next complete stream still requires full-block DRC/LVS, including resolution
of the independent SRAM resistor-width discrepancy.

At 08:05 UTC the unchanged trim-27 physical transient reaches 0.930324 µs of
its required 1.544 µs. It remains running after approximately ten hours of
execution, within the original 24-hour recovery budget. All 1593 recorded
input hashes still match; its log contains no error markers. The 512 MiB trace
reservation remains allocated. The deck exports its waveform after transient
completion, so no intermediate electrical result or comparison pass is claimed.
The health receipt is `active-transmitter-health-20261030T0805.json` under
`build/clock-integration/extracted-repair/`.

The receiver spreading supervisor remains queued for the shared memory slot;
all 194 queued input hashes match. Sharing identical closed layout outputs
recovers 44 MiB while preserving every file path and byte. The audit is
`closed-output-storage-recovery-20261030.json`; active directories are excluded.
The complete SRAM LVS discrepancy, receiver routing, electrical comparisons
and full-chip qualification remain open.

## October 29 fill timeout and candidate geometry retry

Further isolated SRAM diagnosis verifies that all four unmatched cell types
exist in the original GDS. Their pin texts use datatype 2, while the LVS deck
reads metal labels from datatype 25. Private fixtures copy those existing texts
at their original coordinates onto the label layer, checking conductor overlap
and exact preservation of polygon geometry. All four then pass the production
persisted-report checker with strict port correspondence and transistor
dimensions: CINVX2, CINVX8, FILLCAP4 and FILLCAP8. A new CTest also passes three
negative cases: missing labels, swapped input/output labels and wrong transistor
width. The tool returns zero for these failed comparisons; the independent
report checker rejects each one. Receipts are `sram-leaf-pin-diagnostic-v2/`
and `sram-leaf-pin-guards-v2/` under the recovery directory. These isolated
matches do not establish that adding labels will repair full SRAM hierarchy
alignment or that the integrated SRAM passes LVS.

The original bit-cell extraction exposes a separate dimensional discrepancy:
four Metal2 and two Metal3 resistors have L=0.60 µm, W=0.20 µm, while the
original CDL specifies W=0.26 µm. Direct intersection of each metal polygon
with its resistor marker confirms the extracted dimensions. MOS width sums
at each channel length agree for the bit cell and all four isolated cells;
this dimension audit does not qualify connectivity. The retained evidence is
`sram-leaf-dimension-audit.json`. No resistor dimensions or acceptance limits
have been changed to force a match.

A bounded full-SRAM netlist diagnostic also finds 21 unused output pins per
macro. All are unconsumed in each of the eight routed instances. Its first
standalone comparison therefore has a different boundary from the integrated
extraction. A second attempt preserves their circuitry internally and uses
the actual parent supply ties, but exceeds its 1 GiB allocation while flattening.
Both failures and the parent-net incidence audit are retained in
`sram-parent-netlist-diagnostic/`; no full-SRAM comparison pass is claimed.

The subsequent LVS run finishes with unmatched SRAM circuits. Its zero tool
exit code is correctly rejected by the persisted-report checker. The report
contains 80 matched circuit pairs, two mismatched SRAM subcells, four schematic
cell types without layout counterparts, and skipped SRAM/top comparisons.
`flow-lvs-mismatch-summary.json` retains a streaming diagnosis and report hash;
the full failed report is preserved. A bounded 2 GiB attempt to load the entire
report exceeded its allocation, so diagnosis reads the saved text instead.

Two isolated diagnostic CTests now pass (seven cases total). For
`RSC_IHPSG13_CDLYX1_DUMMY`, the schematic uses `lvsres` while the extracted
device uses `res_metal1`, both with L=0.6 µm and W=0.26 µm. A private model-name
adapter matches the extracted subcell with both dimensions and A/Z connections
checked; wrong length and an open terminal fail. The resistor is retained by
the reader and simplifier; the initial removal hypothesis was not confirmed.
IHP documents related SRAM extraction issues in
[issue 239](https://github.com/IHP-GmbH/IHP-Open-PDK/issues/239).

For `RSC_IHPSG13_DFPQD_MSAFFX2P`, all 32 instances in the extracted SRAM parent
connect the two distinct child supply ports to the same physical parent net.
Flattening this subcell into that extracted context gives a transistor match;
an open supply or incorrect clock fails. No implicit supply connection is
introduced. These are small diagnostics in `sram-dummy-reader-diagnostic/`
and `sram-flop-context-diagnostic/` under the recovery directory. They do not
qualify the four unmatched cell types, complete SRAM, top-level ports or chip.
The PDK, production checker and shared frozen sources remain unchanged.

The receiver's five-iteration diagnostic completes with congestion: 18.40%
overall resource usage and 15585 reported 3D overflow. Its saved 2D map contains
5621 regions and 15545 aggregate overflow; these are separate router reports.
Two adjacent 200 µm squares, from (800,1400) to (1200,1600) µm, contain 6435
of that map overflow (41.4%). This identifies local placement/routing hotspots,
not a qualified route or evidence that acceptance limits should be relaxed.
The map summary is `receiver-congestion-locality.json`.

A read-only audit of the same checkpoint covers 149,479 placed instances.
The two hotspot bins have 87.2638% and 89.7333% cell occupancy, measured from
instance-box intersections, versus 32.6696% across the 2400 × 2400 µm die.
They contain 4132 buffer centers, mostly placement/wire-repair buffers.
This is distinct from routing-resource utilization. Evidence is retained in
`receiver-placement-density/checks.json`. A private incremental placement trial
at 45% target density is queued behind the transmitter, using the same die
and requiring byte-identical complete Verilog exports before and after movement.
It has a one-hour execution budget after memory admission and will record
new estimated timing, placement legality and five-iteration congestion maps.
Its receipt is `receiver-spread-diagnostic.json`; it is not routed timing or
physical qualification, and has not yet executed placement.

The transmitter physical recovery starts after these jobs release the slot.
It is running in `charge-trim27-physical-recovery-v2/run-qy1s4i4e/`, with its
original deck, 24-hour budget and 512 MiB trace reservation. Sharing three
byte-identical closed receiver inputs recovers 156 MiB while preserving paths
and hashes (`closed-receiver-v3-deduplication.json`).

The candidate filled PAUSE macro completes the full KLayout DRC run at
20:58 UTC: zero violations across 868 categories, all 37 main rule tables,
maximum rules, density and antenna checks. All nine density measurements
pass; Metal2 is 38.69% and TopMetal1 is 35.54%. The independent retained
receipt `flow-filled-drc-pass.json` hashes the actual filled GDS, complete
reports and logs under `build/clock-integration/extracted-repair/`.
LVS on that same layout subsequently fails as recorded above. This is a
DRC-clean SRAM/control macro, not a complete ASIC.

The PAUSE macro fill attempt reaches its 3600-second timeout during Active/Poly
processing and produces no qualified filled GDS. The failure is retained in
`flow-clock-leaf-geometry-retry/checks.json` under the recovery directory.
Receiver routing subsequently starts from its verified resized checkpoint in
`receiver-physical-wait-recovery-v3/PCSReceiver-r7f_ynnz/`. It fails after
6479.944 seconds with `GRT-0116`: global routing still has congestion. This is
a routability failure, not a timeout or memory kill. The checkpoint and failure
are retained; no routed receiver qualification is claimed.

Private fill candidates remove two unused PDK Boolean differences and preserve
hierarchy during metal-mask operations. The selected candidate additionally
tiles Active/Poly operations at 100 µm with a 5 µm border and one worker;
the fill patterns, exclusion formulas and spacing settings are unchanged.
KLayout documents the border requirements for tiled sizing and Boolean work
in its [DRC runset reference](https://www.klayout.de/doc/manual/drc_runsets.html).
Exact comparisons against the existing implementation pass all nine fill layers
on a fixture containing real standard cells and 64 SRAM bit-cell tiles,
including shapes crossing tile boundaries. All 297 fill/hierarchy/shape checks
pass under CTest, with baseline, candidate and GDS artifact hashes retained.

These fixture results do not establish full-block scaling or geometry closure.
A complete standalone SRAM fills in 12.39 seconds with the hierarchical-metal
candidate under a 768 MiB diagnostic bound. The full routed block exceeds that
small bound during mask sizing; the tiled probe reports worker allocation
errors and reaches its 60-second diagnostic limit. Both failed probes remain
recorded. Full verification starts under the existing 4 GiB bound and
unchanged 3600-second fill/7200-second DRC/LVS limits, using private source
snapshots in `flow-fill-v3-sources/`. A five-case CTest verifies that reported
worker errors and missing, duplicate or reordered fill completion markers are
rejected even if the tool returns zero.

The full-block candidate completes Active/Poly, Metal and TopMetal fill and
advances to the complete KLayout DRC deck at 19:27 UTC. The later DRC pass and
LVS failure are recorded above; fill alone does not qualify its GDS. The private candidate
does not change the maintained fill implementation or any frozen job inputs.
A separate full-block preservation check passes 9280 checks under a 512 MiB
bound: all 244 original cells retain their shapes and instance connections,
and the 14 added cells contain only fill geometry. The receipt
`flow-full-fill-preservation.json` hashes both actual GDS files and its checker.
This preserves the routed design but does not substitute for DRC or LVS.

The completed receiver diagnostic uses the same verified placement, routing
layers, capacity adjustment and clock-rule policy, with congestion maps after
each of five iterations. Its 1800-second execution budget is for diagnosis;
it neither allows congestion nor runs physical qualification. The script and
receipt are `diagnose-receiver-congestion.py` and
`receiver-congestion-diagnostic.json` in the recovery directory. The transmitter
is now running with its unchanged 24-hour physical simulation budget.
Full-chip qualification remains open.

## October 29 trim-27 schematic pass and physical runtime recovery

The complete trim-27 charge-storage schematic passes all original checks over
1.544 µs and 216,866 retained solver records. Maximum plateau error is
6.56753 mV against 20 mV; shared-bias ripple is 4.00713 mV. The complete
119,691,593-byte compressed waveform and its original-byte SHA-256 are retained
in `charge-trim27-qualification/full/results/checks.json` under
`build/clock-integration/extracted-repair/`.

The component-RC physical attempt advances to 0.181108 µs after about two hours.
Its observed rate cannot finish the unchanged 1.544 µs window within the
configured six-hour execution budget. It is deliberately stopped before that
timeout; the original receipt records failure 143 and retains the schematic
pass. No completed physical waveform or comparison is claimed.

`resume-charge-trim27-physical.py` verifies the original inputs, physical deck
and passing schematic artifacts before resuming only the physical experiment
in a fresh directory. Its 24-hour execution budget begins after memory admission;
the circuit, timestep, full window, 3 GiB address-space bound, 512 MiB trace
reservation, electrical checks and 10 mV comparison limit are unchanged.
Six recovery guards pass under CTest, including rejection of incomplete
schematic evidence, changed inputs, a live predecessor, replacement of a
completed physical result, wrong trim and unsupervised execution.
The supervisor receipt is `charge-trim27-physical-recovery.json`.

The released memory slot initially starts PAUSE macro geometry verification:
stream import passes, but fill later times out as recorded above. Receiver
routing is now active, followed by candidate geometry and the longer physical run.
An additional 590 MiB is recovered by sharing byte-identical closed artifacts,
with paths and hashes retained (`small-closed-storage-recovery.json`). None of
these results closes automatic amplitude calibration or full-chip qualification.

## October 29 completed transmitter and corner checks

The complete connected transmitter schematic run finishes all 1.544 µs and
retains 216,953 solver records, but fails the unchanged 20 mV weighted-level
limit. Maximum plateau error is 93.4311 mV: the +1 V target reaches 1.093431 V
at trim code 44. Supply, bounded-node, current, steering/core gate, translated
trim and bias-ripple checks pass; shared-bias ripple is 7.27960 mV. The physical
flow was not started after this independent schematic failure. The separate
charge-storage candidate also completes its full schematic window and fails
only weighted levels: maximum error is 91.3352 mV, with 4.11467 mV bias ripple.
Its retained-state diagnosis is `completed-charge-transmitter-diagnosis.json`.
The failed waveforms and all original
limits are preserved under `transmit-connected-new-replica/results/` in
`build/clock-integration/extracted-repair/`; `completed-transmitter-diagnosis.json`
records every evaluated aperture without rerunning the transient.

Static trim diagnosis on the same complete schematic gives 0.942659, 0.983701,
1.023696 and 1.083990 V at codes 16, 24, 32 and 44 for the +1 V state. It
retains simulator convergence/model warnings and does not establish cold-start,
dynamic, physical or automatic-calibration qualification. The receipt is
`connected-tx-static-trim-v2/checks.json` in the same recovery directory.
Code 27 subsequently gives 0.999013 V in this schematic DC fixture. The
component-RC circuit fails operating-point convergence both without a hint and
with schematic voltages supplied only as numerical initial guesses. Neither
attempt yields a physical amplitude result. A preset is not accepted from this
diagnosis; cold-start and complete dynamic checks remain necessary.
The preset-code-27 candidate passes the original startup-numerics CTest:
maximum nominal/tight difference is 0.447263 mV against 1 mV. Its complete
schematic also passes; the physical runtime recovery described above remains
pending, with unchanged electrical limits.
The supervisor acquires the memory slot before starting CTest timeouts. This
is process characterization, not an automatic amplitude-calibration controller.

The previous receiver routing process disappeared while its log still showed
waiting for the shared memory slot. Its original receipt remains retained;
the cause is unknown. A new supervisor verifies all 188 input hashes and both
saved placement files before resuming in a fresh directory. Its receipt is
`receiver-wait-recovery-v2.json`; that attempt stops before routing because
the required 1536 MiB of free output space is unavailable after admission.
`receiver-wait-recovery-v3.json` queues a fresh attempt after the PAUSE geometry
retry, retaining the same input verification and disk guard.
Two completed clock archives were verified to decompress to the same original
SHA-256 and now share one compressed file, recovering 704 MiB without removing
waveform samples. The current archive hash is unchanged; the historical gzip
encoding change is recorded in `identical-clock-waveform-deduplication.json`.
Sharing the interrupted receiver's byte-identical closed checkpoint recovers
another 103 MiB; its bytes and all recorded hashes remain unchanged.
An additional 412 MiB is recovered by sharing byte-identical closed mapped
inputs, preserving all paths and SHA-256 values. Removing regenerable
precompiled headers from completed PAUSE replays recovers 281 MiB; source
headers, replay executables and results are retained. The audits are
`closed-mapped-storage-recovery.json` and `closed-replay-header-retirement.json`.
Sharing additional byte-identical closed waveforms and routing checkpoints
recovers 682 MiB, without changing their bytes or paths. Active process
directories and open files are excluded; the audit is
`closed-artifact-storage-recovery.json`. Free space is about 2.16 GiB while
the transmitter still holds its reserved output allocation.

The sampler parent-conversion CTest now passes at TT/27 °C, gain 4, one slice.
Both the complete schematic and component-RC parent produce code 112, each
within one code of the independent target; their code difference is zero.
Maximum capture-target difference is 0.293275 mV against 5 mV. The actual
preamplifier, DAC, comparator and C++RTL feedback controller are retained.
This characterization does not waive the failed standalone sampler comparison
and does not qualify the full PHY. Its receipt is
`receive-sampler-conversion/results/checks.json` in the recovery directory.
The PAUSE clock ECO completes detailed routing and extraction. Setup slack is
+95.416 ps and hold slack is +113.400 ps. Functional replay passes 240 intact
frames, six pauses, six resumes and two isolation cases over 630,084 cycles.
All checked crossings pass; slow-corner PAUSE payload clock spread is 459 ps
against 500 ps. These checks exclude post-fill coupling and full-chip timing.

Geometry verification initially stops during stream import: eight SRAM helper
cells are empty. Direct inspection finds those same cells explicitly defined
as empty in the original PDK GDS, with no unresolved references. The importer
now derives permitted empty descendants from that physical library, while
rejecting undefined references, missing real masters and an empty macro root.
Four regression cases pass under CTest. Streaming the actual routed block
also passes, retaining all eight SRAM instances and 244 physical hierarchy
cells. This is only a stream check: the first full-fill attempt times out and
candidate fill, DRC and LVS remain pending under `flow-clock-leaf-geometry-v3.json`.
The original failed receipts are retained.

## October 29 complete nominal extracted processing-clock result

The reserved-output rerun passes the complete 64 µs extracted oscillator/gate
simulation with the live calibration controller. All 13,687,377 samples are
retained and checked. Measured frequency is 133.366812 MHz; 4,256 complete
clock pulses reach the 200 fF load. Minimum high pulse is 3.660195 ns and
maximum edge delay is 317.967 ps. The controller remains qualified for the
last 512 reference edges, with final trim 52,096. The verified compressed
waveform is 1,195,466,415 bytes; the earlier incomplete output remains a failure.
The receipt is
`build/clock-integration/extracted-repair/processing-clock-reserved-rerun/results/checks.json`.
This closes the nominal connected extracted oscillator/gate/controller fixture.
It excludes physical controller drivers, inter-macro routes, the receiver clock
tree, field/contact calibration, PVT, mismatch, noise and full-chip qualification.
The released memory slot admitted the pending flow-control crossing checks.
They completed and failed the slow-corner PAUSE command payload's 570 ps
source-clock spread against 500 ps. The generic error text names a FIFO Gray
bus, but enumeration shows the Gray buses themselves pass. Data-path delay,
previous routed setup (+89.458 ps) and hold (+112.074 ps) remain separate checks.
Two retained-RC buffer-size diagnostics preserve setup/hold/electrical limits
but do not close the payload spread. Inserting a buffer on the single early
PAUSE payload register's clock predicts a 465 ps slow-corner spread, within
the unchanged 500 ps limit; all other crossing checks also pass this prediction.
It retains the previous extracted RC on existing wires and has an unrouted new
branch, so this is not physical qualification. Fresh routing, extraction,
functional replay, crossing checks and full geometry verification are queued
under `build/clock-integration/extracted-repair/flow-clock-leaf-eco.json`.
The connected transmitter simulation subsequently finishes with the failure
recorded above.

## October 28 receiver storage and sampler follow-up

The storage-access receiver GDS published in `results/` has completed its
first four switching symbols after the original 16 µs cold startup. This
32 ns screen preserves the original 2 ps observation grid and thresholds.
It improves the previous current-routing layout, but does **not** pass:

| Layout screen | Settled error (≤15 mV) | Peak error (≤100 mV) | Divider error (≤2 mV) | Output mismatch (≤5 mV) |
| --- | ---: | ---: | ---: | ---: |
| Published storage access | 8.5213 | 83.8594 | 2.03446 | 10.4143 |
| Wider storage rails and via arrays | 8.7787 | 85.5619 | 2.00150 | 10.5166 |
| Direct bias returns and extended ground rail | 8.9086 | 85.1638 | 2.00362 | 10.3091 |
| Parallel terminal-access vias | 8.5729 | 84.4374 | 1.58575 | 9.43709 |
| Wider VDD rail and direct resistor feeds | 8.5816 | 84.4960 | 1.58440 | 9.18116 |
| Direct divider feeds and wider ground rail | 8.5892 | 84.2119 | 1.46272 | 9.07028 |
| Wider sink/output conductors and supply rails | 9.5136 | 84.0022 | 1.47342 | 7.44591 |

The same previous-layout prefix had 19.7454 mV settled, 146.9178 mV peak and
8.07956 mV divider errors. The first three screens fail the unchanged divider and output-comparison
limits. Parallel terminal-access vias pass all three independent short-screen
checks but still fail the output comparison. Their `diagnostic_complete` status records
completed measurement; `short_screen_pass` is false. None is a full 256 ns,
PVT, integrated receive-chain or chip qualification.

Isolated extracted-wire checks inject 1 mA at each storage terminal and verify
KCL residuals below 1 nA. Output plate access falls from 84.80–85.29 Ω to
0.43–0.79 Ω with the published connections; output returns fall from
45.65–48.12 Ω to 1.17–2.29 Ω. Wider connections reduce them further to
0.19–0.51 Ω and 0.67–1.75 Ω, respectively. This does not resolve the waveform
mismatch. The unchanged BIAS_N, BIAS_P and TAIL_BIAS storage returns measure
48.04–48.31 Ω, 51.64–51.95 Ω and 54.71–54.98 Ω. A separate candidate adds
direct returns to those three 10 pF banks and reinforces inter-column links.
It retains every device, capacitance, stimulus and acceptance limit. Its first
version passes local geometry and DC checks but leaves the tail-bias return at
54.71–54.81 Ω: that bank lies left of the existing upper ground rail. The
incomplete short transient was stopped when this wire audit exposed the missing
rail extension. A revised physical extension passes local geometry, LVS and DC;
its BIAS_N, BIAS_P and TAIL_BIAS returns are 1.25–1.35 Ω, 1.73–1.87 Ω and
0.68–0.78 Ω. Its completed short transient still fails the divider and
output-comparison limits listed above; neither candidate is adopted.

An isolated terminal-access candidate adds parallel via cuts at existing MOS
and resistor connections. Its local DRC, LVS, missing-device and conserved-RC
checks pass. DC and contact-current screening also pass. The four-symbol
transient passes the independent checks but fails the original output
comparison, as listed above. It retains the same devices and limits. Wire-only
measurements find that the added vias reduce divider access resistance from
60.82 Ω to 32.41 Ω on VDD and from 59.39 Ω to 30.98 Ω on VSS.

A structural audit matches all 570 MOS fingers across 11 electrical groups,
including topology and total width. The nominal schematic nevertheless uses
single-finger junction defaults: 1,938 µm² total junction area and 12,175.2 µm
perimeter. The extracted shared-diffusion layout has 1,102.5 µm² and 6,271 µm.
An isolated ideal-wire diagnostic projects only those four explicit junction
parameters onto the existing schematic fingers, preserving the original
reference and stimulus. The completed diagnostic passes the same independent short-screen checks.
It differs from the original schematic by 4.6577 mV at OUT, but the layout
differs from it by 13.8113 mV at OUT and 57.3007 mV at DRIVE. Matching
junction geometry therefore does not resolve the wiring failure. A separate
GDS polygon audit checks all 570 fingers and finds zero required junction
corrections; extracted area and perimeter equal the actual diffusion geometry.
These diagnostics cannot qualify the physical circuit or replace the original
comparison result. Receipts are `receive-junction-projection-first-four/checks.json`,
`receiver-junction-diagnostic-comparison.json` and `receiver-junction-gds-audit.json`.

The retained high-load DC operating point shows about 32 mV of VDD-route loss
at the bias dividers and 53 mV at the pull-up and tail-bias resistor terminals.
A new isolated candidate widens the VDD rail and adds direct upper-metal feeds
to those two high-current resistors. Local geometry, LVS, missing-device and
DC/contact-current checks pass at the same 596,336.4941 µm² footprint. Pull-up
VDD drop falls from 53.2791 to 18.6466 mV, tail-bias feed drop from 52.7179
to 11.7024 mV, and divider-feed drops from about 32 to 18–19 mV. Its complete
short transient passes the independent limits but still fails the unchanged
output-comparison limit at 9.1812 mV.

The completed ideal-wire diagnostic retains actual devices and all inter-net
capacitance. It differs from the original schematic by 2.2560 mV at OUT; the
actual layout differs from that ideal-wire case by 7.7504 mV. That supports
targeting route resistance. These are counterfactual comparisons, not additive
error terms or physical qualification. See `receiver-wire-diagnostic-comparison.json`
and `receiver-vdd-plane-dc-wire-drops.json`.

A companion layout adds direct feeds to both bias dividers and a wider shared
ground return. Its first 32 µm ground rail fails `Slt.c.TM2` (30 µm maximum
unslotted width); that failed geometry is retained. The 28 µm revision passes
local geometry, LVS, DC/contact-current screening and the four ground-access
guards. Area is 626,016.9741 µm², about 5% above the VDD-only candidate.
Bias-divider feed drops are 6.45–7.12 mV on VDD and 7.83–8.99 mV on VSS.
Its completed short transient passes the independent voltage checks but still
fails waveform comparison at 9.0703 mV against 5 mV. The maximum occurs
26.746 ns into switching; all four settled apertures differ by only
1.4827–4.1869 mV. The failure is in the transient response, not the settled
output comparison.

AC diagnostics at the retained low/high DC loads preserve all physical devices
and compare output impedance from 100 kHz to 10 GHz. At 125.89 MHz, the
high-load schematic is 9.6586 Ω; VDD-only and wider-ground layouts are
11.0958 and 10.9385 Ω. Idealizing only sink gates changes impedance by less
than 0.01 Ω. Idealizing sink drain/source access reduces it to 9.9982 Ω;
these counterfactuals identify a routing target, not a physical result.
A same-footprint candidate with wider sink collectors and output connections
passes local geometry, LVS and DC/contact-current checks and measures
10.7334 Ω. A combined wider-output/wider-ground candidate passes geometry, DC/current
and ground-access checks. It measures 10.5794 Ω at high load and keeps the
626,016.9741 µm² wider-ground footprint. Its completed four-symbol transient
passes the independent voltage checks but still fails output comparison at
7.4459 mV; coupled external observations also differ by 5.7909 mV against
5 mV. The completed counterfactual transient bypassing only the thirty sink fingers’
drain/source access reduces output mismatch to 5.6968 mV, still above 5 mV.
It passes the individual voltage checks, but establishes that sink access
alone does not close the comparison. Amplifier-access diagnosis follows.
The combined amplifier-drain/sink-contact candidate subsequently passes local
DRC/LVS, extraction, DC/contact-current and ground-access checks at the same
footprint. Its first AC attempt exhausts a 512 MiB cap; the identical circuit
completes within a separate 1 GiB diagnostic budget. High-load impedance is
10.6597 Ω versus 10.5794 Ω before the contact change, farther from the
schematic. Its long transient remains unstarted pending route/capacitance
analysis; geometry/DC success is not treated as dynamic qualification.
An October 29 diagnostic moves the external ground connection to an existing
node near the sink while retaining every device and RC value. It predicts
10.4944 Ω at high load, only a small improvement; no physical pin relocation
is adopted. Separate selected-net equipotential diagnostics retain all other
wiring. Over the 10–500 MHz logarithmic sample grid, complex impedance
disagreement with the schematic is 0.8603 Ω RMS for the physical baseline,
0.7425 Ω with internal amplifier wires idealized, and 0.7462 Ω with VDD wires
idealized. Ideal ground wiring improves the single 125.89 MHz point but worsens
this wider-band measure to 1.0420 Ω. DRIVE and bias wiring have little effect.
These results guide further routing work; the frequency band and RMS measure
are diagnostic, not acceptance limits. Receipts are
`receive-local-ground-impedance/checks.json`,
`receive-selected-wire-impedance/checks.json` and
`receiver-selected-wire-summary.json` under the same local recovery directory.
Counterfactuals cannot qualify a layout or advance to the full physical run. These small-signal screens cannot replace
the unchanged full transient comparison.

The registered `storage_ground_access` CTest now requires a continuous
TopMetal2 path from each declared storage return to the external VSS pin.
Its four cases pass against the maintained layout and the revised five-bank
candidate. It also rejects the actual previous five-bank layout even though
LVS passed through the retained lower-metal wiring. The test changes no
circuit or electrical acceptance threshold.

The sampler's inverse-clock shield has also been moved clear of its Metal5
VDD rail. This isolated candidate passes the geometry CTest (58.01 s), including
LVS and missing-device checks. Its electrical CTest fails (77.54 s): independent
acquisition/hold checks pass in both flows at all five levels, but mismatches
are 6.4518, 6.6496, 6.8397, 7.3357 and 7.7377 mV against 5 mV. It is not adopted.

Local receipts are under `build/clock-integration/extracted-repair/`:
`receive-storage-first-four/checks.json`,
`receive-storage-wide-first-four/checks.json`, `storage-wire-networks.json`,
`storage-wide-wire-networks.json`, `storage-bias-wire-networks.json`,
`sampler-separated-clock-shields-candidate/results/checks.json` and
`sampler-separated-clock-shields-extracted/results/checks.json`.
The corrected MOS-storage replica now passes the complete 2.128 µs loaded
22-translator-bank run. Both schematic and extracted flows pass their independent
checks; all 22 comparisons pass, with maximum edge difference 89.4942 ps and
waveform RMS difference 83.8818 mV against the unchanged 100 ps/150 mV limits.
Its exact schematic trajectory was independently rechecked before reuse. This
closes the nominal loaded-bank check after the GDS junction correction; full
TX, inter-macro routing, corners and chip qualification remain separate. The
receipt is `loaded-junction-replica/sequence.json`. The complete processing-clock rerun now passes as recorded above; its earlier
incomplete run remains a failure.
The slow/hot (125 °C) and fast/cold (−40 °C) runs have completed. Both circuits
pass their independent checks at both corners. Fast/cold passes all 22
comparisons, with 81.5936 ps maximum edge departure and 81.5102 mV RMS waveform
departure. Slow/hot fails all 22 edge comparisons at 101.9411 ps against 100 ps;
its 79.8249 mV RMS departure passes the 150 mV limit. The corner sequence remains
failed, with the original traces retained. `slow-hot-bank-diagnosis.json` records
the offending edges without weakening the acceptance limit.

## Other qualification history

The composed native-cell PLL passes schematic checks but its first unfilled
PEX run fails cycle variation, output delivery and tuning ripple. Frequency,
divided phase wander, rails and supplies pass. The candidate's queued fill
was stopped. A revised placement keeps tuning and mirror-bias storage with
the oscillator. Its complete extracted transient reduces ripple to 7.255 mV,
but still fails the 2 mV ripple, 80 ps cycle-variation (90.507 ps measured) and
1 ns delivery limits (1.435–1.473 ns measured). An archived-network diagnostic
separates wire-resistance effects from capacitive coupling. No complete clock-macro result is claimed. A separate power-routing prototype
passes local DRC, exact 259-device LVS and open-reference rejection without a
footprint increase. Its extracted tuning-source return resistances fall from
62–65 Ω to 33–36 Ω. Its complete transient passes cycle variation at 76.73 ps,
but delivery (1.138–1.162 ns) and control ripple (32.75 mV) still fail.
The source-contact candidate further reduces cycle variation to 52.10 ps,
but delivery (1.067–1.086 ns) and control ripple (31.69 mV) still fail.
The direct capacitor-return candidate still fails delivery (1.067–1.086 ns)
and control ripple (31.17 mV), while cycle variation passes at 51.51 ps.
The distributed-contact candidate passes cycle variation at 49.62 ps, but
delivery (1.016–1.034 ns) and control ripple (31.90 mV) still fail.
The upper-metal supply-pin candidate places both buffers in one row, passes local geometry and six
placement guards, and occupies 6% less area. Its complete 2 µs extracted run
passes delivery (0.972–0.976 ns) and cycle variation (29.24 ps), but fails
frequency (249.872 MHz), divided phase wander (209.22 ps) and control ripple
(7.99 mV). Acquisition oscillation contributes to the final 400 ns measurement;
the last 100 ns still has about 4.22 mV control ripple against the unchanged
2 mV limit. Fill remains stopped pending electrical repair.
An unchanged-circuit probe run reproduces those results and finds only
0.634 mV ripple on the tuning capacitor's ground terminal, versus 65.09 mV
on the main filter output and 64.88 mV on its return. A real-R3 sizing trial
with 3 kΩ smoothing and 4 kΩ damping passes frequency (249.999 MHz),
phase wander (16.81 ps), cycle variation and delivery, but fails tuning
ripple (4.64 mV). Its final 100 ns still has 3.70 mV ripple. A second
trial uses 8 kΩ smoothing and 6 kΩ damping. Both retain the previous
external wiring RC and require matching C++ export and fresh physical
layout/extraction before qualification.
The 8 kΩ/6 kΩ retained-wire diagnostic also fails: divided phase wander is
244.481 ps and tuning ripple is 11.191 mV. Frequency, cycle variation and
delivery pass. The separate fresh C++/layout/extracted candidate also fails
its complete 2 µs run: 293.471 ps divided phase wander and 12.078 mV tuning
ripple. Frequency, cycle variation (65.565 ps) and delivery (0.969–0.978 ns)
pass. Its conditional fill sequence stopped on that failure. A
3 kΩ/8 kΩ diagnostic also fails: phase wander is 83.36 ps and tuning ripple
8.92 mV; the other checks pass. A second diagnostic uses 1.5 kΩ smoothing,
6 pF control storage and 4 kΩ damping, preserving approximately the 9 ns
smoothing time constant of the 3 kΩ/3 pF trial while reducing control-node
impedance. It fails frequency, phase wander (243.82 ps) and tuning ripple
(11.73 mV). Both use PDK-sized devices with the previous external RC;
neither qualifies a GDS layout.
Slow/hot schematic screening passes all eight electrical groups at nominal supplies: 250.001 MHz output, 0.341 mV tuning ripple and 0.882 ns maximum delivery. Fast/cold schematic screening also passes all eight groups over the complete 2 µs: 250.002 MHz output, 0.362 mV tuning ripple and 0.692 ns maximum delivery, with unchanged inputs. These results do not
establish a passing clock macro or filled layout.

The shared TX translation replica now has a C++-derived physical implementation
with its actual 50 pF storage. Local DRC/LVS, missing-device rejection, complete
startup and independent/comparison checks pass at TT/27 °C, SS/125 °C and
FF/−40 °C. The macro occupies 75,638.83 µm², contains 18 LVS devices including
the explicit taps, and reaches 1.768–1.855 V with at most 1.33% settled error.
The wider-metal variant below passes the nominal loaded bank; fill,
passive/supply corners and full TX qualification remain open.

The latest separate TX translator passes both SS/125 °C loads in the local
extracted diagnostic: 92.98 ps at 0.2 pF and 97.68 ps at 2 pF, below the
unchanged 100 ps comparison budget. Short upper connections for the inverter
output and pull-down nodes create the improvement. Both independent waveforms
also pass their absolute limits. All six maintained guard, geometry and TT/SS/FF tests pass (874.28 s).
A separate TT startup diagnostic with one translator and the actual replica
also passes: 94.32 ps maximum edge difference and 86.96 mV waveform RMS.
The first maintained connected matrix passes TT/FF but fails SS at 106.48 ps.
Direct physical connections to the replica storage then pass the SS diagnostic
at 96.53 ps/74.54 mV RMS without increasing area. Both maintained replica tests
pass; all three maintained connected corners and their guards also pass
(1,278.57 s), with maximum edge differences 83.52/96.53/75.13 ps at TT/SS/FF. The C++-derived 22-translator bank
has passing topology/observation guards. Its complete nominal schematic startup
and simultaneous-switching stress pass for all 22 translators, with shared bias
1.773–1.854 V. All 22 extracted gates pass their absolute limits, but the
121.71 ps edge difference exceeds the unchanged 100 ps comparison limit.
The extracted shared bias spans 1.545–1.924 V. A wider-metal candidate with
upper-layer BIAS/VSS access passes local DRC, strict LVS, missing-device
rejection and three unloaded startup corners. Its wire-only storage access
falls from 7.66–7.98 Ω to 0.22–0.54 Ω on BIAS and from 3.12–4.27 Ω to
0.95–2.10 Ω on VSS. The complete 2.128 µs nominal loaded-bank test now passes
all 22 independent gate checks and the comparison: worst edge difference
88.964 ps and waveform RMS 83.013 mV, under the unchanged 100 ps/150 mV
limits. Shared bias remains within 1.723–1.832 V. Supply/passive corners
and complete TX remain open.

The first connected full-TX schematic simulation stops at 39.357 ns with
an R3 pullup convergence error, before controller switching begins. Its
incomplete output is a failed run. The maintained 64 ns startup regression now
passes with 1 pA and 0.1 pA absolute current tolerances. All 77 voltage
observations differ by at most 0.464 mV, below the 1 mV numerical budget.
Twelve guards pass. With the loaded replica bank now passing, both complete
transmitter circuits have been emitted and the schematic run is queued for
the shared memory slot. Full-window electrical acceptance and the extracted
connected flow remain required.

The shared [receive reference layout](receive-common-layout.md) passes its
maintained local DRC/LVS, missing-sink rejection and conserved-RC checks
(566,644.91 µm² before fill). Nine topology/binding guards and eight integration
guards pass. Its eight-lane nominal schematic passes all twelve independent
check groups, with 7.09 mV settled reference error and 78.00 mV transient error
under 6.81–16.54 mA sink loading. The baseline extracted layout fails with
35.27 mV settled error, 158.58 mV transient error and 31.90 mV divider error.
The revised current-route simulation reached the end of its transient, but
waveform export exceeded its 64 MiB reservation. Its incomplete archive cannot
qualify the circuit. The saved prefix already shows 55.77 mV settled reference
error and 23.82 mV divider error, above the unchanged 15 mV and 2 mV limits.
Circuit diagnosis is required before another full run. Future receiver captures
reserve 256 MiB: complete baseline archives occupy about 118 MiB each. The
slower duplicate SPARSE run was intentionally retired; its exit 143 is not an
electrical verdict. A subsequent sense-routing candidate passes local DRC,
exact LVS, missing-sink rejection and conserved extraction. Separate divider
and feedback connections reduce its DC reference error to 4.80 mV, divider
error to 0.78 mV and schematic/output difference to 2.39 mV. Seven regression
checks pass, including geometry equivalence to that candidate. Its maintained
geometry-regeneration CTest also passes. A subsequent pinned-PDK current check
finds only 5.2/10 mA drain/source Via3 capacity against at least 16.47 mA required.
The unstarted transient was retired. Distributed upper-metal sink access now
passes local DRC/LVS at the same 593,507.56 µm² footprint. Eight Via3/Via4
cuts per stripe give 53.2/51.6 mA aggregate drain/source Via3 capacity.
Both DC load extremes pass the original voltage/comparison screens. The
unreduced high-load DC audit finds a maximum 254.96 µA among 4,112 mapped
Via1–4 contacts, below 400 µA; the 127 unmapped 9 Ω edges also remain below
that limit. These checks do not qualify wire density, transient currents or
PVT reliability. Four maintained routing tests pass, including geometry
equivalence and invalid-input rejection. The complete original receiver
transient now fails: 19.934 mV settled reference error, 147.117 mV peak error
and 8.291 mV divider error, against unchanged 15/100/2 mV limits. Input
RC-equation and source checks pass. Internal-node probes are investigating
the switching failure; no receive-macro pass is claimed. Post-processing
initially exhausted memory by duplicating the trace. Streaming validation
recovered the complete archived waveform without another simulation or any
change to electrical limits; four validator regression cases pass.
The maintained DC/contact-current screen also passes on the fresh regenerated
extraction (fresh 28.14-second CTest after the validation change), including both load extremes, unreduced-model
agreement and coordinate-file provenance. Six stimulus/output guards pass.
The fresh resistor mesh has 125 unassigned 9 Ω edges, all below 400 µA;
its contact maximum remains 254.961 µA. This remains nominal DC evidence.
Completed nonloading probes exactly reproduce the failed waveform prefix and
show up to 77.821 mV between output storage plates and OUT, with target-capacitor
returns 36.205–47.350 mV above ground. An optional direct storage-access overlay
now passes local DRC/LVS/extraction, the same DC/contact screen and four routing
guards. Its four-symbol diagnostic is running with the original cold startup;
neither that short diagnostic nor DC success qualifies the full receiver.

The [physical receive sampler](receive-sampler-layout.md) has passing local
DRC, strict 21-device LVS and missing-hold/dummy rejection. Its fourth layout
connects the downstream load at the hold plate and occupies 15,243.42 µm².
Both schematic and extracted circuits pass independent acquisition/hold
checks at five input levels, but closing-transient comparison still fails:
28.042 mV versus the unchanged 5 mV budget. Ten new integration guards pass
against actual 1-, 4- and 13-slice exports. Complete ADC comparison decks
retain the preamplifier, DAC, comparator and C++RTL controller; their
simulations remain pending. No qualified sampler or ADC macro is claimed.
An optional GDS-derived junction correction now restores omitted dummy-device
diffusion area; six guards pass for both 25- and 34-finger layouts. Corrected
five-level simulations still fail comparison at 28.018 mV for the baseline
and 19.681 mV for the stronger-driver/source-contact candidate. Raw extraction
and the queued parent inputs remain preserved; generic extraction is unchanged.
Isolated C++ sizing and upper-clock-routing trials reduce the best complete
five-level mismatch to 5.519 mV at 15,005.95 µm². Every independent
acquisition/hold check passes, but the original 5 mV comparison still fails.
A separate one-volt late-step diagnostic finds at most 9.363 mV acquisition
error relative to the actual loaded input; one transition still differs by
7.538 mV between schematic and layout. Smaller complement drivers and removal
of all redundant inverse-clock posts do not improve the worst static result.
These candidates have not replaced the baseline or qualified the ADC.

The large receiver hit the pinned router’s known optional clock-NDR restart
loop. Its sized checkpoint is preserved. A fresh [routing recovery](receiver-routing.md)
uses ordinary PDK spacing and repeats all downstream acceptance checks. The
first recovery was killed by the kernel OOM handler during another simulation's
waveform export; it produced no routing verdict. A fresh recovery is queued
from the same verified post-sizing checkpoint. It has no routed pass yet.

The 64 µs extracted processing-clock simulation reached its stop time, but
waveform export exhausted disk space. The compressed archive ended at
61.247 µs and failed its gzip integrity check; it cannot establish a complete
result. A fresh run verifies byte-identical circuit and controller inputs,
reserves 1.75 GiB of actual disk blocks for compressed output, and holds the
shared EDA memory slot through simulation, export and checking. Its test
window, numerical settings and acceptance limits are unchanged. The complete
extracted result remains pending.

The [three-domain reset-controller macro](phy-reset-physical.md) now passes
native/RTL/mapped and routed replay, filled DRC and strict LVS in 140 × 140 µm.
Nominal extracted setup/hold slack is +2.176/+0.270 ns. Downstream reset
distribution, recovery/removal, MTBF and final fill coupling remain open.

The [transmitter MOS banks](transmit-mos-layout.md) pass the six main/history
steering, sink and cascode geometry cases. They use parallel via cuts into
center-fed TopMetal1 collectors; the 1,500 µm cascode has three guarded banks.
Strict LVS also rejects a deliberately missing bank. Six-case extraction
preserves every finger and the intended external connection points. Nominal TT
DC checks pass with at most 3.54% current error against independent equal-width
PSP references. Unfilled metal minimum density, connected driver layout,
transient/PVT electrical behavior and EM remain open.

The [50 Ω line pullup](transmit-pullup.md) now passes local geometry, strict
LVS, missing-section rejection and extracted TT DC testing. Eight parallel
sections use distributed contacts and center-fed upper-metal collectors.
Their individual body couplings are matched by physical identity, with charge
conservation and negative parser tests. Full-chip density, thermal/EM and
connected transmitter qualification remain open.

The [connected four-device current cells](transmit-current-cell.md) pass local
DRC/LVS, cut-input rejection and conserved extraction. Both steering directions
pass extracted TT DC comparisons at 1.3/2.3/3.3 V line bias and half/nominal/
four-thirds current. Worst errors are 4.00% main and 3.08% history, below the
unchanged 5% local budget. Wider current routes, sixteen-cut TopVia2 landings
and direct branch ports corrected an initial 16.0% layout loss. Complete
transmitter composition and PVT/full-chip qualification remain open. Six
extracted 42 ns switching cases also pass at an 8 ns symbol interval, using
ideal gate and bias sources. Each waveform passes independent differential
and total-current bounds before comparison; worst differences are 3.61% main
and 2.90% history. Actual translators, bias startup and line loading remain open.

The separate [PSP charge-ownership candidate](mim-area-budget.md) retains two
explicit local capacitors per current cell and assigns eight generic charge
terms to the corresponding process MOS terminals. Five ownership guards pass.
Its main/history storage macros pass maintained local DRC/LVS, missing-capacitor
and substrate-tap rejection, and conserved extraction (115.82 s). All six
42 ns extracted current-cell/storage switching cases pass (327.38 s), with
maximum differences of 3.61% main and 2.93% history, using the original
independent and comparison limits. This remains a candidate: full transmitter
loading and chip integration have not adopted it. The current eight-transmitter
component inventory exceeds recommended whole-chip MIM area even before
receiver and clock storage are included.
The complete candidate's 64 ns numerical startup now passes at both current
tolerances, with maximum departure 0.504 mV across all 77 voltage outputs
against a 1 mV budget. Both full transmitter circuits are prepared and queued
for the shared memory slot; this startup result does not establish switched
transmitter operation.

The [connected trim/cascode bias macro](transmit-bias.md) passes local DRC,
strict LVS, missing-switch rejection and all 64 extracted TT trim codes.
Four-cut routing vias and wider buses repair the first candidate's loss and
nonmonotonic carry transitions. Physical/schematic checks pass independently;
maximum comparison error is 3.299% against the unchanged 5% local budget.
The footprint is 221,281.85 µm². A fresh maintained bias/guard run passes
(119.13 s) after the shared row helper changed. Full TX storage/loading/startup,
calibration, PVT, mismatch, fill and EM remain open.

The new [physical transmitter partition](transmit-physical-partition.md) exports
all 931 actual C++ contributions exactly once across 33 blocks. Eight partition
and six current-cell binding guards pass. The actual eight main/history cells
match the qualified physical banks; retained storage has explicit access to
their existing TAIL/LOWER RC anchors. Complete loaded and routed assembly is
still required.

The latest PAUSE datapath repair completed detailed routing with zero router
violations and exact 630,084-cycle replay. Extracted setup is +0.091719523 ns,
but one SRAM-address net still exceeds the 80 fF limit. Its two antenna diodes
made the point-to-point repair incorrectly classify it as multiple functional
loads. The helper now buffers its sole logic sink while preserving the diodes;
the real-cell connectivity CTest passes. Fresh detailed routing finishes with
zero router violations, exact 630,084-cycle replay, setup +0.089458220 ns,
hold +0.112074018 ns and no electrical violations. The final crossing-check
supervisor times out while waiting for the shared memory slot. A fresh
supervisor verifies the retained route and replay, then queues crossing and
geometry checks with their execution budgets starting after admission.
The previous interrupted and failed receipts remain available.

The repeater now uses [Ethernet PAUSE flow control](../flow_control.md), as
selected by the user. The forwarding path has two 4096-word synchronous frame
buffers mapped to eight actual IHP dual-port SRAM macros. Independent C++, RTL,
PDK SRAM and IHP standard-cell replays pass 630084 observations, 240 intact data
frames, six PAUSE/six resume frames, and unsupported-peer/reset isolation.
The dedicated FIFO test also passes stopped-transmit prefill and bank/pointer
wrap; it catches the repaired false underrun before the first SRAM read.
The actual FLP composition negotiates symmetric PAUSE in both C++ and RTL over
264750017 reference cycles. `NegotiatedRepeater` now connects these negotiation
results directly to forwarding, with immediate disable on PMA loss/restart.
Its maintained C++/RTL integration CTest passes with agreement over 264761099
observations, including received PAUSE and stopped-clock link loss.
The first pipelined SRAM/control route completed with zero router violations
and exact functional replay, but failed extracted setup at −0.516693354 ns
and exceeded signal-slew limits. Hold was +0.004137135 ns. A new registered
read-credit stage separates Gray decoding/pointer subtraction from queue and
SRAM-issue control. It uses only the second synchronizer stage. Native FIFO/PAUSE, FIFO/forwarding/negotiated RTL, PDK SRAM and mapped-cell
tests all pass. The renewed placement passes setup +0.302903503 ns and hold +0.080113918 ns;
a 2.5-times wire-capacitance optimization trial then failed global setup at
−1.948678613 ns after inserting long buffer chains. It was stopped before
detailed routing. Restoring nominal PDK RC and allowing setup buffer removal
passes global setup +0.311036527 ns and hold +0.117311329 ns; clock capacitance
and six fanout violations remained in that estimate. Detailed routing completed
with zero router violations and the exact 630084-cycle replay. Extracted setup
(+0.188799202 ns) and hold (+0.011086354 ns) pass, but 710 slew, 576 capacitance
and six fanout report entries fail. A further FIFO change separates read issue
from same-edge queue consumption while retaining full frame throughput. Its
native, RTL FIFO/forwarding/negotiated, PDK SRAM and mapped-cell checks pass.
The renewed global route passes setup +0.593975544 ns and hold +0.115303881 ns,
with no slew or fanout violations, but fourteen clock branches exceed the
80 fF capacitance limit. A targeted branch split now handles both buffer leaves
and directly attached register clock pins. All fourteen branches pass a real
checkpoint connectivity exercise. The fresh global route now passes setup
+0.604821086 ns and hold +0.120023437 ns, with no electrical or antenna
violations. Detailed routing finished with zero router violations and exact
630084-cycle replay. Extracted setup (+0.444233119 ns) and hold
(+0.088663414 ns) pass, but 36 slew and 180 capacitance report entries fail.
The capacitance failures comprise 142 signal nets, 23 clock nets and 15 input
ports. The next candidate uses three-times wire capacitance during signal
repair and 0.26 ns / 60 fF finishing targets, then restores the original
0.30 ns / 80 fF acceptance limits. A concurrent converter rebuild required
fresh SRAM/cell proofs; both pass with a pinned converter, and fresh placement
passes setup +0.294023484 ns and hold +0.080415234 ns. The new global
route passes setup +0.332676113 ns and hold +0.120145671 ns, with zero
electrical and antenna violations. Detailed routing and exact replay completed.
Extracted setup is +0.132938102 ns and hold +0.113469906 ns; only two
capacitance entries remain (one signal and one clock), with no slew/fanout
violations. The clock branch includes an inverter load that the repair helper
previously rejected. The helper now preserves inverter leaves, and a second
helper splits overloaded point-to-point signal wires. The real OpenROAD
connectivity regression passes. At most two extracted-load repair passes are
allowed, each followed by fresh detailed routing and extraction; the original
limits and final replay remain mandatory. Fresh SRAM and mapped-cell CTests
pass after a shared C++HDL header changed. Fresh placement, route and
independent geometry/crossing qualification are queued. The fresh global route again passes setup
+0.332676113 ns and hold +0.120145671 ns with zero electrical/antenna violations.
Detailed routing is running. The
[physical flow](flow-control-physical.md) checks extracted crossing delays
separately from synchronous timing, then streams actual SRAM GDS for full DRC/LVS.
These remain macro results; complete MDI latency, slave transmit-clock recovery
and full-chip qualification are not established.

The physical processing oscillator's compact resistor layout passes conserved
extraction and the six fixed-trim cases in the controller's acquisition interval.
The preceding early-settling stress failed at 119.844 ps against 80 ps and remains
recorded. A newly tested Magic tile-area correction prevents negative area
allocations for breakpoints outside their tile; 3,244 independent partition
cases pass. Fresh oscillator, gate and reference-PLL extractions conserve
capacitance. Field-solver calibration remains open.

A separate analytical oracle now reproduces the pinned extractor's sidewall
coefficient defect: all 70 nonzero checks return approximately half the
expected coupling. An isolated Magic candidate removes the duplicate parser
halving. It passes all 84 checks across seven metals and both parser keywords,
including orientation, scaling and shielding; single/series MIM extraction
also passes. The hierarchical PLL extraction used this candidate. Existing processing
clock simulations keep their original tool inputs. These coefficient checks
do not establish field-model accuracy or upgrade older extracted results.

The ADC reference PLL's compact oscillator placement passes complete filled
DRC and strict LVS, including rejection of a disconnected input. The physical
inventory contains 526 normalized devices. Its full 2 us extracted electrical
simulation failed: the output settles near 124.689 MHz and the divided output
near 62.298 MHz, instead of 250/125 MHz. The reference-side detector also
misses alternate reference edges. Its failed waveform is retained. Geometry
inspection found detector enable/reset connections spanning folded row cuts.
The placement objective now counts both row buses and the connecting spine;
six targeted placement tests pass in CTest. The new candidate reduces the
reference-master enable span from 1610 to 237 um and the feedback clock-inverter
span from 1597 to 103 um. Other paths grew. The new layout passes full DRC with zero violations and strict
LVS with all 526 normalized devices; its bounding area is 2.067 mm2. Fresh
extracted electrical simulation failed: OUT is 166.612 MHz and DIVIDED
83.288 MHz in the final 400 ns. The reference detector still misses edges;
control ripple is 59.5 mV against the unchanged 2 mV limit. The next placement
uses exported C++ ownership to retain each complete master/slave flip-flop
inside one physical row. Twelve hierarchy/placement checks pass; new full
DRC/LVS and electrical qualification are required. Independent pre-fill checks
confirm unchanged physical devices/topology and all 91 components of each
flip-flop retained within a row. The new footprint is 1.611 by 1.404 mm.
Full filled DRC passes and strict LVS matches all 526 normalized devices.
The corrected-sidewall extracted simulation still fails: OUT is 125.031 MHz,
DIVIDED 62.448 MHz, and the reference detector responds to only 25 of the
50 reference edges in the final 400 ns. Control ripple is 142.7 mV. The
common-reset wire contributes 785 fF of extracted interconnect load; the
reference master-Q wire adds 223 fF. A startup diagnostic retains the same
network and observes internal detector nodes to locate the missed-edge cause.
The clock remains unqualified.

A full 2 µs sizing trial reduced the common-reset R3 resistor from 1 kΩ to
200 Ω while retaining the old extracted wiring. It still failed: OUT was
124.991 MHz, DIVIDED 62.552 MHz, and control ripple 137.4 mV in the final
400 ns. That resistor-only change was not adopted. Increasing the four
reset-NAND channels fivefold also fails the complete 2 µs test: OUT averages
167.012 MHz, DIVIDED 83.494 MHz, and control ripple is 139.9 mV. The detector
produces only 34 UP pulses for 50 reference edges in the final 400 ns. Its
retained waveform shows reset recovery and latch propagation overlapping the
next reference edge. This sizing trial has no matching GDS and was not adopted.
A compact PDK standard-cell realization of the same sequential functions is
being evaluated separately; it does not qualify the existing PLL layout.
The maintained cell-divider schematic tests pass all six load/corner
combinations at 250 MHz. Fresh generic detector/storage C++ and Verilog-A
regressions and their comparisons pass. Earlier detector output buffers fail
slow/hot loading or extracted reset timing. A new gated-output detector makes
reset decisions from stored state independently of load and suppresses both
controls while storage clears. It passes all 42 schematic corner/load cases. The extracted wrapped-phase
case still misses the reference edge at 160.05 ns during common-reset
recovery. The resulting reversed phase polarity blocks adoption. Its
675.84 µm² symmetric layout passes local DRC, strict LVS
(96 normalized devices), cut-clock rejection and conserved RC; the first
routing short was detected and corrected before those checks passed.

The divider's first 264.32 µm² layout passes the absolute electrical oracle
at slow/hot but exceeds the 100 ps schematic-comparison bound at 110.07 ps.
Moving the clear driver and removing unnecessary port stubs reduces it to
224 µm². All six extracted corner/load cases then pass independently and
against their schematic, with slow/hot maximum difference 90.06 ps. All seven divider CTests passed after the common parser was separated.
Later shared-router edits require refreshed geometry/provenance checks.
Isolated minimum-density violations remain for later chip fill. These are
component results, not a repaired or qualified full PLL. See
[compact clock-cell realization](reference-clock-cells.md).

A new whole-IC ownership binding removes the detector/divider implementation
by C++ device identity while retaining surrounding connections and storage.
Its first full-loop **schematic diagnostic** passes the existing 2 µs contract:
250.001554 MHz output, 125.000001 MHz divided feedback, 1.464 mV control ripple
and 768.15 ps maximum output delivery. The generic outer R/C values remain
ideal; this does not qualify the failed physical detector or a new PLL GDS.

The processing-clock controller had a second acquisition defect: after its
512-reference-cycle monitor detected drift, it used the 128-cycle threshold to
choose the retuning direction. The corrected controller passes 698,640 matching
native/RTL/mapped-cell events, including slow and fast drift recovery without
reset. The previous controller fails the new directed regression. Fresh routed
replay also passes, with extracted setup +0.018183399 ns and hold +0.061225358 ns.
The two extracted Gray buses pass their independent delay/skew checks. Full
DRC passes with zero violations; strict LVS matches 20,540 devices and 40 ports. The new native and Verilog-A 64 µs closed-loop delivery runs independently pass
at 134.132 and 134.467 MHz. Their direct comparison passes: frequency differs
by 0.249% (1% limit), delivered-waveform normalized RMS by 20.342 mV (60 mV
limit), and sustained readiness by 128 reference cycles (417-cycle limit).
The PSP transient completed all 64 us and its electrical checks pass at
133.773 MHz, with 3959 delivered pulses and 175.7 ps maximum delivery delay.
Its overall receipt fails provenance because `cpphdl_array.h` changed during
the run. The exact former header was recovered and hash-checked from Git for
historical reproducibility. A fresh source build and export now reproduce the
controller library, Verilog-A model, metadata and complete PSP circuit byte
for byte. A separate artifact-equivalence receipt therefore links the current
source to the completed electrical checks; the original failed receipt is
preserved. This is not a new transient, native-solver or chip proof. The older
extracted delivery run timed out after 12 hours at approximately 37 us of the
required 64 us. Its partial controller event log shows admission but cannot
qualify delivery. A fresh extraction with the corrected sidewall tool is being
prepared. The full-run wall-clock budget is now 24 hours, with the same circuit,
64 us duration, timestep and electrical acceptance bounds; CTest also allows
time for the serialized full-trace checker. The small startup checker does not
need to wait behind memory-intensive routing.
The first Verilog-A run exhausted its
1 GiB allocation limit while creating output vectors; the identical complete
circuit was resimulated successfully with 3 GiB reserved. The original failed
receipt remains unqualified; older 64 us successes below are
historical evidence, not qualification of the corrected controller.

The integrated negotiated PAUSE repeater again passes its C++/RTL test. The five
changed RTL hierarchies have been regenerated from fresh passing proofs and
linted. The receiver resume stopped before routing on the changed shared
header; fresh native/RTL checks pass, and mapped checks are running. Receiver routing, physical
slave transmit-clock generation, connected
TX/RX analog macros, full PHY/PCS assembly, padframe/power and full-chip
verification remain unfinished. No complete repeater GDSII is qualified.

The detailed component history below includes superseded candidates and their
original measurements; it does not replace the current status above.

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
provides a smaller alternative. The revised upper-metal cascode-storage bank
and dedicated MOSCAP bank together occupy 231,152.7982 square micrometres,
37.79% below the two-bank MIM reference footprint. The old lower-metal bank
failed its first extracted high-frequency check at 37.1 pF against the 50 pF
floor. Fresh upper-metal geometry/LVS and both disconnection negatives pass;
extracted TT/27 °C AC also passes over 1.6–2.4 V and 100 kHz–1 GHz, retaining
at least 63.24 pF. The dedicated MOSCAP bank now uses distributed gate vias
and merged upper-metal return collectors. Fresh local DRC, adapter-based LVS,
both disconnection negatives and TT/27 °C extracted AC pass: minimum 435.06 pF,
maximum wiring loss 3.20% over 0.85–1.10 V and 100 kHz–1 GHz. Its former
single-via unit failed at 2.10 pF instead of the required 10 pF. Dedicated-model
binding requires independent LVS and retains all four distributed terminals;
raw wiring and exact passive-node reduction records remain available.
Extracted PVT, field calibration and full-transmitter composition remain open.
The complete schematic
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

## Autonomous receiver and clock integration, 2026-10-24

[`AutonomousADCReceivePHY`](../autonomous_adc_receive_phy.md) now connects the
actual thirteen-slice SAR sequencers to fixed-rate timing recovery, a clock
crossing, autonomous cancellation/equalization and PCS. Its interface consumes
comparator decisions; it does not consume far-end training targets. Four
coupled digital fixture cases recover two complete frames and 1,598 bytes each,
with exact native/generated-RTL trajectories. These fixtures model comparator
decisions from held samples. They do not qualify the complete electrical AFE,
automatic gain/offset calibration or the full cable envelope.

The ADC remains at 250 MHz; the separate processing clock is qualified over
132–136 MHz. The carry-save PCS baseline uses exact path comparison and a
six-stage branch-metric pipeline. Its frame, fault and symbol-enable traces
match native C++, generated RTL and 71,945 mapped IHP cells. The slow-corner
136 MHz cell-only estimate passes with 0.85 ns setup slack. Revised placement
buffering initially gave 0.002952305 ns setup slack and −0.215014458 ns hold
slack. Drive-strength repair after hold repair shortened some paths again.
The maintained flow now inserts final hold delay after that repair, then uses
the placement space reserved for late buffers during legalization. The fresh
`build/clock-integration/final-hold-space-receiver/PCSReceiver-a63onlnw`
checkpoint has **0.122084454 ns hold slack** and **0.000614619 ns setup slack**;
slew, capacitance and fanout checks pass. The 136 MHz clock, 0.1 ns uncertainty
and interface constraints are unchanged. The requested 0.15 ns repair margin
is an optimization target, not a relaxed acceptance limit. Setup margin is
very small. This is a placement result, not routed timing closure; detailed
routing, post-route equivalence, extraction and physical verification remain
pending. The routing flow repeats final hold repair using routed estimates
and still requires nonnegative extracted setup and hold slack.

Global-route repair of that baseline subsequently failed setup at −0.450 ns
in `metrics.squared`, despite repairing hold to +0.051 ns at that intermediate
stage. The run was stopped before detailed routing; it produced no qualified
GDS. The new candidate removes conditional sample negation before squaring
and instead reverses the five stored level costs in the next stage, using
`(-sample - level)^2 == (sample + level)^2`. Polarity travels with its accepted
sample and freezes during symbol pauses. Pipeline latency remains six clocks.
The independent native branch-metric oracle passes. Complete receiver frame,
fault and clock-enable trajectories now match in native C++, generated RTL
and 73,085 mapped IHP cells. The slow-corner 136 MHz cell setup estimate improves
from +0.85 ns to **+1.86 ns**, with unchanged constraints and functional evidence
required. Its first placement had −0.021163516 ns setup and +0.117585614 ns
hold slack after legalization. A further setup-reserve and hold repair now
runs in a separate OpenROAD process, preserving both the original placement
and the repaired checkpoint. Restarting before global routing avoids mixing
the placement and routed parasitic estimators' update state. The maintained
`build/clock-integration/polarity-margin-separated/PCSReceiver-g1cdmor3` run
reproduces **+0.160363287 ns setup** and **+0.150055081 ns hold** before routing.
Its global-route estimate subsequently failed setup at −0.115185201 ns while
hold passed at +0.139998347 ns. Detailed routing was stopped to preserve that
checkpoint for repair. Replacing one excessive delay cell preserves hold;
repairing the final loaded network and allowing more setup-repair passes gives
**+0.053054452 ns setup** and **+0.139850020 ns hold** after legalization in
`build/clock-integration/final-network-repair-safe/`. Remaining XOR/XNOR slew
violations still reject that candidate. The standard-cell library has only
one drive strength for those gates. Two output-isolation passes improve setup
to +0.200952142 ns and retain +0.139849916 ns hold, but two remaining output nets
still violate slew. The maintained `finish_timing.tcl` now iterates real output
buffers, setup/hold repair and antenna repair. The v5 run in
`build/clock-integration/receiver-final-routing-v5` passes its final global
estimate with **+0.202502027 ns setup**, **+0.139845580 ns hold**, no slew,
capacitance or fanout violations and zero reported antenna violations.
The combined run was stopped during detailed routing as it exhausted available
RAM. Its repaired global database and interruption receipt are retained.
`receiver-final-routing-v6` resumes that exact checkpoint in a fresh process
without the synthesis/timing state. The maintained large-module flow now
separates global timing repair, detailed routing and extracted qualification
into three processes; two physical-driver guard tests pass for this sequencing.
The failed OpenROAD estimator-state attempts are retained. A new CTest exercises the maintained repair on real IHP cells:
the deliberately loaded XNOR starts with a 1.91 ns transition against 0.5 ns,
and the short path starts at −45.273 ps hold slack. Both failures are repaired;
the final estimated hold margin is +167.015 ps. Four physical-flow integrity
regressions also pass after the driver changes.
The v6 detailed route completed in 6 h 05 min with zero router violations.
Independent extraction gives **+0.055523142 ns hold**, but **−1.623519182 ns
setup** and slew/capacitance violations. Normal and fault replay match native;
the long enable/pause/reset replay initially exceeded its 180 s harness limit.
It subsequently passed all **186,741 events** in 319.626 s with a bounded
900 s limit and exactly the same native trace hash. The maintained receiver
flow now allows 900 s for each complete replay. This does not waive a timing
failure or shorten a test.

The retained diagnosis compares the same 112,496 net names in estimated and
extracted SPEF. For estimated wire loads above 1 fF, extracted/estimated
capacitance ratios are 1.358 median, 2.104 at the 95th percentile and 2.381 at
the 99th percentile. `routing_rc_margin.tcl` applies a 2.5× capacitance margin
only during receiver optimization; extraction restores the original PDK LEF
and uses the unchanged OpenRCX model. Its real OpenROAD CTest verifies scaling,
repeatability, restoration and unchanged acceptance constraints. It caught and
fixed compounding caused by `set_layer_rc` modifying OpenDB technology values.
The interrupted v7 candidate is retained. The v8 run found that omitting
resistance in `set_layer_rc` clears it; the helper now supplies both RC values
in the loaded library units. Its extended regression verifies unchanged wire
resistance as well. A checkpoint after electrical repair preserves buffering
before subsequent timing optimization. Both real-cell CTests pass. The v9
incremental repair was stopped with checkpoints retained: its third saved
stage still has −0.927933335 ns setup under nominal placement estimates,
while subsequent routing is congested. A fresh 1.8 mm receiver placement now
uses timing-driven net weighting and routability-driven spreading at 60%
target density. Timing limits and the mapped functional design are unchanged.
That placement completes with +0.059227291 ns setup, +0.140721887 ns hold,
110,410 legally placed cells and no electrical violations. A fresh routing
run imports its hashed database and repeats all downstream checks. Setup
repair now avoids serial rebuffering; electrical and hold repair still insert
required cells. Global screening restores nominal LEF RC after preserving the
2.5× optimization report. Detailed extracted acceptance remains unchanged.
No new receiver GDS is qualified. These placement estimates do not qualify a
finished ASIC. The 359-file RTL catalog has been
regenerated, and every exported hierarchy passes Verilator lint.

The [clock circuits and their qualification boundaries](clocks.md) are now
implemented. The reference clock passes native/Verilog-A requirements,
comparison and the fresh nominal IHP PSP test. Its process-model output is
250.003564 MHz from 125 MHz; simulated output periods span 3.994359–4.005641 ns.
The process receipt is
`build/1G/IHPSG13G2/tests/results/reference_clock_psp/checks.json`.
This is schematic nominal evidence, not an extracted or noise/PVT jitter
qualification. The processing oscillator passes all six static native,
Verilog-A and nominal PSP trim/load/refinement cases. Its direct generic
comparison also passes. At trim 38528 the native/Verilog-A frequency difference
decreases from 0.835894% to 0.413712% when the native timestep is halved;
all waveform, tuning-voltage and supply comparison limits pass.
Closed processing-clock control passes native and Verilog-A tests using the
achieved oscillator edges. Its direct comparison also passes after rechecking
both full electrical trajectories: frequency difference is 0.213552%, output
period-shape RMS difference is 15.838 mV, and sustained readiness begins on the
same reference cycle. The two autonomous controllers select different trim
codes; accounting for their ideal passive DAC voltages leaves 1.313 µV tuning
voltage disagreement. The first direct-voltage comparison failed because it
did not account for those different commands; its report is retained in
`build/clock-integration/closed-clock-comparison-initial/`. Five guard tests
include an independent nodal solution of the two DAC ladders. The closed-clock
PSP run passed for that controller revision at 134.282632 MHz, trim 38592,
1.197758 V minimum high level and 1.190273 V minimum supply, with all 8,000
reference edges and the final 512 cycles qualified. The completed receipt and
original controller are retained under
`build/clock-integration/clock-before-timing-fix/`; the clock tests require rerunning
after the acquisition-range correction below. These checks do not implement the slave transmitter's
recovered-clock tracking or qualify a routed clock tree. Complete two-port
PHY integration, analog layout and full-chip verification remain unfinished;
there is still no complete repeater GDSII.

The acquisition clock must also meet a faster timing budget than the settled
PCS clock. The nominal PSP acquisition trajectory contains a 3.370812 ns
delivered period (296.664 MHz). A new controller regression maps both domains,
replays native/RTL/mapped/resized trajectories, and checks slow-corner cell
timing at 125 MHz reference and a 400 MHz raw-clock design budget. The original
controller fails with −0.661825299 ns setup slack, despite exact functional
trajectories. The longer independent fixture now covers 192,836 events and
counter wraparound. A separate arithmetic prototype precomputes both search
midpoints and registers counter carry predictions; it preserves the complete
native trajectory and has +0.685320 ns cell-only setup slack. The same 192,836
events also match generated RTL, mapped IHP gates and resized gates. That
arithmetic is now in the maintained controller. Both fresh digital CTests pass:
1,474 mapped cells, exact complete trajectories and +0.685319841 ns cell setup
slack. All 359 generated RTL/package files have been regenerated and their
complete hierarchies linted. The fresh native closed-circuit test passes;
The queued Verilog-A and partial PSP runs were deliberately stopped after a
fast-corner diagnostic invalidated that acquisition budget; their interruption
receipts and source snapshots are under
`build/clock-integration/before-clock-envelope-fix/`.
At maximum trim, FF/1.32 V/−40 C with the declared 50 fF stage capacitances and
20 fF output load produces **645.155 MHz** and a **1.550016 ns minimum period**.
The diagnostic is under `build/clock-integration/clock-fast-limit/`; it is one
operating point, not a complete oscillator envelope. The revised controller now passes both maintained digital CTests at an
**800 MHz raw-clock budget**: **+0.172788680 ns cell setup**, **1,497 cells**, and
**292,092 exact C++/RTL/mapped/resized events**. It uses a continuously running
counter with two-bit predicted carries, delayed Gray conversion, a pipelined
heartbeat comparison, unary silence history and asynchronous reset assertion.
The expanded fixture covers five tunable and three rejected curves, startup
rates up to 780 MHz, wraparound and cold/warm reset without either clock.
The maintained source and generated RTL have been updated. All twelve PSP startup/load/PVT points and six acquisition-checker guards
pass. The fastest complete startup period corresponds to 645.421 MHz. Slow
or stopped midscale is allowed because the reference-domain search raises
trim; maximum trim must start and sustain a clock above 136 MHz. The corrected
checker fully revalidated ten hash-matched trajectories and simulated the two
slow-corner midscale cases fresh. Fresh
closed-circuit qualification is required for this revision. The cell proof
still excludes routed hold, CDC, reset release and receiver clock delivery.
The new controller physical flow preserves both clock domains and times
`ready_pcs_out` in the raw-clock domain. Its input snapshot requires exact
native/RTL/mapped/resized traces and tracked mapped JSON as well as Verilog.
Four snapshot guards and the existing physical-driver regressions pass. A
controller physical run completed under
`build/clock-integration/controller-decision-watchdog-physical/`. The earlier bounded
candidate completed routing and exact replay, but failed extracted setup at
−1.434322 ns (hold +0.067394 ns), so no qualified GDS was produced. The revised
controller registers Gray decoding and readiness qualification. Its clock tree
removes redundant buffer levels and advances the readiness branch. Placement
estimates pass at +0.061829 ns setup and +0.069796 ns hold. The capacitance budget
is now 0.1 pF with fanout eight; the 800 MHz acquisition requirement, uncertainty
and output delay remain unchanged. Four real-cell clock-tree regressions pass.
The readiness revision subsequently completed detailed routing with zero router
violations and exact functional replay. Extracted hold passes at +0.066345 ns,
and raw-output setup at +0.041346 ns, but the reference measurement/decision
path fails setup at −0.169423 ns. Its failed receipt and source snapshot remain
under `build/clock-integration/controller-ready-physical/`. The maintained
controller now captures the complete 128-cycle measurement and decides on the
next reference edge. Four fresh CTests pass, including all 292,092 events in
native/RTL/mapped/resized flows. A negative regression reproduced lost watchdog protection when the reference
stopped in the new decision state. That intermediate physical run was stopped
before qualification. The maintained fix keeps the watchdog armed there and
registers its acquisition level before crossing domains; the new boundary-loss
fixture passes in all four digital flows. The final 280 × 280 µm controller GDS passes its component checks:
extracted setup +0.052648444 ns, hold +0.069256634 ns, no electrical violations,
exact native/routed replay, zero full-deck KLayout DRC violations, and transistor
LVS matching 47 circuits, 40 ports and 20,432 devices. The independent extracted
Gray-bus CTest passes all 22 bits at slow/fast corners; worst conservative arrival
windows are 0.6918 ns and 0.4703 ns. This is a controller macro, with nominal
routed RC and no post-fill coupling, reset-release or metastability-MTBF claim.
The updated closed-clock C++RTL and Verilog-A flows both pass independently,
at 134.245431 MHz and 134.532411 MHz respectively. The nominal PSP loop also
passes at 134.282637 MHz with trim 38592 and 512 final qualified reference edges.
A bounded recheck verifies all 8,671,136 samples from the completed 64 µs
simulation. The native/Verilog-A comparison passes with 0.21332% frequency
difference and 38.079 mV output-period RMS difference. Its initial list-based
checker exceeded its memory cap; the failed receipt and original reader are
preserved. The original waiting PSP checker/CTest queue was stopped after the
independent compact recheck passed. Exact original physical-flow scripts
are retained by their recorded hashes under
`build/clock-integration/retained-flow-sources/`, since subsequent receiver
optimization changes are not the scripts used to qualify this controller.
No complete repeater GDSII is qualified.

The preceding controller GDS used a 5 fF output load. Charge integration of
the actual admission gate, at both held clock levels and all three declared
process corners, finds a maximum equivalent request capacitance of 52.790 fF.
The maintained input-charge checker passes all six trajectories. Controller
implementation now reserves 80 fF for that gate and its interconnect; the
100 fF total-net acceptance limit remains unchanged. The first loaded route
failed global timing because its 72 fF optimization target was smaller than
the external load. A corrected 95 fF optimization target allowed routing, but
extracted setup still failed at −0.233637109 ns, with positive hold slack.
That failed layout and its exact functional replay remain recorded under
`build/clock-integration/controller-loaded-admission-v2/`.
The next candidate factors the readiness AND into an enable/reference AND,
a raw-qualifier NAND and an output inverter, preserving asynchronous withdrawal.
Its real-cell Boolean proof and four malformed-topology rejection cases pass.
The controller also uses 2.5 times LEF wire capacitance during optimization;
independent extraction restores original process parameters. The first factored run exposed a long serial buffer
chain introduced by setup rebuffering; this optimization is now disabled for
the small controller, while electrical and hold repair retain their buffers.
The readiness register has a dedicated buffer-8 clock branch to balance its
input and output paths. Five real-cell clock-tree cases pass, including
rejection of a conflicting branch before mutation. The flow records the conservative optimization report,
then restores original LEF capacitance for global screening before independent
detailed-route extraction. A real-cell regression checks that the margin does
not leak into the final timing view. The previous shared-root candidate failed
extracted setup by 7.528 ps; a buffer-4 branch failed by 9.838 ps, with positive
hold in each case. A separate sizing study selected buffer-8. Its current
candidate completes detailed routing with **+0.037747696 ns extracted setup**
and **+0.064936139 ns extracted hold**, with no electrical violations. Its
280 × 280 µm GDS passes the complete DRC deck with zero violations and
transistor LVS (46 circuits, 40 ports, 20,404 devices). Fresh controller
functional, mapped/resized and routed proofs match all 292,092 events. The
extracted Gray-bus crossing audit also passes. The receipt and original flow
source snapshots are retained under
`build/clock-integration/controller-loaded-admission-v7/ProcessingClockControl-1jbz89kz/physical/`.
Subsequent receiver routing-script changes do not modify this qualified
artifact or renew its provenance. Post-fill coupling, reset release and the
connected receiver clock tree still require qualification.

The new 38-MOS `processing_clock_gate` passes both registered CTests: six
checker guards and the complete electrical matrix. Native, Verilog-A and
IHP TT/1.2 V/27 C, SS/1.08 V/125 C and FF/1.32 V/−40 C each deliver the
32 expected complete pulses while blocking all 32 acquisition cycles. The
slow/hot case has a 3.684 ns minimum high pulse and 178.242 ps maximum edge
delay at 20 fF load. Native/Verilog-A RMS disagreement is 18.392 mV at the
output and 11.386 mV at the stored request, below the unchanged 20 mV limits.
The comparison excludes one initial native sample before SPICE's first
positive integration time; both full-duration coverage checks still apply.
The initial comparison-boundary failure is retained under
`build/clock-integration/gate-proof`; the fresh passing receipt is under
`build/1G/IHPSG13G2/tests/results/processing_clock_gate`.
Its generated Verilog-A model is included in the maintained catalog.
This is standalone schematic evidence: controller integration, latch-aperture
timing, clock-loss/restart handling, physical realization and the receiver's
extracted clock tree remain open.

`processing_clock_delivery.hpp` now connects the oscillator's RAW node to the
physical admission gate, sharing its supply and actual transistor loading.
The new closed-loop bench drives ADMIT from `ProcessingClockControl` readiness
in the raw-clock domain and loads the delivered output with 200 fF. Its native
build, Verilog-A export and 89-MOS process binding succeed. Five checker cases
pass, including lost pulses, cut pulses and output before admission. The complete
C++RTL test passes at 134.132254 MHz: 3,833 complete delivered pulses, 3.713606 ns
minimum high pulse and 36.706 ps maximum edge delay. Verilog-A passes at
134.466526 MHz with 3,980 complete delivered pulses, 3.716310 ns minimum high
pulse and 25.537 ps maximum edge delay. Its initial checker exhausted memory
after simulation completed. A packed-double reader now retains every sample
without Python objects for every value; four reader regressions pass. Separate
recheck receipts validate all 1,324,173 native and 10,647,127 Verilog-A samples,
the original simulation hashes and the unchanged electrical limits. The failed
original receipt is retained. The independent generic comparison passes:
frequency differs by 0.24859%, delivered-clock waveform RMS by 20.342 mV,
and sustained readiness by one 128-reference-cycle measurement window.
The connected PSP acquisition remains running. This still excludes the actual receiver clock tree, reset distribution,
clock-loss/restart qualification, latch aperture characterization and layout.

The new `PHYResetControl` connects the three-domain release chains to enable,
power-good and both clock qualifications. Its generated RTL binds the combined
request to the child's asynchronous-reset pin. Both reset CTests pass: the
standalone release block matches 30,720 events, and the connected control
matches 153,600 events across native, generated RTL and mapped IHP cells.
The latter uses 21 cells, including six resettable registers. Tests include
brief qualification withdrawals while every destination clock is stopped.
This does not yet connect reset to the complete receive pipeline or qualify
physical reset routing, pulse-width limits, recovery/removal or metastability.

The connected clock-admission layout now uses actual MOS fingers and MIM
plates, including two physical plates for each 2 fF series element. Strict
KLayout LVS matches 104 devices; the complete DRC run has only isolated-window
minimum-density violations. Local geometry, pin and antenna checks pass, and
removing an input via fails LVS as required. This is an unfilled standalone
macro, not a complete chip. The [analog extraction flow](analog-extraction.md)
preserves 46 MOS fingers and 28 MIM plates. Its capacitance-conservation and
real MIM extraction guards pass. Placement now puts capacitors near their
connected devices and reduces the enable wiring load from 80.735 to 15.485 fF.
The controller's real output masters driving the extracted gate and a 200 fF
output load pass TT/27 °C/1.2 V, SS/125 °C/1.08 V and FF/−40 °C/1.32 V.
Each produces 32 complete intended pulses; worst delay is 352.431 ps against
the unchanged 400 ps limit. Extracted input-charge requalification now passes
all six held-clock/process cases at 100 ps command edges. Maximum equivalent
load is 71.509 fF; the remaining 8.491 fF must cover the external route from the
controller. Its receipt is `build/clock-integration/clock-gate-placed-charge-v2/checks.json`.
Field-solver calibration, clock-tree/reset integration and full-chip placement/fill remain
open. See `build/clock-integration/clock-gate-placed-controller-pex/checks.json`.

### November 3: powered receiver hold repair

The hold-trim protection filter was incorrectly treating connected VDD/VSS
special nets as reasons to skip data cells. The fixed filter inspects signal
terminals while preserving all protected-cell and clock-net exclusions. The
powered fixture reproduces the original no-op and now passes rail preservation,
rejected-change rollback, exact endpoint and fresh-process timing checks.

The bounded full receiver run under
`build/clock-integration/receiver-onehot-revision/bounded-powered-hold-repair/`
completed 18 trials, retaining 16 delay reductions and rejecting two changes
that would violate the 0.1 ns hold reserve. After final legalization, global
routing and electrical repair, estimated setup is **+0.142126322 ns** (previously
+0.078034468 ns), with **+0.122228563 ns hold**, no electrical violations and
exact endpoint equivalence. Exact flow source snapshots accompany the receipt.
This is a global-route diagnostic; fresh detailed routing/extraction and
complete-chip integration remain required. No final ASIC GDSII is qualified by
this result.

The following `branched-buffer-repair` diagnostic uses an explicit four-sink
candidate bound to address buffers on the remaining critical logic path. Five
focused CTests, including the new real-cell three-sink case, passed in 4.74 s
before that run. Its final receipt must be checked independently; starting the
run is not evidence of improvement or qualification.

That four-pass buffer diagnostic has now completed: 192 buffers removed and one
new buffer inserted to repair the observed weak-driver slew violation. The final
global estimate is **+0.191255912 ns setup**, **+0.122228563 ns hold**, with no
electrical violations and exact endpoint equivalence. Reloading the preceding
hold-repair seed agreed exactly with its saved setup/hold reports. The separate
real-cell early/full-route handoff regression passed in 12.70 s. These are still
estimated receiver results; detailed-route extraction is the next acceptance
step, followed by routed C++RTL-reference replay and physical checks.

The fresh route probe is running under
`receiver-onehot-revision/early-route-powered-branches/`. The checked continuation
under `receiver-onehot-revision/powered-branches-continuation/` waits for its
extracted timing before attempting full routing, native-reference gate replay
and receiver layout qualification. The retained receiver hardware oracle's
source hashes still match. Neither an active process nor a passing global
estimate establishes detailed-route or complete-chip signoff.

The probe has advanced through pin access and track assignment into detailed
routing. A separate bounded-memory input-load job now waits for its extracted
SPEF and will write `receiver-onehot-revision/powered-branches-input-loads.json`.
It measures both the clock root against the passing 55 fF gate load and reset
against the controller's tested 80 fF output load, leaving external wiring as
an explicit remaining budget. Load-receipt guards, including changed-SPEF
rejection and reset selection, pass. This measurement is diagnostic and does
not substitute for connected waveforms or complete-chip timing.

While routing runs, native process coverage now includes the actual two-MOS
`sg13g2_inv_1` schematic with an inherited output-capacitor class. The eight
native/reference cases (DC and loaded switching at selected TT/SS/FF
conditions) pass independent circuit contracts, timestep refinement and
unchanged numerical tolerances. Maximum waveform difference is 5.84 microvolts
against a 30-microvolt limit. Native-only and fault-injection CTests also pass.
See [the process test documentation](../../tests/IHPSG13G2/README.md) for
scope, timings and reproduction. C++RTL is faster for these short DC cases;
its transients remain slower than ngspice. The extracted clock and complete
1G analog workloads must retain their existing reference qualification until
equivalent native implementations are verified.

### November 3: extracted receiver result and output-load repair

The powered-branch early route has finished. Extracted setup is
**-0.849113703 ns** and hold is **+0.127491802 ns**. The checked continuation
correctly stopped before full routing/replay/layout because setup fails. The
two-iteration diagnostic also reports 17,810 router violations; it is not a
DRC-clean layout. Routing took 11,238 seconds including initialization, and
extraction took 98.47 seconds. The earlier positive global estimate was not
confirmed by extraction.

The completed input-load report measures **30.164881 fF** at `clk`, leaving
24.835119 fF for external wiring within the tested 55 fF load, and
**4.409720 fF** at synchronous `reset`, leaving 75.590280 fF against the
controller's tested 80 fF load. These are integration budgets, not connected
waveform or complete-chip qualification.

A fresh OpenROAD reload reproduces the failed extracted timing exactly.
Two bounded data-sizing rounds retained 69 same-function cell changes and
improved setup to **-0.526512206 ns**, keeping hold at **+0.127491802 ns**.
The independent final reload agrees and exact logical endpoint checks pass.
Receipts are in `powered-branches-routed-seed/` and `extracted-data-sizing/`.
Their unchanged-wire estimates still require legalization and fresh extraction.

Three critical stages expose the gap between estimated and routed loads:

| Output | Global estimated load (fF) | Extracted load (fF) |
| --- | ---: | ---: |
| `_110502_/Y` | 15.732288 | 23.664789 |
| `_110510_/Y` | 16.939215 | 28.031560 |
| `_110511_/Y` | 22.359820 | 33.888061 |

These IHP compound gates have no stronger equivalent master. The post-sizing
repair now calls the existing single-strength output-isolation helper, allowing
a real buffer to drive the downstream load. Every inserted buffer is followed
by legalization and rebuilt routing/parasitics. The original setup, hold,
slew and capacitance limits remain unchanged. Three real-cell CTests passed
in 10.97 seconds, including a loaded a221oi fixture that requires the explicit
isolation, exact endpoint comparison and both timing/electrical screens.

The full `extracted-isolation-repair/` run completed in 1,513.20 seconds.
Its native process and all endpoint proofs passed, with no nominal electrical
violations. Nominal global estimates are **+0.488666892 ns setup** and
**+0.124259055 ns hold**. The unchanged 1.75x wire-capacitance screen still
fails: **-0.539986730 ns setup**, **-0.125959471 ns hold**. The continuation
therefore stopped before detailed routing; these estimates do not qualify a
new GDSII.

The separate pin/hold recovery regression passed using actual IHP cells:
it starts with a failing hold path, requires inserted physical delay, proves
endpoint equivalence and checks both nominal and enlarged-C timing/electrical
limits. Its recorded 845.73 seconds includes waiting for the previous repair's
exclusive EDA memory slot, not 14 minutes of native optimization. The associated
four admission-guard cases passed as well.

`extracted-pin-hold-repair/` completed its native pass in 322.18 seconds.
It retained 431 equivalent input swaps and inserted 15 delay buffers, with
separate passing logic proofs. Hold now passes both estimates: **+0.150902629 ns**
with enlarged wire capacitance and **+0.130429566 ns** nominally. Setup improved
to **-0.391951382 ns** with enlarged capacitance and **+0.552256942 ns** nominally.
Nominal electrical limits pass. Setup still prevents promotion to routing.

The next `extracted-output-repair/` candidate explicitly isolates 48 loaded
single-strength gate outputs on critical data paths, using real IHP buffers.
This addresses slow outputs below the electrical slew limit without altering
the timing constraints. Output selection tests and a native loaded-gate
regression pass; the latter requires at least 50 ps setup improvement, unchanged
logical endpoints, and both timing/electrical screens. Its 92.16-second CTest
duration includes waiting for the preceding repair's memory slot.
The single-pass result is **-0.331142247 ns** enlarged-C setup, with hold
**+0.150902629 ns** and no nominal or enlarged-C electrical violations.
Its endpoint proofs pass, but the continuation correctly stopped. Exact source
versions for this superseded diagnostic are saved in its `source-snapshot/`
directory; they are required to reproduce its original source hashes.

`extracted-output-rounds/` restarts from the verified pin/hold checkpoint with
up to three rounds of fresh critical-path selection (at most 128 outputs per
round). The native regression now uses two loaded loops: only the first is
initially selected, and the second must be selected from the next routed timing
report. It passes, improving setup from **-0.232196271 ns to +0.325656176 ns**,
with exact endpoints and both timing/electrical screens. The final native test
took 1.04 seconds without an EDA queue wait. `extracted-output-rounds-continuation/`
waits for the full receiver result before routing, replay and layout checks.
Fresh extraction remains mandatory; these repair steps do not yet qualify a
new receiver or ASIC GDSII.

The adaptive output run completed three passes (89, 60 and 27 buffers).
Enlarged-C setup is **-0.295707464 ns**, hold **+0.150961027 ns**; nominal
setup is **+0.495310485 ns**, hold **+0.130429566 ns**. Endpoint proofs and
both electrical screens pass. Setup still fails, so its continuation stopped.

`extracted-branch-repair/` now targets a different source of critical delay:
serial data-buffer branches that were excluded by the earlier single-load input
restriction. Only short branches driven by a real buf8/buf16 are eligible;
the merged fanout must be at most eight. Clock/protected nets, fixed/protected
cells, external ports and distant neighbors remain excluded. Each pass removes
at most 32 explicitly recorded buffers, followed by legalization and fresh RC.
Actual electrical and hold repair follow, then exact endpoint proofs and both
timing screens. The real-cell regression improves setup from
**-0.078620501 ns to +0.219764441 ns**, preserves sibling endpoints, and rejects
protected, clock and excessive-fanout candidates. Its final CTest passed in
0.93 seconds. `extracted-branch-continuation/` waits for the receiver result;
no updated qualified GDSII has been produced by these diagnostics.

The branch run completed in 435.83 seconds, removing 78 buffers with passing
endpoint/electrical checks. It did not improve timing: enlarged-C setup is
**-0.304686964 ns**, hold **+0.150961027 ns**; nominal setup is
**+0.481946945 ns**, hold **+0.130429566 ns**. The continuation stopped.
This candidate is not used as the next seed.

`extracted-skew-repair/` instead starts from the better adaptive-output
checkpoint and evaluates 16 same-function clock-buffer drive changes near
critical registers. The existing trial engine rebuilds RC for each replacement
and rollback, requiring non-worse setup/TNS, a strict improvement in one of
them, and at least 0.1 ns hold margin. Final legalization, fresh routing and both
screens still follow. The new native stage regression passes with setup
**+1.840350151 ns to +2.149088383 ns**, exact endpoint preservation and both
timing/electrical screens. Its 52.67-second CTest included waiting for the branch
run's EDA slot. `extracted-skew-continuation/` waits for this receiver candidate
before attempting detailed routing, replay and layout checks.

The clock-drive run completed in 684.75 seconds. Three of sixteen trials were
retained, but enlarged-C setup improved only to **-0.294139832 ns**, with hold
**+0.150961027 ns**. Nominal setup/hold remained **+0.495310485/+0.130429566 ns**.
Endpoint and electrical checks pass; timing admission still fails and the
continuation stopped. No full receiver route or new qualified GDSII resulted.

## November 3 independent reachability and constrained mapping

The reported `decoder.reachable[6]` path exposed an unnecessary dependency on
the winning metric. A trellis destination is reachable whenever any legal
predecessor is reachable. `PCSTrellisDecoder` now computes that recurrence
directly from predecessor validity. Cost and byte selection keep their existing
winner logic. This removes metric comparison from reachability without adding
latency or changing symbol-enable behavior.

The decoder passes native, generated RTL and IHP-mapped gate replay against
the independent absolute-cost reference. The regression adds 32 restarts with
unreachable predecessors, extreme metrics, ties and stalls. A separate mapped
netlist check proves that reachability D cones depend only on previous
reachability and control inputs. Its positive, metric-dependency and cycle
guard cases pass. The three focused reachability/mapping-option CTests pass
in 0.78 seconds; this does not include the earlier synthesis/replay time.

`receiver_mapped.py` now optionally accepts `--abc-delay-ps` and
`--abc-load-ff`. Explicit targets enable ABC's constrained buffering/sizing
flow with a characterized `sg13g2_buf_4` input driver. Constraints and synthesis
script hashes are included in the functional evidence. Mapping without these
options retains the prior behavior. ABC targets are optimization requests,
not timing qualification or changes to the physical clock constraints.

`build/clock-integration/receiver-reachability-revision/` uses a 3000 ps mapping
target and 15 fF output load. Full-receiver native/reference and generated RTL
checks pass; mapped replay and cell timing are running. Fresh 2400 um placement,
clock-tree synthesis and global routing are queued behind those checks, at the
unchanged 7.352941176 ns period. Other critical endpoints include selected
bytes and cost registers, so reachability alone does not establish closure.
Fresh detailed routing, extraction, replay and layout checks remain required
before publishing an updated qualified receiver layout.

The first constrained mapping stopped at ABC because this Yosys version kept
quotes in the `-constr` filename. The runner now writes a fixed `abc.constr`
and launches synthesis from that working directory. A real IHP/Yosys CTest
checks constrained mapping, including ABC's timing script; it and the option
guards pass in 0.46 seconds. Failed-run evidence is retained under
`failed-quoted-constraints/`. Mapping has restarted using the unchanged,
already-validated native/RTL fixtures. Physical work still waits for mapped
replay and cell timing; the failed invocation produced no usable layout.

The corrected full-receiver mapping and replay pass: 75,573 IHP cells,
64 complete frames / 51,136 GMII bytes, 4,244 fault observations and exact
symbol-enable trajectories. The slow-corner cell estimate is **+1.62 ns setup**
at 7.352941176 ns, slightly below the preceding candidate's +1.76 ns. Increased
cell count therefore does not by itself demonstrate a timing improvement.
Fresh placement/CTS and global routing have started under `physical-global/`.

Placement/CTS completed in 803.05 seconds with **+0.246815026 ns setup** and
**+0.126772046 ns hold**, passing electrical checks. Route preparation took
162.04 seconds; global routing/repair took 2,016.42 seconds. Final nominal
global estimates are **-0.091244347 ns setup**, **+0.127878264 ns hold**, with
no electrical or antenna violations. The 2.5x wire-C optimization estimate is
**-1.905304432 ns setup**. Nominal setup improves by about 0.435 ns relative to
the preceding receiver, but still fails. The retained `finished_global.odb`
is now the seed for four bounded buffer-removal rounds at a 200 um span and
0.8 ns optimization target, followed by electrical repair and endpoint checks.
No detailed-route or new GDSII qualification follows from this checkpoint.

This run also exposed a driver error: `--global-route-only` parsed negative
estimates correctly, but the enclosing stage raised on OpenROAD's expected
final timing-failure exit before invoking that parser. The driver now accepts
that specific diagnostic stop only with the exact native error, intact saved
ODB/SDC and consistent failed timing/electrical reports. Other errors, missing
route estimates, missing checkpoints and inconsistent reports remain failures.
The native return code stays in the receipt and all qualification flags remain
false. The orchestration regression exercises the nonzero native stage and
requires that no detailed routing or streaming follows; the guard suite passes
in 0.60 seconds. The original failed receipt is unchanged; a separate
`checkpoint-review.json` records the diagnostic review of its retained files.

Two bounded arithmetic studies in this revision passed SAT equivalence.
At the same slow-corner cells, 0.1 ns input transition and 5 fF output load,
the five squared-distance lookup has **2.337577343 ns** combinational delay
versus **4.565953255 ns** for subtraction/absolute-value/multiplication.
Precomputing full-width branch differences reduces the isolated cost-input
comparison path from **2.104937077 ns to 1.941316724 ns**. These are exploratory
cell estimates, not changes to the active receiver or routed qualification.
A proposed shortened metric representation was rejected: constrained data
symbols can repeat in framer state 2, invalidating its assumed two-symbol bound.

Four bounded global-buffer rounds removed 227 buffers with exact endpoint
equivalence. Nominal setup/hold improve to **+0.212561533/+0.127878264 ns**;
rounds three and four add no setup benefit. A separate native reload reproduces
both nominal values before and after changing the wire-capacitance multiplier.
At 1.75x wire C, setup/hold are **-0.781638801/+0.141463846 ns**, with slew
violations. This is an optimization screen failure, not extracted timing.
The current worst reported endpoint is `metrics.squared`, fed by
`samples_for_metrics`; the squared-distance calculation needs further work.

`receiver_output_stage.py --margin-measurement` can now use this measured
global diagnostic directly. It verifies the producer receipt, saved ODB/SDC,
netlist, endpoint proof, nominal RC restoration and input hashes before
repair. It explicitly records that no filler insertion/removal occurred.
Tests reject unfinished measurements, changed topology, unbound producers and
failed nominal restoration; the output-selection, pin-stage and RC-screen
guard CTests pass in 0.25 seconds. Three bounded output-isolation rounds and
electrical repair complete under `output-repair/` in 349.34 native seconds.
All reload, inserted-buffer and final endpoint proofs pass. Fifty inserted
buffers clear electrical violations, but final enlarged-C setup/hold are
**-0.724759817/+0.141463846 ns**, and nominal setup/hold are
**+0.114909865/+0.127878264 ns**. The timing screen fails; the repaired geometry
is retained as a diagnostic. Detailed routing, fresh extraction and layout
qualification remain outstanding.

## November 3 exact squared-distance lookup

The squared-distance stage now enumerates the 256 constant ADC codes and
combines the one matching result. Each PAM level is also constant after loop
unrolling. This generates a combinational truth table from a short C++HDL
method, without a committed generated table, runtime multiplier or added
pipeline latency. Polarity, accepted-symbol gating and downstream metric
selection retain their behavior.

An isolated native exhaustive test and SAT proof of the generated RTL pass
for all 256 samples and all five level costs. At the same SS cell conditions,
the C++HDL expression measures **2.321997881 ns** versus **4.565953255 ns** for
the preceding arithmetic; it uses 1,664 cells versus 888 for the isolated
five-output lane. These are cell estimates, not routed receiver timing.
`receiver-square-lookup-revision/` runs the production branch-metric and
full-receiver native/RTL/mapped tests before fresh physical implementation.
All six selected native/reference CTests pass in 3.65 seconds, including the
expanded every-byte/every-polarity metric checks. All 20,551 branch-metric
observations match the independent constellation oracle in native, generated
RTL and IHP-mapped flows; that unit pipeline completes in 627.09 seconds.
Full-receiver native and RTL normal/fault/enable trajectories also match:
64 frames, 51,136 GMII bytes, 4,244 fault observations, 127,188 poisoned pause
clocks and 32 disabled resets. Full-receiver mapped replay subsequently passes
in 1,912.17 seconds. The cell-timing check passes in 26.97 seconds, with
+2.0 ns slow-corner setup slack (no routed RC).

Placement/CTS completes in 870.69 seconds, route preparation in 166.82 seconds
and global-route repair in 1,911.44 seconds. The final saved global checkpoint
has nominal setup/hold **-0.226636499/+0.123614453 ns**, with electrical checks
passing. Four buffer-removal rounds and electrical repair take 392.14 seconds;
the independent RC measurement takes 175.92 seconds. Exact endpoints pass and
nominal setup/hold improve to **+0.391374946/+0.123614237 ns**. Enlarged-C
setup/hold remain **-0.804755390/+0.140290454 ns**, with electrical violations.
The worst reported path now runs from `decoder_metrics[2]` to
`decoder.costs[86]`, rather than through the sample squaring stage.

Three output-isolation rounds complete in 367.99 native seconds, inserting
33 identity buffers with exact endpoint proofs. Nominal setup/hold are
**+0.372040182/+0.123614125 ns**; enlarged-C setup/hold are
**-0.699598610/+0.140290558 ns**. Electrical checks pass in both views, but
the optimization setup screen still fails. This is retained diagnostic
geometry, not a routed/qualified receiver GDS.

`receiver_data_stage.py` now permits bounded same-function drive trials on that
saved geometry. It writes a retained ODB/SDC before the trials, legalizes and
reroutes the result, and accepts it only when fresh nominal and enlarged-C
electrical/hold checks pass and setup/TNS improve without a nominal setup
regression beyond 1 ps. A rejected trial restores the exact original masters,
orientations, positions and placement states before rerouting. OpenROAD rejects
loading a second ODB into an already populated database; the native rejection
test exposed that limitation, and the stage now restores these attributes in
place. The retained ODB remains available to a fresh process. The predicate
regression and real-IHP improvement/rejection fixture pass; the additional
fresh-process checkpoint reload check also passes. The production 48-trial pass
completed in 2,210.99 native seconds and retained its original checkpoint after
the candidate failed the selection screen. See the subsequent publication
update for the completed pin-repair candidate.

The real IHP output-isolation fixture also passes the new independent global
measurement and admission handoff: setup improves from -0.232196271 to
+0.325656176 ns, with exact endpoints and nominal RC restoration.

## November 3 clock-controller artifact publication

`results/processing_clock_control.gds.gz` and its layer image now expose the
previously completed loaded-admission-v7 controller, without changing its
physical geometry. The package is 304,458 bytes of compressed GDS and a
224,237-byte image, plus provenance. The packager checks the original GDS hash,
source snapshots, saved full-deck DRC/LVS and routed timing, and compares the
original native/routed traces again. The original native test executable has
been overwritten; its expected hash is explicitly recorded as unavailable.
No simulation or signoff is claimed to have been rerun by packaging. Seven
packaging regression cases reject changed sources/layouts, incomplete scope,
unbound evidence and mismatched replay. The component's recorded timing remains
+0.037747696/+0.064936139 ns, with zero DRC violations and matching transistor
LVS. Receiver timing, clock-tree integration and full-chip qualification remain
open; this is not a completed repeater GDSII.

## November 3 current physical block publication

The earlier standalone publication is superseded by
[`results/repeater_development.gds.gz`](results/repeater_development.gds.gz),
a single hierarchical collection of 21 currently available physical blocks,
and newly rendered overview/per-block PNGs. The receiver is exported from the
latest completed `receiver-square-lookup-revision/pin-repair/global.odb`.
This publishes failing geometry as-is without weakening qualification checks.

Equivalent-input pin repair completed in 339.02 seconds. Nominal global-route
setup/hold are **+0.412071288/+0.123619340 ns**; the enlarged-capacitance
estimates are **-0.661499321/+0.140293896 ns**. The margin view also reports
two clock-buffer capacitance violations. The continuation driver rejected this
candidate and retained the preceding data-repair checkpoint. The new GDS
publishes the latest pin candidate for inspection, not as an accepted timing
solution. Signal detailed routing is still pending for this revision.

The package also includes the newer clock-drift controller, with recorded
standalone extracted setup/hold **+0.018183399/+0.061225358 ns**, zero full-deck
DRC violations and matching LVS, plus the latest available analog, transmit,
clock, reset, flow-control/SRAM, core and padframe geometry. Geometry hashes
prove each block is preserved through merging and GDS round trip. Four
publication regression cases pass under CTest, including failing timing and
same-named cells with different geometry.

The assembly adds **no electrical connections between blocks** and includes
separate clock alternatives for review. It is not a chip floorplan or a full
PHY/ADC/DSP layout; the equalizer/echo canceller has no physical checkpoint to
include. Block dimensions and recorded checks are indexed in
[`results/README.md`](results/README.md). Full-chip timing, DRC and LVS remain
unfinished. Historical published files remain available in Git history.
