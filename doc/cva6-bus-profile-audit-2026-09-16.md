# Why the small CVA6 bus replay is slow

## Conclusion

The slowdown is real, but it is not primarily trace handling, imported replay
context, or Verilator skipping idle cycles. Two costs are independently measured:

1. **Repeated evaluation of unchanged combinational producers.** Four request
   trees execute 88,939,364 times for one 46,264-cycle trace. Completed results
   never change between calls within the same epoch on this workload. Caching
   only these four bodies reduces total runtime by **23.7%**.
2. **Expensive packed-field transport.** Slice writeback accounts for
   **34.4–35.1% of exclusive CPU samples**. Replacing its partial-byte endpoint
   bit loops with masked byte writes reduces total runtime by **12.0%**, without
   changing scheduling or the generated hardware graph.

These are separate diagnostic experiments, not a combined speedup or production
fix. Even the request-cache experiment remains 78.7× slower than this native
cut-out. The rest of the packing, array access and generated evaluator work
still matters. All tool sources and original benchmark binaries are unchanged.

## Controlled timings

Three alternating-order trials per variant, serially pinned to CPU 2. Original
RTL, production parameters, trace and Clang `-O2` are unchanged. Native frontend
flags match the production comparison. Every hardware variant checks all 4,052
output bits over all 46,264 cycles before timing, then consumes all timed outputs.
All hardware trials report checksum `0261761b2a1c459e` and 46,264 checked cycles.

| Variant | Median work time | Individual trials, seconds | Runtime reduction vs cpphdl baseline |
| --- | ---: | --- | ---: |
| Original Verilator replay | **0.167225 s** | 0.167679 / 0.167225 / 0.165605 | reference |
| Original cpphdl L1 + context | **17.241728 s** | 17.278392 / 17.088499 / 17.241728 | — |
| Cache four request-tree bodies | **13.152857 s** | 13.381260 / 12.998177 / 13.152857 | **23.715%** |
| Masked-byte writeback endpoints | **15.170543 s** | 15.230633 / 15.024622 / 15.170543 | **12.013%** |
| Regenerate without imported context | **17.144066 s** | 17.370281 / 17.087258 / 17.144066 | 0.566%, not meaningful |

The baseline gap is **103.1×** for this bus-only workload. The last row is
rejected as an optimization: its difference is below the observed spread and
the requested 10% threshold. No baseline source or executable was replaced.

A separate **no-hardware driver control**, copying the same input bundles and
hashing captured expected outputs, takes 0.006351 s median. This is a diagnostic
overhead bound, not another simulator result; it does not calculate hardware
outputs. It is only 0.037% of cpphdl's work time. Timed PC samples independently
place the actual cpphdl driver at 0.06–0.07% of execution.

## 1. Fresh work-phase PC and stack profiles

Unlike the previous whole-process profiles, the sampling hooks start **after**
trace loading, input preparation, model construction and the reference checker.
They stop immediately after the timed loop. Profiling and counters are kept
separate from the uninstrumented timing table.

| Exclusive PC category | Run 1, 14,269 samples | Run 2, 14,830 samples |
| --- | ---: | ---: |
| Packed slice writeback | 34.38% | 35.09% |
| Four request-tree bodies | 18.14% | 17.94% |
| Packed slice reads | 2.66% | 2.51% |
| Driver | 0.06% | 0.07% |

The first profile's remainder includes 27.47% in other generated evaluators,
11.70% in other cpphdl helpers, 1.77% in generated work, and 3.81% in other or
unresolved symbols. These are exclusive categories; do not add inclusive caller
costs to this table.

An independent stack-sampling run collects 15,925 samples. The interrupted PC
and at least one caller are recovered in every sample; individual frames can
still be inlined. It reproduces 34.95% writeback and 17.96% request-tree self-time.
Caller attribution connects expensive stores to real bus payload production:

- `logic_bits<135>::updateParent`: prominently demux 0/1
  `mst_reqs_o_aw_comb`, evaluators 2464/2465.
- `logic_bits<129>::updateParent`: prominently demux 0/1
  `mst_reqs_o_ar_comb`, evaluators 4386/4396.
- 130-, 136-, 104-, 105-, 148- and 374-bit writebacks also occur in interface
  packing, data selection and output assembly, including `master_outputs_comb`.

This is not an RF array-index problem hiding behind a similar timing ratio.
The expanded replay now exercises the bus's actual packed structures and
consumer fanout. The remaining helper profile includes struct `pack()`,
packed-array `operator[]`, struct assignment and slice extraction.

## 2. Dynamic counts and result-stability checks

Counters observe the **completed result at function exit** during the timed
trace, not partial assignments inside a producer:

| Request tree | Evaluator | Calls | Observed epochs | Result changes within an epoch |
| --- | ---: | ---: | ---: | ---: |
| Demux 0 B | 3380 | 22,156,641 | 46,278 | **0** |
| Demux 1 B | 3394 | 22,120,856 | 46,278 | **0** |
| Demux 0 R | 4331 | 22,541,011 | 46,278 | **0** |
| Demux 1 R | 4345 | 22,120,856 | 46,278 | **0** |

There are 14 additional asynchronous-reset settling epochs, not extra simulated
trace cycles. Normal producer counts are 478 per epoch; some demux-0 epochs have
563 calls. Reset epochs have 477. Detailed histograms are retained.

The caching experiment changes only these four generated function entries to
return their existing result after the first call for that model/epoch. It
retains original RTL computations and validates the entire trace. Its 23.7%
runtime reduction exceeds the trees' ~18% self-time because it also removes
upstream work: request-input evaluators 81 and 149 alone contribute about 4.3%
of baseline exclusive samples.

**This is workload evidence, not a general caching proof.** The diagnostic uses
explicitly selected evaluator IDs and is not an acceptable automatic compiler
fix. A production change must establish complete writes, dependency stability
and the appropriate evaluation phase for arbitrary inputs, then pass full CVA6.

An initial probe was accidentally inserted at a nested closing brace inside a
partial write group. It appeared to show changing results. Inspecting the
insertion point caught this instrumentation error; that log is explicitly saved
as `count/check.partial-probe-rejected.log` and excluded from conclusions. The
corrected probe is after the whole producer body; all four counts above are
from the corrected rerun. The earlier apparent instability is withdrawn.

### Why existing optimization misses the reuse

`Combs.cpp::memoizableProceduralComb` recognizes a producer whose first target
occurrence is a whole-result assignment, followed by local field updates. These
request trees instead completely fill their result through indexed writes and
guarded generate-loop groups. The current proof cannot accept that pattern.
Ordinary procedural comb semantics therefore retain repeated demand evaluation.

The generated request tree already performs its internal reduction bottom-up.
The small graph reports zero lazy cycle back-edges. It is not justified to blame
an unbalanced reduction or cyclic scheduling just from the large call count.
The missing proof/representation of indexed writes is a concrete obstacle to
producer reuse; adding another broad scheduler does not by itself remove it.

## 3. Packed writeback control and disassembly

`include/cpphdl_logic.h::logic_bits::updateParent` already copies the middle of
a slice bytewise. Its unaligned head and tail still use per-bit `get`/`set`
operations. The diagnostic changes **only those endpoints** into masked byte
operations, retaining the same bytewise middle copy and all simulation work.

All generated cpphdl translation units are rebuilt against an isolated copy of
the headers, avoiding mixed definitions. The generated schedule is unchanged.
A separate bit-reference test passes **105,285 cases**, covering all ranges at
selected widths through 148 bits and random/boundary ranges at 292, 374 and
4,114 bits. The entire bus output trace also passes in every timed trial.

Disassembly confirms that `logic_bits<135>::updateParent` shrinks from **950 to
335 machine-code bytes**. Baseline code contains repeated shifts, masks and
read-modify-write sequences for boundary bits. The resulting whole-replay
12.0% runtime reduction establishes a real cost, but does not eliminate the
other packing layers or prove a full-CVA6 gain.

## 4. Controls that reject tempting explanations

**Imported context causes the 100× gap:** not supported. Regenerating without
the import changes 268 scheduled / 4,864 dynamic values/evaluators to 294 / 4,838,
and dynamic states from 5,739 to 5,713. Both have 344 instances and zero lazy
back-edges. The no-context binary still takes 17.14 s and matches every output.

**Verilator mostly skips the workload:** not supported. Work-phase entry counts
for its dominant generated functions are:

- `ico_sequent__TOP__0`: 92,528 calls, exactly twice per simulated cycle.
- `ico_sequent__TOP__1`: 138,793 calls, approximately three per cycle.
- `nba_sequent__TOP__1`: 46,264 calls, exactly once per cycle.

These are larger kernels, not one-to-one request-tree functions, so their counts
must not be presented as a direct speedup factor. But the native model executes
substantial generated logic every cycle. Its input-combinational trigger code
explicitly enables the first iteration of each evaluation. Its scalar/word
representation and shared expressions avoid the hundreds of producer traversals
seen in cpphdl.

**I/O, swapping or CPU starvation:** not supported by the runs. `/usr/bin/time`
records 99% CPU utilization, no major faults, no swaps and zero filesystem input.
The first cpphdl process uses 34.71 s user and 0.07 s system time, over 34.79 s
wall time. These whole-process numbers include both checking and timing; they
are not substituted for the 17.24 s work timer. RSS is about 58 MB for cpphdl and
55 MB for native, mostly including the trace and prepared inputs.

**Instruction-cache misses explain the gap:** not measured. Static executable
text is 3,541,518 bytes for cpphdl versus 317,527 bytes for native, but code size
alone is not a cache-miss measurement. `perf stat` cannot access even task-clock
events under this host's `perf_event_paranoid=4`. No global security settings
were changed; no hardware-counter conclusion is claimed.

## Next compiler work and limits

The evidence favors extending the existing complete-write/dependency analysis
to indexed combinational producers, coupled with lowering packed field traffic
to direct masked native storage operations. That addresses proven costs without
handwritten CVA6 hardware or an additional competing scheduler. A LUT for the
request tree alone does not remove the separately measured field-transport work.

Neither diagnostic is promoted to production here. The two gains are measured
independently and must not be added or treated as a measured combined result.
The remaining ~79× gap after caching four trees is explicit evidence that this
is not a complete route to Verilator speed by itself.

This cut-out is still not a numerical whole-CVA6 performance surrogate. It lacks
CSR/RVFI, scoreboard and cache cones, and does not reproduce the original full
native build's dominant request-copy functions. Keep full-model correctness and
timing as the acceptance gate for an automatic optimization.

## Reproduction

Artifacts are in `build/bus-profile-audit-20260915/`:

- `build.py`, `Probe.c`, `profile.py`, `time.py`: isolated experiment recipes.
- `profiles.json`, `*-pc-*.bin`, `sample-stack-0.bin`: timed-region profiles.
- `count/check.log`, `native-count/check.log`: corrected dynamic counts.
- `summary.json`, `timings.json`, `timing-inputs.json`, `time-*.log`,
  `usage-*.log`: all individual timing/checksum/resource results and hashes.
- `writeback-body.inc`, `WritebackCheck.cc`, `writeback-*.asm`: writeback control
  and independent checker/disassembly.
- `no-context/generate-command.json`, `no-context/generate.log`: schedule control.
- `perf-availability.log`: hardware profiling restriction.

Production input and executable manifests were verified unchanged. Analysis
variants are confined to scratch build directories; failed hypotheses and the
sub-10% no-context experiment do not alter the production flow.
