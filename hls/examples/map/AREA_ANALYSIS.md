# Why std::map Uses More Logic Than RbMap

Measured on 2026-09-25 using the same block-RAM flow as the
[baseline comparison](README.md#synthesis-comparison). The full examples use
32-bit keys and values, 16-bit address signals, and 4,096-byte allocation pools.
The attribution tables below describe the baseline before multi-cycle call
sharing; the follow-up measurements at the end describe the improvement.
The [memory-access investigation](MEMORY_ACCESS_ANALYSIS.md) remeasures both
versions and explains repeated loads, missing store forwarding, and mux growth.

**The largest measured difference is erase/rebalancing, amplified by the HLS
scheduler's duplicated call-site state and memory-port selection logic. It is
not a difference in RAM capacity, and it is not mainly allocator wrappers.**

## Measure Operations Separately

I generated and synthesized experimental copies with selected command branches
removed. These are deliberately incomplete designs for area attribution, not
proposed fixes or passing replacements for the real examples. Each row uses
the same backend, address width, pool size, and Yosys settings.

| Operations available | std::map LUTs | RbMap LUTs | Difference |
| --- | ---: | ---: | ---: |
| Full example | 27,268 | 15,262 | 12,006 |
| Full except erase | 16,114 | 10,475 | 5,639 |
| Full except clear | 25,637 | 14,821 | 10,816 |
| Full except traversal/checksum and bulk value update | 22,081 | 10,092 | 11,989 |
| Insert/update, lookup, size only | 9,232 | 4,890 | 4,342 |

Every row still infers 16 RAMB36E1 blocks. Fresh, unmodified control copies
reproduce the original full-design LUT counts exactly.

Removing erase saves 11,154 LUTs from `std::map`, versus 4,787 from `RbMap`.
The difference, 6,367 LUTs, explains about **53% of the full-design gap** in
this experiment. Clear explains another roughly 1,190 LUTs of difference.
Traversal/checksum and bulk update cost about 5,200 LUTs in each design, but
removing them barely changes the gap.

These marginal costs are not additive module areas. Removing an operation also
changes the shared scheduler, muxes, live values, and technology mapping.
The minimal row establishes that insertion/lookup and their supporting logic
still differ substantially after erase, clear, and traversal are removed.

## What Remains After Optimization

The generic counts below are taken after process lowering, fast optimization,
width reduction, and memory-read-register merging, before Xilinx mapping.
Flip-flops are counted after mapping.

| Structure | std::map | RbMap |
| --- | ---: | ---: |
| Scheduled blocks in generated RTL | 723 | 378 |
| Generated helper function bodies | 362 | 194 |
| Static scheduled memory-read sites | 291 | 129 |
| Static scheduled memory-write sites | 135 | 61 |
| Generic `$mux` cells | 9,971 | 4,977 |
| Sum of `$mux` output widths | 197,039 | 90,789 |
| Generic equality cells | 505 | 226 |
| Generic addition cells | 168 | 141 |
| Generic right-shift cells | 9 | 9 |
| Generic multiplication cells | 2 | 2 |
| Generic division cells | 0 | 0 |
| Mapped flip-flops | 3,446 | 2,177 |
| Mapped CARRY4 cells | 667 | 567 |

The memory-site counts include uses of shared helpers, not merely their
definitions. They count potential operations in the compiled schedule, not
the number executed by one command. Similarly, scheduled blocks are not
clock counts: multiple blocks can execute in the same clock. The generated
`active` vector is combinational, not one flip-flop per block.

The much larger increase in muxing than arithmetic is important. The problem
is predominantly selecting, forwarding, and preserving values across many
possible operations, not an unusually expensive key comparison or divider.

### Memory And Next-State Selection

`delayed_scheduler.cpp::resolveAccesses()` turns each indirect access into a memory
request and a scheduling boundary. The shared-block emitter then carries
successive versions of state through the scheduled blocks. Conceptually:

```systemverilog
next_address = previous_address;
if (this_block_active && fault == 0)
    next_address = this_access_address;
```

The same pattern applies to write data, access size, next phase, and other
values. All sites ultimately feed one shared memory port. More access sites
therefore grow both the request-selection muxes and the control selecting
which operation runs next.

Counting generic mux output bits that retain explicit generated signal names:

| Named mux destinations | std::map bits | RbMap bits |
| --- | ---: | ---: |
| `active` and `phase` | 53,038 | 22,228 |
| Memory address, write data, size, read/write enables | 29,514 | 13,214 |
| `fault` | 352 | 32 |
| Other/internal mux outputs | 114,135 | 55,315 |

These are intermediate mux-width sums, **not LUT counts**. Internal helper
outputs can include more control or memory-selection logic in the last row.
After LUT mapping, control, address generation, and data paths share logic;
their overlapping fan-in cones cannot be added as independent area totals.

### Retained Values

Tracing mapped flip-flop outputs to generated variable names finds 3,057 bits
associated with retained C++ variables in `std::map`, versus 1,852 in `RbMap`.
The shared memory-access staging adds 144 versus 80 bits. Together these
differences account for the full 1,269-flip-flop gap. The other categories
(command capture, result, allocator pointer, fault, phase/handshake, and RAM
read offset) have equal flip-flop counts.

For example, named left/right rotation call sites retain 800 bits in the
standard-map design, and `__tree_is_left_child` call sites retain another 352.
RbMap's `rotate` and `replace` call sites retain 245 and 384 bits respectively.
These are source-name attributions, not isolated LUT areas or equivalent
function boundaries.

## Why The C++ Implementations Produce Different Schedules

### Erase And Rebalancing

The libc++ version used here has separate mirrored branches in
`__tree_remove`, separate `__tree_left_rotate` and `__tree_right_rotate`
functions, and repeated parent/child accesses in those branches. The full
generated schedule contains:

- 165 blocks attributed to `__tree_remove` itself.
- 80 blocks for left rotations and 80 for right rotations, across insertion
  and deletion call sites.
- Additional blocks for parent access, left-child checks, and successor
  traversal.

RbMap uses a direction argument and `child[2]` for the mirrored cases. Its
schedule has 37 blocks for `fix_erase`, 24 for `erase`, 50 for `rotate`, and
72 for `replace`; the last two also serve insertion.

There is also a real contract difference. libc++ physically transplants the
successor node when deleting a node with two children. RbMap copies the
successor's key/value and removes the successor instead. RbMap does not promise
the standard map's stable handles to other elements. Its simpler algorithm
cannot simply replace std::map's implementation without changing that contract.

libc++ also maintains a cached begin-node pointer and computes a successor in
`__remove_node_pointer`. The successor is needed to update that cached pointer,
not just to supply the return value of `erase`. Discarding the returned iterator
does not make all of this work removable.

### Function Reuse Is Not Yet Complete Hardware Reuse

`delayed_scheduler.cpp` assigns ordinary calls distinct `__call_N_depth_D` scopes.
`SharedBlocks.cpp::BlockSharing::add()` reuses matching function text with
parameters, but the emitter still invokes those helpers from separately
scheduled blocks with separate live values.

An SV helper named `__shared_N` is consequently **not a promise of one shared
multi-cycle hardware unit**. Synthesis elaborates its calls and optimizes them;
different arguments, enables, and retained values can keep separate logic.
The full standard-map design has five distinct left-rotation call scopes and
five right-rotation scopes. Those repeat the memory sequences and live values
even where the source-level helper body is shared.

This is a converter optimization opportunity, not an unavoidable cost of
using a standard C++ container. RbMap is affected by the same limitation,
but its smaller control graph exposes fewer copies.

### Recursive Clear

libc++ `__tree::destroy` recursively visits both children. The current example
requests depth eight, so generated RTL has eight bounded depth-specific
bodies. Their retained variables account for 384 mapped flip-flops. RbMap's
iterative clear uses parent links and accounts for 64.

The no-clear experiment confirms an area cost, but this is secondary to erase.
Removing clear or reducing the recursion limit would weaken the design, not
fix it. Any optimization must retain the declared recursion behavior.

## What Does Not Explain The Gap

- Both implementations use the same eight-byte-lane RAM backend and infer
  16 RAMB36E1 primitives. Its utilization deserves separate work but is common
  to both designs.
- A native layout probe using the same libc++ headers reports **40-byte nodes
  for both containers**. Their container objects are 24 and 16 bytes.
- Total byte arenas differ by only 48 bytes: 4,160 versus 4,112. The 4,096-byte
  allocation pools and 16-bit address signals are unchanged.
- Host-layout pointer slots still occupy eight bytes in both node types.
  Packing them would help both; it is not the explanation for this difference.
- The earlier constant-divider problem is absent from these optimized generic
  netlists. Both have two multiplication cells for the workload checksum.
- Long allocator/tuple/helper listings do not directly imply equivalent
  hardware cost: much of that code folds away. Source length alone is not an
  area measurement.

## Optimization Priorities

1. **Share multi-cycle methods across call sites.** Start with rotations and
   repeated node-access helpers. Use fixed argument/live-value registers and
   explicit return continuations, preserving the existing bounded hardware
   model. Do not introduce a runtime CPU stack. Measure area and command latency
   together; sharing must not silently add a clock to every ordinary call.
2. **Restructure memory-request and next-state selection.** Collect mutually
   exclusive request producers instead of passing each signal through every
   block. Prove exclusivity from the schedule; blindly changing priority muxes
   to parallel selection could change blocking-assignment semantics.
3. **Forward and reuse proven-equal field loads.** Repeated parent/child queries
   create memory boundaries and retained temporaries. Elide them only with
   alias/write analysis that proves a cached value remains valid.
4. **Reuse register storage for non-overlapping live values.** Call-local
   temporaries should not require separate physical registers solely because
   their generated names differ. Recursion depths and simultaneously live
   caller values must remain distinct.
5. **Optimize recursive clear and node layout afterward.** Both matter, but
   the erase and mux measurements provide the stronger first targets.

The baseline investigation did not change converter, container, or
production-test behavior. The ablations are synthesis diagnostics, not
functional tests. The follow-up below implements the first optimization.
These results are not place-and-route timing measurements.

## Local Evidence

Experiment sources, synthesis scripts, logs, intermediate/mapped JSON netlists,
and analysis scripts are under `build/hls/rbmap-investigation/`:

- `ablate.py`, `ablations.json`, and `ablations-full-minimal.json`: operation
  removal experiments and full controls.
- `run.py`, `Map/`, and `RbMap/`: instrumented baseline synthesis and netlists.
- `analyze.py` and each design's `analysis.json`: cells, flip-flop attribution,
  mux widths, and overlapping mapped fan-in cones.
- `source_metrics.py` and `source-metrics.json`: scheduled access-site counts.
- `Layout.cpp`: libc++ and RbMap object/node size probe.

Diagnostic netlist-dump runs mapped to 27,267 and 15,246 LUTs rather than
27,268 and 15,262, a small mapping variation in the instrumented runs. Their
flip-flop counts agree. The operation-removal table uses the
unmodified flow, whose full control copies reproduce the original baseline.

## Follow-Up: Shared Multi-Cycle Bodies

The scheduler now shares eligible pointer-writing helpers across callers,
including their parameter/local values and scheduled memory accesses. This is
an AST-based rule for free/static `void` functions, not a special case for map
or rotations. Known direct-object addresses remain specialized. See
[Sharing Multi-Cycle Calls](../../../doc/hls.md#sharing-multi-cycle-calls) for the
eligibility rules and clock-boundary analysis.

Using the unchanged BRAM synthesis flow and complete workloads:

| Measurement | Before | After |
| --- | ---: | ---: |
| Map LUTs | 27,268 | 23,366 |
| Map flip-flops | 3,446 | 3,062 |
| Map CARRY4 | 667 | 566 |
| Map rotation-associated flip-flops | 800 | 448 |
| Map scheduled blocks | 723 | 619 |
| Map workload execution clocks | 11,435 | 11,435 |
| Multimap LUTs | 27,580 | 24,274 |
| Multimap flip-flops | 3,588 | 3,204 |
| Multimap workload execution clocks | 2,834 | 2,834 |

Both designs still infer 16 RAMB36E1 cells. Address width, allocation pool,
supported operations, and bounded recursive clear are unchanged. Map's LUT
reduction is 14.3%; Multimap's is 12.0%. The 800-to-448 register comparison uses
mapped flip-flop Q aliases attributed to the rotation scopes, as above; it is
not an isolated LUT-area measurement.

Five call sites per rotation direction become two hardware bodies: insertion
and deletion retain separate enclosing call contexts. Each new rotation scope
accounts for 112 mapped bits: 80 data bits and a still-32-bit caller selector.
Across the four bodies, those selectors account for 128 bits. Narrowing them
to the actual caller count is a further general optimization opportunity.
The remaining body duplication is deliberate for now:
sharing across contexts must account for re-entry and recursion bounds, not
just equal function declarations. Nonstatic methods and ordinary read-only
helpers remain outside this optimization.

The updated standard-map RTL is in
`build/hls/tests/std/hls_delayed_Map_shared_memory_bram-rtl/generated/`.
Reproduce the new synthesis measurements with:

```sh
python3 hls/tools/synth_std.py --yosys build/tools/oss-cad-suite/bin/yosys \
    --storage bram --containers Map Multimap --memory-mib 6144 \
    --output build/hls/map-call-sharing-bram
```

That directory contains immutable input snapshots and hashes, reports, and
Yosys scripts; `Map/mapped.json` from the diagnostic rerun supplies the new
register attribution. The register-backed backend is behaviorally tested too;
its area has not been remeasured in this follow-up.
