# Paired execution profiling of the native-layout CVA6 bus replay

## Scope and reproducibility

This profiles the retained native-layout executable from September 17 against
the frozen original-SV Verilator bus replay. Neither simulator was rebuilt,
no hardware block was replaced, and full CVA6 was not regenerated. Imported
full-design demand context remains in the cpphdl executable. This measures the
small crossbar replay, not full integer-matmul execution.

The new reusable entry point is
`hdlcpp/tests/cva6/profile_context_replay.py`; usage and platform requirements
are in `hdlcpp/tests/cva6/context_replay/README.md`. All generated artifacts,
including the downloaded/extracted Valgrind package, are outside the repository:

```
/home/me/cpphdl-validation-20260918/profile/paired-v2/
```

`inputs.json` fingerprints the two executables, trace, and profiler sources.
`commands.json` records the exact commands and profiling environment overrides.
`results.json` retains all individual results; `summary.json` normalizes costs
per simulated cycle. Raw PC samples, loaded-module ranges, symbol reports,
Callgrind call graphs, and logs are retained alongside them. Build provenance
remains in the September 17 native-layout audit and its final-build manifest.

Reproduction command:

```sh
python3 -B /home/me/cpphdl/hdlcpp/tests/cva6/profile_context_replay.py \
  --cpphdl /home/me/cpphdl-validation-20260917/native-final/current-run \
  --verilator /home/me/cpphdl-validation-20260916/previous-audit/bus-v3/verilator/VXbarBench \
  --trace /home/me/cpphdl-validation-20260916/small/capture-matched/matmul-bus.bin \
  --output /external/new-profile-directory --cpu 2 \
  --valgrind /home/me/cpphdl-validation-20260918/profile/tools/root/usr/bin/valgrind \
  --valgrind-include /home/me/cpphdl-validation-20260918/profile/tools/root/usr/include \
  --valgrind-lib /home/me/cpphdl-validation-20260918/profile/tools/root/usr/libexec/valgrind
```

## Measurement contract

Every executable invocation first checks all 4,052 logical output bits on all
46,264 captured cycles. This validation, trace parsing, input preparation, and
model construction are outside the timer and profiling window. Timed work
includes input assignment, evaluation, state commit, output copies, and checksum
accumulation. The Verilator fixture source is byte-identical to the production
`BusRun.cc`; the model reports one simulation thread.

Three distinct measurements are kept separate:

1. Five uninstrumented, serial, alternating-order CPU-2-pinned timing trials.
2. Native PC sampling at 1 ms and 7 ms periods. Each simulator executes 20 full
   repetitions per run: 925,280 timed cycles. Both periods independently give
   essentially the same coarse cost distribution.
3. Callgrind instruction/reference/call counts and cache/branch simulation for
   one complete timed replay per simulator. Instrumented elapsed time is never
   used as a speed comparison.

The sampler is an external preload library; it does not require debug builds,
frame-pointer changes, or per-cycle hooks. The gate intercepts libstdc++'s
`steady_clock::now` only when its caller is in the driver's `main` symbol.
It requires exactly two matching calls and rejects sample overflow.

An initial process-wide `clock_gettime` pilot was rejected: Verilator also reads
clocks during initialization, before the benchmark interval. Its aborted profile
is **not** included in any result. The final gate is independently tested with
a clock-reading, CPU-intensive checker outside the interval and a distinct work
function inside it. Both native sampling and a separate Callgrind probe capture
only the work function, never the checker.

Hardware `perf` counters are unavailable (`perf_event_paranoid=4`, actual
`perf stat` permission failure saved in `profile/perf-permission.log`). Cache and
branch misses below are consequently **simulated**, not measurements of this
machine's hardware misses, IPC, bandwidth, or stall cycles.

## Uninstrumented speed

| Simulator | Median seconds | Minimum–maximum seconds | Timed cycles |
| --- | ---: | ---: | ---: |
| Verilator | 0.189406501 | 0.178452321–0.380873215 | 46,264 |
| cpphdl native layout | 5.390477459 | 5.261297583–5.440754466 | 46,264 |

The ratio is **28.46x**, consistent with the previous audit's 28.7x. Absolute
times vary on this shared KVM host; the first Verilator trial is a visible
outlier and is retained, not discarded. No performance improvement is claimed
in this profiling-only change.

All single-repeat runs return `0261761b2a1c459e`; all 20-repeat sampled runs
return `3fdca667a4a71b50`. Comparisons reject differing output checksums or
cycle/check counts. Preparation and compilation time are not included.

## Dynamic work: the same 46,264 cycles

| Metric per simulated cycle | Verilator | cpphdl | Ratio |
| --- | ---: | ---: | ---: |
| Executed instruction references | 33,517 | 572,463 | 17.08x |
| Data reads + writes (references, not bytes) | 13,132 | 189,800 | 14.45x |
| Out-of-line calls recorded by Callgrind | 55.00 | 4,253.64 | 77.34x |
| Conditional + indirect branches | 1,112.84 | 49,802.56 | 44.75x |
| Simulated L1 instruction misses | 2,872.99 | 11,030.58 | 3.84x |
| Simulated L1 data misses | 4.50 | 3,721.27 | 826.49x |
| Simulated branch mispredictions | 32.27 | 3,134.10 | 97.11x |
| Simulated last-level data misses | 4.50 | 4.64 | 1.03x |

The cache model uses 32 KiB, 8-way L1 instruction/data caches and a 16 MiB
direct-mapped last-level cache, with 64-byte lines. These are Valgrind model
parameters, not a validated model of the host's complete memory hierarchy.
In particular, this does **not** show a 826x increase in DRAM traffic. The
last-level data-miss counts are nearly equal.

The instruction count alone establishes substantial excess executed work, not
a compilation-time problem. The 28.46x runtime ratio divided by the 17.08x
instruction ratio is about 1.67. That remaining per-instruction cost difference
cannot be assigned precisely to actual cache stalls or branch penalties without
hardware counters. The simulations identify plausible pressure points, not an
exact additive accounting of lost seconds.

Callgrind assigns 324,142 instructions/cycle (56.62%) to out-of-line datatype
helpers, and 197,094 (34.43%) to demand evaluator bodies. These include the
helpers' and evaluators' own instructions only, not their callees' costs.
The call total counts surviving compiled calls, not every generated producer;
inlined producers are not separately counted.

Concrete repeated work in the final executable:

* The two incoming request bundles undergo about **62 full `slv_req_t` decodes
  per cycle**, distributed across separate field consumers, rather than two
  shared decodes. The ten response bundles undergo about **110 full decodes**.
  `eval_1957` alone accounts for approximately 50 response decodes/cycle.
* `eval_4755` calls unpacking of a 640-bit vector into ten 64-bit elements about
  three times/cycle. That executes the bit-copy slice helper 30 times/cycle,
  at **1,496 instructions/call**. This one helper executes about 44,894
  instructions/cycle: more than the entire Verilator replay per cycle.
* Four hot grant-tree evaluators (`6244`, `6263`, `7167`, `7186`) each execute
  about **11 times/cycle**, each costing approximately 1,146 instructions/call.
  This is directly counted in this native build, not reused from an old build.
* There are approximately **611 `memset` and 374 `memcpy` calls/cycle** in
  cpphdl, versus effectively zero and two respectively in Verilator. These
  are library calls; inlined clearing/copying is additional.

High indirect-branch counts do not establish a `std::function` bottleneck.
The recorded hot functions instead include packed-array accessors and field
dispatch evaluators. Disassembly shows PLT calls to small `memcpy`/`memset`
operations in the four-bit array accessor, and indexed jump tables in
`eval_1957`. No named `std::_Function` body has recorded instruction cost here.
The raw call edges and exclusive costs are available in `*-symbols.json`;
the derived ranking is in `paired-v2/evidence.json`.

## What sampling establishes

The two periods respectively put 57.75% / 57.72% of cpphdl samples in generated
demand evaluators, 30.71% / 30.87% in out-of-line datatype/conversion helpers,
and 5.85% / 5.52% in scheduled work bodies. Register commit is about 0.42%.
Verilator places 92.72% / 92.18% in its generated model functions.

These are **exclusive symbol buckets**, not a decomposition into abstract
compiler passes. In particular, the evaluator bucket includes inlined
arithmetic, packing, copying, and dependency checks. It does not prove that
timestamp checks alone account for 58% of execution.

The known out-of-line helper bucket alone corresponds to approximately 1.66 s
of the 5.39 s cpphdl replay: almost nine complete Verilator replays. Conversely,
even deleting that entire bucket for free would still leave roughly a 20x gap.
These are sampling-based estimates, not measured ablation speedups.

The driver's own symbol is 0.19% / 0.23% for cpphdl and 3.38% / 3.81% for
Verilator. Sampling therefore does not support blaming trace parsing or the
common checksum for the large gap; trace parsing is outside the gate anyway.

## Source and machine-code cross-check

One concrete remaining representation cost is
`cpphdl::logic<640>::bits(last, first) const`. It constructs an 80-byte result
and copies a slice **one bit at a time**. The frozen executable's disassembly
contains the byte load, bit test, byte read/modify/write, and loop branch for
each selected bit. The loop is not merely verbose generated C++ optimized away
by Clang. The corresponding 320-bit specialization also remains hot. The
disassembly is saved as `profile/logic640-bits.asm`.

The cpphdl evaluator for `mst_reqs_comb[0]` (`eval_4755`) visits individual
field producers through timestamp checks and pointer slots. Other hot producers
construct arrays of fields, extract slices, and reconstruct typed structures.
Native aggregate storage removes some pack/unpack round trips but does not
eliminate these internal serialized field vectors and demand boundaries.

The measured Verilator executable instead spends most time in a few straight-line
generated `ico`/`nba` bodies using native scalar and word operations. Local
Verilator sources explain the relevant architecture:
`V3DfgDfgToAst.cpp` shares multi-consumer values and removes redundant variable
representations; `V3Sched.cpp` builds phase/trigger regions. This is source-level
context for the measurements, not a claim that Verilator computes every node
exactly once per cycle.

The executable `size` text totals are 3,646,007 bytes for cpphdl and 317,527
bytes for Verilator (11.48x). These include runtime/template code and are not
the hot instruction working sets. They alone do not prove an instruction-cache
bottleneck.

Separate `sizeof` probes compiled against the frozen model headers establish a
large persistent object-layout difference:

| Object | Bytes |
| --- | ---: |
| cpphdl `XbarRoot` | 1,172,032 |
| cpphdl optimized state/cache object | 82,976 |
| Verilator root | 2,112 |
| Verilator symbol table containing root and both interface cells | 2,496 |

Thus cpphdl's root plus cache occupies 1,255,008 bytes versus 2,496 bytes for
Verilator's corresponding symbol-table/model objects. This approximately 503x
ratio is **not** total process memory or a measured hot working set: it excludes
stack temporaries, input traces, additional heap allocations, and runtime
context objects. Verilator's generated functions keep much combinational work
in temporary native values instead of retaining every hierarchical C++ port,
intermediate, and cache slot in the persistent model. The original interface
objects and field copies remain in cpphdl even after native-array lowering.
The probes and outputs are in `profile/ModelSize.cc` and
`profile/model-sizes.txt`; runtime/generated header hashes match the frozen
September 17 manifest.

## Next change justified by this evidence

The target is **shared, native field values in compact phase-local execution**,
not another cache flag on the existing hierarchy:

1. Recognize generated field projections/pack/unpack adapters as one value
   graph. Decode a boundary bundle once in its valid phase and let field
   consumers share that result or directly access its native words. Do not
   reconstruct the entire bundle separately for each requested field.
2. Emit only live register state and necessary shared temporaries into the
   optimized model. Do not retain the original object hierarchy's unused
   ports and intermediate representations merely to address a few fields.
3. Share proven-complete producers within the correct phase; preserve reset,
   ordering, partial-write, and cyclic dependency semantics. The 11x grant
   multiplicity identifies an audit target, not permission to blindly memoize.

Native aggregate storage is a useful first step, but these counts show why it
is not enough. Merely optimizing one slice function or deleting a few forwarding
timestamp slots cannot close this gap. A future change should be accepted only
after the same checked replay shows reduced dynamic work and a reproducible
runtime gain of at least 10%, with the imported context retained.

## Validation

The profiling parser and native-gate tests pass, as do the existing 20 Python
replay/measurement/trace tests. A separate external Callgrind probe confirms
that the checker is excluded and the timed work function is called once.
The source change adds profiling tools/tests/docs only; it does not modify
production simulator semantics or silently enable a nonrepresentative schedule.

Callgrind bookkeeping was also audited: stopping instrumentation before an
explicit dump can leave the automatic file's `summary:` zero or underflowed,
although its per-function rows and `totals:` retain the costs. The paired cache
run used verified sums of exclusive rows equal to `totals:`, not that header.
The final profiler now dumps **before** stopping and consumes `.callgrind.1`.
With cache simulation, Callgrind can charge the final client-request block to
the global summary before assigning it to a function (9 instructions in the
end-to-end smoke test). The parser records this boundary delta, permits at most
64 nonnegative references per event, and rejects larger/underflowed discrepancies.
Exclusive row sums must still equal `totals:` exactly. The measured profiler sources
are preserved externally as `WorkProfile-measured.c` and
`profile_context_replay-measured.py`; the final gate is independently rerun on
the full checked traces without cache simulation to cross-check instruction
and call counts. `profile/final-gate-validation.json` records those commands,
hashes, and comparisons.

Both full-trace cross-checks pass: the final gate records exactly **11 fewer
instructions** than the original gate in each simulator, with **identical call
counts** and matching outputs. This fixed profiler-boundary difference is
negligible beside 1,550,619,866 versus 26,484,430,396 instructions. The new
end-to-end tool is also smoke-tested on a 512-cycle checked prefix; those timings
are validation only and are not included in the performance tables.
