# Receiver routing resources

After the reachability candidate and its output-repair diagnostics completed,
29 inactive artifact groups were losslessly compressed, recovering 682.6 MiB.
`receiver-reachability-revision/archived-superseded-reachability.json` records
original/archive checksums and restoration commands. Active square-lookup
inputs, source snapshots and reference traces were retained. Older diagnostic
checks require restoring their archived geometry/netlists first.

During the November 3 timing repairs, another 96 inactive artifact groups were
losslessly compressed, recovering 659.3 MiB. The receipt is
`receiver-onehot-revision/archived-nov3-repair-diagnostics.json`; it records
source/archive SHA256 checks and exact restoration commands, including hard
links. The two superseded output/branch diagnostics and older generated routing
artifacts were eligible; all paths bound by the active skew candidate were
excluded. Reference waveform `.dat`/`.csv` files were not selected. Restore an
archived file before running an older checker that expects its raw path.

The November 2 `early-route-200` diagnostic ended with signal 9 after
10,392 seconds. The kernel OOM record identifies OpenROAD at approximately
3.79 GiB RSS while a separate C++ compilation consumed approximately 1.13 GiB.
The host has 5.7 GiB RAM and nearly full swap. This was not a routing-timeout
failure. There is no routed database or extracted timing from that run.

The EDA memory lock serializes participating tools; independent processes
which do not use `tool_process.py` can still compete for memory. Retrying an
identical allocation pattern would leave the same failure mode open.

`receiver_route_probe.py`, `resume_receiver_route.py` and `receiver_continue.py`
accept `--allocator /absolute/path/libjemalloc.so.2`. The optional configuration
uses one arena, immediate dirty/muzzy-page purging and no retained virtual
extents. These are allocator controls, not router or electrical relaxations.
The [jemalloc manual](https://jemalloc.net/jemalloc.3.html) describes them.
The run hashes the selected shared library and records the configuration.
Inherited `LD_PRELOAD` and `MALLOC_CONF` do not silently change that selection.
`/usr/bin/time` records each stage's peak resident memory, page faults and elapsed
time in `*.resources`. The full receiver probe subsequently completed with
3,808,412 KiB peak RSS (about 3.63 GiB). This is lower than the earlier OOM
snapshot, but the runs experienced different concurrent workloads; it does
not establish that allocator selection alone prevents future OOM failures.

`pdk/1G_repeater_IHPSG13G2/tool_packages.tsv` pins Ubuntu's
`libjemalloc2` 5.2.1-4ubuntu1 by SHA256. The existing `prepare_tools.py` downloads
and extracts it into the local tool directory without a system installation.
The OpenROAD wrapper and executable remain unchanged.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_route_probe.py \
    --seed build/clock-integration/trellis-compressor-revision/global-unbuffer-repair-200 \
    --output build/receiver-route-retry \
    --allocator out/repeater-toolchain/root/usr/lib/x86_64-linux-gnu/libjemalloc.so.2
ctest --test-dir build/behavior --output-on-failure \
    -R 'ihp_receiver.(route_allocator_native|allocator_guards|route_probe_guards|route_resume_guards|continuation_guards)$'
```

The allocator native test runs actual IHP early/full routing and extraction,
checks exact endpoints, setup/hold and electrical limits, and verifies that
the library is bound to the early-route receipt. It passes with the selected
allocator. The full receiver retry and checked continuation are under
`trellis-compressor-revision/early-route-200-jemalloc/` and
`route-continuation-200-jemalloc/`. The probe finished after 9,740 seconds plus
93 seconds for extraction, with setup -0.928154469 ns, hold +0.120607637 ns,
electrical violations and 17,335 router violations. The continuation rejected
the setup failure. Resource completion does not qualify the receiver.

For a changed receiver netlist, `physical_module.py --module PCSReceiver
--global-route-only` stops after placement, CTS, global routing and repair.
The `global_checkpoint_only` receipt retains estimated setup/hold and electrical
results, including failures, with all qualification flags false. Missing-pin
route-estimation warnings reject the checkpoint. This permits inspection before
another multi-hour detailed-route attempt. Omit the option to run the full
physical flow; this stop mode does not establish extracted timing or clean GDSII.

Retired run artifacts were losslessly compressed with SHA256 verification and
restoration commands in `retired-receiver-retry-archives.json` and
`retired-old-receiver-pairs.json`. The latter preserves restoration of three
old checkpoints and their hard-linked copies. Inputs needed by the active
global candidate remain available and hash-verified. This cleanup recovers
approximately 0.84 GiB; it does not delete reference waveforms.

The November 3 powered-cell hold-trim regression exposed a protection bug:
checking every terminal for special nets caused VDD/VSS to exclude all actual
receiver cells. `receiver_hold_trim.tcl` now excludes POWER/GROUND terminals
from the signal protection test; fixed cells, dont-touch cells and protected
signal/clock nets remain excluded. The fixture connects both supply rails and
protects one delay cell, then checks accepted changes, exact rejected rollback,
rail preservation, endpoint equivalence and an independent timing reload.

Hold trimming now shares a bounded trial allowance across up to three passes,
reselecting critical paths after each pass. The command-line controls are
`--hold-trials` (default 18) and `--hold-seconds` (default 600). The time limit
stops new trials; a running RC update, legalization and final verification are
allowed to finish. The enclosing process still has its 1,800-second deadline.
The saved candidate and receipt can seed another bounded run. Every replacement
and rollback retains a complete global-route RC refresh; only the redundant
refresh immediately before an already-refreshed trial was removed. The native
hold-trim regression also checks that a one-trial limit is respected.

Additional inactive guides, old diagnostic GDS/DEF and LVS databases were
losslessly compressed with verified round-trip hashes. Receipts under
`build/clock-integration/receiver-onehot-revision/` are
`retired-inactive-route-guides.json` and
`archived-inactive-physical-artifacts.json`; the latter includes restoration of
hard-linked copies. They recovered approximately 433 MiB and 1,149 MiB,
respectively. Retained older evidence must be restored at its recorded path
before rerunning a checker that requires its uncompressed file. Current receiver
seed files and ngspice reference waveforms were not compressed by this cleanup.

`receiver_global_unbuffer.py --buffer-sinks 4` additionally permits an explicitly
selected data buffer to drive three or four local sinks. The default remains
two. The source net must still have exactly one load, register loading stays
protected, and the 200 um span cap and topology/clock/protection exclusions
remain in force. This changes the candidate search, not the design's maximum
fanout, capacitance or transition constraints. The new
`analog.1g.ihp_receiver.global_unbuffer_branch_native` CTest removes an actual
three-sink IHP buffer and checks exact endpoints, electrical/hold/setup limits
and independently reloaded routing estimates. The OpenDB selection regression
also checks the explicit sink bound and rejects invalid limits.

`archived-retired-receiver-checkpoints.json` records a further 677 MiB recovered
from superseded receiver ODB/SPEF checkpoints, including older placement states
of this revision and the no-op hold run. Original byte hashes, verified gzip
archives and hard-link restoration commands are retained. The live buffer
candidate, its immediate seed, mapped C++RTL-reference fixtures and all inputs
needed by the active route/continuation remain uncompressed and available.

`archived-retired-small-artifacts.json` records another 1,607.4 MiB recovered
from inactive regeneration-audit and older extracted-repair artifacts. All
263 archive groups have verified round-trip content hashes and restoration
commands. Existing receipts remain available. Current receiver inputs and
the new routed measurement, sizing and repair artifacts were excluded.

The powered-branch early route completed but failed extracted setup. Its
`receiver_routed_seed.py --probe-origin ...` reload reproduces the timing
before `receiver_path_sizing.py` evaluates equivalent cell replacements. This
avoids promoting the earlier positive global estimate to physical closure.
The next post-sizing stage now isolates overloaded single-strength outputs
before generic repair and checks for residual overloads afterward. Each
topology change is legalized and followed by rebuilt routes and RC. The
existing 1.75x wire-capacitance optimization screen and original acceptance
limits are retained.

```sh
ctest --test-dir build/behavior --output-on-failure \
    -R '^analog\.1g\.ihp_receiver\.(final_repair_native|final_isolation_native|rebuffer_native)$'
```

The isolation fixture uses a real `sg13g2_a221oi_1` driving an excessive load,
requires an explicitly inserted output buffer, proves unchanged logical
endpoints and passes nominal/enlarged-C timing and electrical screens.
The three focused tests passed in 10.97 seconds. An earlier 108-second test
duration included waiting for the full receiver sizing job's memory slot;
the same test subsequently ran in 2.88 seconds with the slot available.

After the one-hot receiver's repair continuations stopped on failed timing,
`archived-nov3-repair-diagnostics.json` recovered 659.3 MiB from 96 inactive
artifact groups. `archived-superseded-onehot.json` then recovered another
2,239.1 MiB from 78 groups in the superseded one-hot revision. Both manifests
retain byte hashes, verified gzip round trips and hard-link restoration
instructions. Source snapshots, receipts and reference waveform data remain
available. The new reachability revision and its active tests were excluded;
free disk space after archival was approximately 4.9 GiB.
