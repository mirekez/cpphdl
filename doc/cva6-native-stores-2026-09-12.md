# CVA6: one native-store lowering, not another scheduler

## Implementation

The new default emission path replaces standalone indexed writes to candidate
module fields with a typed store. There is no additional optimization flag,
model-specific arbiter implementation, LUT catalogue, or second scheduler.

- `Combs.cpp` lowers stores after effect analysis and dependency scheduling.
  It preserves RHS-before-LHS evaluation, live RHS references, temporary
  lifetimes, and individual write visibility. Expression-valued assignments,
  braced initializer assignments, array fields, and unsupported syntax stay on
  the existing path. C++ type dispatch handles dependent types conservatively.
- `include/cpphdl.h` implements the store with native masked words for narrow
  logic and a single-byte update for wide logic. Fixed-size `memcpy` avoids
  alignment/aliasing violations and preserves physical padding. Constant
  evaluation and other byte orders use the existing byte setter.
- Matching register-backed arrays copy their current-value array base once.
  Previously, `sv_assign_field` mistook these sources for scalars and recursively
  broadcast/repacked the entire register into each destination element.
- The inactive header-inlining path, its duplicate evaluator emitter, and its
  unused bookkeeping are removed. Evaluators remain in source partitions.

The production change is **87 net lines** across the optimizer and runtime
header, including explanatory comments. The regenerated CVA6 source contains
**1,349 typed-store call sites**; some dependent types can select the compatibility
fallback when instantiated by C++.

### Why keep the scheduler?

The measured bottleneck was generalized packed writeback. Replacing that work
does not require inventing a new clock, proving an ordinary method pure, or
choosing a different retained-value edge in a cycle. The existing dependency,
work-mutation, and lazy-cycle rules therefore remain authoritative.

This change **does not prove complete writes or memoize additional producers**.
It implements the native-representation part of the proposed approach and
removes an unused alternative emission path. Claiming producer sharing or a
new RTL settling algorithm would be inaccurate.

## Rejected form

An initial native-word implementation assigned its result through
`logic::operator=`. The isolated 16-input arbiter took approximately 0.10 seconds
per million iterations versus 0.055 for control. Assembly showed split byte
stores followed by overlapping word loads. That form also cleared otherwise
untouched padding. It was removed, not left behind an option.

The retained form copies the actual byte store into/out of the native word.
Initial isolated checks took approximately 0.030 seconds per million iterations
for both 11- and 16-input trees. Final repeated measurements are recorded below.

## Correctness checks

- The new optimizer regression compares source and optimized execution over
  all 65,536 input values in both scheduler modes at both `-O0` and `-O2`.
  Cases cover RHS/index side effects, references, temporary lifetimes, bit 63,
  a 129-bit value, array element assignment, source literals, comma expressions,
  initializer lists, assignment used as a loop condition, and an imported HDL
  `size_t` alias. Generated local types are qualified to avoid that collision.
- Actual generated CVA6 request-tree bodies agree with source execution for
  6,144 input/previous-value pairs at 11 inputs and 196,608 at 16 inputs.
  These are compiler-emitted kernels, not hand-written tree replacements.
- Runtime tests cover nested register-backed array copies, scalar broadcast,
  packed register copies, C++17 constant evaluation, and padding-preserving
  stores at 14 widths spanning 1 through 129 bits. All seven datatype tests pass.
- Address/undefined-behavior sanitizer checks of the new lowering pass.
- The broader optimizer suite passes 13 of 14 tests. The existing
  `optimizer_structural_nttp` failure (structural aggregate spelling leaking
  into generated code) also occurs with the saved pre-change compiler and
  pre-change runtime header. It is not changed by this patch.

## Full-model comparison protocol

Artifacts and reproducible commands are in `build/native-stores-20260912/`:

- `collect.sh`, `regenerate.sh`: fresh collection, then the existing L1 mode
  with the real seed/PCH so forwarding-helper metadata is available.
- `control.py`: regenerate with the saved compiler and compile against the
  saved runtime header. Control and candidate have identical state headers.
- `rebuild.py`: rebuild all constructor and runtime translation units with
  fresh PCHs, retaining the established Clang `-O2` / constructor `-O0` flags.
  `resume.py` finishes runtime compilation after correcting the generated
  `std::size_t` qualification; unchanged current constructors/PCHs are retained.
- `copy_only.py`: an ablation using the same current runtime and constructors
  without native-store lowering; this separates the two performance effects.
- `compare.py`: five alternating 5,000-cycle trials, CPU 2 affinity, the same
  matrix ELF, plus two trials at each of 0, 1,000, and 10,000 cycles. The existing
  Verilator example uses seed 1. Build and profiling time are excluded.

The ELF SHA-256 is
`a6585f05afe272344411dd651d5412292a4b65039a108da1f44da90ea2226c8c`.

## Results

Five alternating trials, 5,000-cycle cap, median process wall time:

| Variant | Seconds | Reduction from fresh control |
| --- | ---: | ---: |
| Saved production baseline | 7.2943 | — |
| Freshly regenerated control | 7.3228 | — |
| Correct typed array copies, old bit-store emission | 4.5779 | 37.5% |
| Correct typed copies plus native-store emission | **3.6169** | **50.6%** |
| Existing Verilator example | 0.7063 | — |

The complete change gives a **2.02x speedup**. Native-store lowering contributes
another **21.0% elapsed reduction over the copy-only ablation**. Both retained
performance changes independently clear the requested 10% threshold. Removing
dead emission machinery is cleanup, not an independently claimed speedup.
The slower whole-logic-assignment experiment is not retained.

Candidate runs span 3.6015–3.6792 seconds; fresh control spans 7.2337–7.6042.
The saved-production median is within 0.4% of fresh control. These are paired
local measurements, not a guarantee for other designs or machines.

### Startup-adjusted comparison

Medians of two trials at each cap:

| Variant | 0 cycles, seconds | 1,000 cycles | 10,000 cycles |
| --- | ---: | ---: | ---: |
| Fresh control | 0.0189 | 1.4406 | 14.3606 |
| Native stores and typed copies | 0.0171 | 0.7339 | 7.2113 |
| Verilator | 0.3191 | 0.4063 | 0.9487 |

The 1,000-to-10,000-cycle slopes are approximately **1,436, 720, and 60.3
microseconds per cycle**, respectively. The candidate is **5.12x slower** than
Verilator at the short 5,000-cycle cap, but approximately **11.94x slower** by
the advancing-model slope. Verilator's larger initialization cost masks part
of the gap in the short-run ratio.

### Equivalent isolated block

Five alternating runs of ten million 16-input request-tree evaluations, GCC
`-O3`, CPU 2, matching checksum `2cfac66cb2cae871`:

| Implementation | Median seconds | Relative to Verilator |
| --- | ---: | ---: |
| Old compiler-emitted CppHDL block | 0.53933 | 2.36x |
| New compiler-emitted CppHDL block | 0.30131 | **1.32x** |
| Existing Verilator block benchmark | 0.22847 | 1x |

This is a **44.1% reduction** for the new block. It includes the CppHDL graph
dispatcher, not just a replacement Boolean equation. Unlike the full-core
timings, exhaustive output comparisons and matching checksums establish that
these blocks compute the same tested function.

### What the profile says

Separate 10,000-cycle runs sampled instruction pointers with a 1 ms CPU timer:
11,671 control samples and 6,148 candidate samples. Profiling is excluded from
the performance table. Percentages are sample shares, not call counts or
hardware cache counters; inlined stores are attributed to their enclosing body.

| Sample category | Control | Candidate |
| --- | ---: | ---: |
| Packed slice writeback | 50.6% | 33.6% |
| Dynamic combinational bodies | 23.9% | 35.9% |
| Array operations | 11.7% | 13.4% |
| PMP struct methods | 4.8% | 0.05% |

The representation bottleneck shrinks substantially; it is not eliminated.
The larger percentage for an unchanged category does not imply more absolute
work, since total runtime roughly halves. Dynamic-body samples also include
useful computation, so they cannot all be labeled scheduler overhead.

Separately, `count.py` instruments four request-tree producers in a disposable
binary built from the current generated sources. At a 1,000-cycle cap,
evaluators 4321, 9251, 8013, and 8033 each execute **477,477 times**: **477 calls
per model evaluation**, including the initial evaluation. This re-confirms the
previous producer-sharing problem after the native-store change. Counter runs
are not included in timing results and do not replace the production binary.

## Important correctness limit

All CppHDL variants have identical output hashes at matching cycle caps, but
still time out. An additional candidate run with `CPPHDL_TRACE_COMMITS=1`
confirms **zero retired instructions at 5,000 cycles**. The existing Verilator
example completes the same ELF successfully after **69,315 cycles**.

Consequently the full-model numbers measure the cost of advancing the existing
converted model, **not equivalent retired-instruction throughput**. The typed
array fix passes dedicated semantic tests, but neither it nor faster bit stores
establish whole-core architectural equivalence. No such equivalence is claimed.

The remaining architectural work is a real complete-write/effect proof that can
share repeatable producers safely, plus removal of remaining packed-proxy work
and investigation of the existing failure to retire. Adding more heuristic
schedulers or treating the faster timeout as a completed CVA6 validation would
hide these issues rather than solve them.
