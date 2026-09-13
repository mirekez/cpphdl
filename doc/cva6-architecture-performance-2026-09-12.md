# Why CppHDL trails Verilator: measured architecture investigation

The previous round concentrated on conversion time and individual runtime helpers.
That did not explain the simulator's architecture-level gap. This investigation
profiles both simulators, counts generated evaluations, and changes representation
and scheduling in isolated experimental binaries. Production sources and the
production harness binary are not replaced by these experiments.

## Main result

Five alternating trials, same matrix ELF, 5,000-cycle cap, process wall time:

| Model | Median seconds | Relative to Verilator |
| --- | ---: | ---: |
| CppHDL baseline saved at the start of this investigation | 7.2380 | 10.42x |
| Prototype: two typed array copies and four native request-tree functions | 4.0962 | 5.89x |
| Existing Verilator CVA6 example, fixed seed 1 | 0.6949 | 1x |

The prototype reduces elapsed time by **43.4%**, a **1.77x speedup**. This is a
representation experiment, not an allocator/compiler-flag optimization. It still
does not reach whole-model Verilator speed.

### Startup does not explain CppHDL's cost

Two further trials at each cycle cap separate startup from advancing the model:

| Model | 0-cycle cap, seconds | 1,000 cycles | 10,000 cycles |
| --- | ---: | ---: | ---: |
| Baseline | 0.0203 | 1.4701 | 14.6293 |
| Prototype | 0.0159 | 0.8255 | 7.9720 |
| Verilator | 0.3210 | 0.4032 | 0.9279 |

The 1,000-to-10,000-cycle slopes are approximately **1,462 microseconds per
cycle** for baseline, **794 microseconds** for the prototype,
and **58.3 microseconds** for Verilator. Thus the estimated advancing-model gap
is still **13.6x** for the prototype. The short-run 5.89x wall-time ratio partly
hides the gap behind Verilator's larger initialization cost. These slopes remain
subject to the different architectural activity noted below.

Peak RSS in the 10,000-cycle runs is approximately 33 MiB for baseline, 34 MiB
for the prototype and 520 MiB for Verilator. Similar declared root sizes do not
mean equal resident memory: the models initialize/touch different storage.

All baseline/prototype runs have identical output hashes and timeout status.
**Both CppHDL variants still report zero commits**, also confirmed with commit
tracing enabled on the prototype. Matching these logs is not
architectural equivalence: Verilator can complete the program, whereas the
converted core has not demonstrated that. These whole-model figures measure the
cost of advancing the existing models, not equivalent retired-instruction work.

## The concrete bottlenecks

### 1. Repeating a combinational producer instead of sharing its result

Instrumenting generated dynamic partitions 5 and 8 found **477,477 calls each**
to request-tree evaluators 4321, 8013, 8033 and 9251 in a 1,000-cycle capped run.
There are 1,001 model evaluations including initial evaluation: each of these
four producers executes **477 times per model evaluation**. These two partitions
alone recorded 2,519,995 dynamic-evaluator calls across 548 reached functions.
Instrumented-run times are not used as performance measurements.

These are the two AXI demultiplexers' B/R request trees. Each actual instance has
11 request input bits and a 15-bit internal tree. Its complete result can instead
be computed with native integer operations, once as needed, or looked up.

The optimizer explains why this happens. `Combs.cpp:3350`
(`memoizableProceduralComb`) requires the first use of result storage to be a
whole-result assignment. The generated tree starts with indexed bit writes, so
this conservative proof misses it even though the complete tree is assigned in
dependency order. `Combs.cpp:3438` then leaves it repeatable. The dynamic-demand
logic around `Combs.cpp:4700` intentionally preserves source-method evaluation
and cached-value ordering rather than imposing an independently proven RTL
schedule. Blindly memoizing every method would therefore be unsafe.

### 2. Expensive packed-value representation, not expensive Boolean equations

Statistical CPU profiling of the baseline, 25,599 resolved/unresolved samples:

| Sample category | Share |
| --- | ---: |
| Out-of-line `logic_bits::updateParent` | 49.2% |
| Generated dynamic comb bodies | 25.6% |
| Array operations | 9.3% |
| PMP structure methods | 4.6% |
| Configuration builder | 2.2% |
| Work chunks | 2.0% |
| Strobe chunks | 0.3% |

These are exclusive symbol categories, not inclusive call-tree costs. For example,
array and comb work also calls the separately counted slice helpers.

`include/cpphdl_logic.h:540` makes `logic_bits<WIDTH>` inherit the full parent-width
value. Its constructor materializes the selected data and clears the remainder;
writeback then merges data into the parent. `include/cpphdl_array.h:118` builds
packed-array proxies on that representation. Many operations on a small field
thus pass through generalized packed containers and proxies rather than a direct
word/field operation. After the prototype changes, wide proxy/writeback operations
still account for **42.7%** of its CPU samples, including widths 6122, 136 and 271.
In the separately profiled Verilator run, 87.6% of samples fall in scheduled HDL
comb/sequential bodies rather than generic value-container helpers.

Re-emitting the existing writeback helpers at `-O3` did **not** improve the full
model. Merely optimizing the same representation harder is not the answer.

### 3. A real array-copy semantic bug also creates excessive work

`include/cpphdl.h:462` recognizes an exact array source type, but not
`reg<that_array>`, which derives from that array. It falls into element broadcast
instead of copying the array value. For generated PMP and BHT copies this can
recursively pack the entire source while assigning destination elements.

Tests with distinct values, compared recursively by individual fields rather
than only packed-container equality, found:

| Copy | Original mismatched destination elements/rows | Direct typed copy |
| --- | ---: | ---: |
| 64-entry PMP configuration array | 63 | 0 |
| BHT array, 64 rows | 64 | 0 |

The prototype replaces just the two generated calls with a copy from the matching
array base subobject. It does not change packed-layout rules or broadcast of real
scalar inputs. This demonstrates both the semantic problem and its runtime cost;
a general runtime fix needs regression coverage for nested and register-wrapped
array types, followed by a complete rebuild.

## Native/LUT experiment against the actual Verilator RTL

A separate wrapper instantiates the unmodified CVA6 `rr_arb_tree.sv`, exposes its
16-input request tree, and is compiled with local Verilator 5.049-devel.
Both it and the extracted generated CppHDL function are checked against a separate
Boolean-tree oracle for every input. CppHDL additionally checks three prior result
patterns per input: **196,608 cases**, demonstrating independence from old storage.
The actual 11-input specialization passes another **6,144 cases**.

Matched GCC 15.2, `-O3` measurements; medians of three trials, 10 million identical
deterministic vectors per trial. Each timed variant produces checksum
`2cfac66cb2cae871`:

| Request-tree implementation | Seconds |
| --- | ---: |
| Extracted generated CppHDL code | 0.6201 |
| Verilator model evaluation | 0.2296 |
| Native topologically ordered integer expression | 0.1545 |
| Native packed updates | 0.1321 |
| 128-KiB lookup table | 0.1129 |

**The rewritten kernel can already exceed Verilator's speed.** This is a small
combinational kernel, not a full CVA6 result. The Verilator measurement includes
its model evaluation entry point; the hand-lowered kernels are direct functions.
That difference is precisely one of the representation/scheduling choices being
tested. Earlier Clang runs also passed, but are not substituted for this
same-compiler table.

A full decoder lookup indexed by every instruction/control bit would be
exponentially large. The request-tree table demonstrates the useful case: a
small, proven input domain. Once native lowering removes the local overhead, a
LUT adds very little to the full-model result.

## Full-model experiments and the 10% rule

Three trials per configuration; baseline order reversed in the middle round.
The two experiment series have their own contemporary controls. Negative gains
mean a slowdown. Decisions concern additional benefit, not reusing an earlier
change's gain to justify a small addition.

| Experiment | Median seconds | Interpretation |
| --- | ---: | --- |
| Series A baseline | 7.2768 | Reference |
| Re-emit unchanged slice helpers at O3 | 7.3575 | -1.1%; rejected |
| Series A same-binary original-tree control | 7.2868 | Reference |
| Four native request trees | 6.1374 | 15.8%; successful prototype |
| Four LUT request trees | 6.1052 | Under 1% beyond native; no LUT rollout |
| Memoize the four original trees | 6.1376 | 15.8%; diagnostic alternative |
| Memoization added to native trees | 6.1545 | No extra benefit; rejected |
| Memoization added to LUT trees | 6.0329 | Under 10% extra; rejected |
| Series B same-binary copy control | 7.2086 | Reference |
| Direct PMP array copy | 6.3291 | 12.2%; successful prototype |
| Direct BHT array copy | 6.0851 | 15.6%; successful prototype |
| Both array copies | 5.1927 | 28.0% total; each addition exceeds 10% |
| Both copies plus native trees | 4.0678 | 21.7% beyond both copies |
| Both copies plus memoized original trees | 3.9235 | Under 10% beyond native alternative |
| Both copies plus LUT trees | 4.0117 | Under 10% beyond native alternative |

The final five-trial comparison uses direct copies plus native trees, not the
smallest noisy number or a combination of subthreshold additions. Diagnostic
switches, instrumentation, helper overrides and memoization are not enabled in
the production model.

## What Verilator does differently

The local sources were inspected, rather than assuming that Verilator is just a
fast event loop:

- `/home/me/cva6/tools/verilator-new/src/V3Order.cpp`: dependency-graph ordering
  of statements across the netlist, including sequential ordering constraints.
- `V3Sched.cpp`: logic classification, combinational-cycle handling, settle and
  active/NBA regions, triggers and evaluation loops.
- `V3DfgOptimizer.cpp`, `V3Gate.cpp`, `V3Const.cpp`: dataflow simplification,
  deduplication, elimination and constant folding before C++ emission.
- `V3Table.cpp`: selectively replaces suitable logic with tables using input-bit,
  output-size and instruction-cost limits; it is not a universal LUT simulator.
- `/home/me/cva6/work-ver/Variane_testharness___024root__19.cpp`: the actual
  generated trigger guards and direct scalar/wide-word calculations.

The essential distinction is **optimizing HDL dependencies and values before
emitting C++**, versus preserving a graph of C++ methods, mutable packed results,
proxies and demand-driven calls and hoping the host compiler removes the cost.

The baseline executable's `.text` is 25.7 MB versus Verilator's 3.2 MB; `.rodata`
is 47.1 MB versus 0.15 MB. This establishes code-generation bloat, not measured
instruction-cache misses. Model root object sizes are actually similar:
539,445,440 versus 537,035,072 bytes, dominated by modeled memory. Large model
allocation alone therefore does not explain the speed difference.

## What should change next

1. Correct register-backed aggregate copying and the stalled-core correctness
   problem; require successful differential execution, not identical timeouts.
2. Preserve a typed, bit/field-aware RTL dependency representation long enough to
   prove complete writes and purity. Fuse shared combinational producers and
   schedule them once per valid evaluation region, with explicit SCC settling.
3. Lower scalar and fixed-slice operations to native integers and wide operations
   to word arrays; eliminate pack/unpack round trips and unused aggregate fields.
4. Consider LUTs only for remaining small-input, expensive cones, using measured
   cost and exhaustive/differential verification.

The obstacle is not an inherent inability of CppHDL to match native C++ speed.
It is the current lowering/scheduling abstraction and unresolved semantics. The
kernel measurements demonstrate the former is changeable; the zero-commit core
prevents claiming that the whole-design problem is solved.

## Reproduction and measurement limits

Artifacts and drivers are in `build/architecture-20260912/`. Important records:
`runtime.json`, `copies.json`, `final-comparison.json`, `call-counts-summary.txt`,
the three CPU sample files and profile reports, `copy-semantics.log`,
`arbiter-verify11.log`, and `arbiter-o3-verilator-*` / `arbiter-gcc-*` logs.
`scaling.json` contains the extra 18 startup/longer-run measurements. The four
whole-model timing series contain 78 timed executions; isolated kernel timings
and profiling/counting runs are additional.

`build_probes.py`, `build_representations.py` and `build_copies.py` generate and
link isolated experimental objects against the established harness build.
`measure.py`, `measure_copies.py` and `final_comparison.py` reproduce the timing
series. This relies on local baseline objects and precompiled headers; it is not
a clean-build benchmark. No template override is added to production.

Full-model compiler: Clang 21.1.3, established O2 runtime/O0 constructor setup.
Block comparison: GCC 15.2, O3 on both generated Verilator and CppHDL variants.
ELF SHA-256 remains
`a6585f05afe272344411dd651d5412292a4b65039a108da1f44da90ea2226c8c`.

The host is a shared KVM guest; runs are unpinned and ordinary background services
remain active. Timed series do not overlap compilation or other benchmark series.
Hardware `perf` counters are unavailable (`perf_event_paranoid=4`); SIGPROF PC
sampling is used instead. Profiling runs advance CppHDL for 20,000 cycles and
Verilator for 200,000 fixed cycles, so their instruction/activity distributions
are not equivalent. `profile_summary.py` uses executable symbol sizes and leaves
unresolved/library PCs in a separate bucket; nearest-public-symbol libc labels
in exploratory raw profiles should not be interpreted as identified internals.
Timing conclusions come from the separate uninstrumented
comparisons, not the instrumented counting run.
