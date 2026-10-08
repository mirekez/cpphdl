# Memory Accesses And Selection Logic

Investigation on 2026-09-27. This follows
[the original area comparison](AREA_ANALYSIS.md) and the multi-cycle sharing
change. The initial investigation did not change converter or container
behavior. The implementation follow-up at the end records subsequent changes.

## Remeasured Baseline

The original numbers reproduce exactly. After the call-sharing change, the
standard-map numbers are lower, but the gap remains:

| Measurement | Map before sharing | Map after sharing | RbMap |
| --- | ---: | ---: | ---: |
| Static read sites | 291 | 237 | 129 |
| Static write sites | 135 | 93 | 61 |
| Total access sites | 426 | 330 | 190 |
| Scheduled blocks | 723 | 619 | 378 |
| Generic mux cells | 9,971 | 8,252 | 4,977 |
| Sum of generic mux output widths | 197,039 | 161,942 | 90,789 |

The generic netlists are measured at the same point: process lowering, fast
optimization, width reduction, and memory-read-register merging, before Xilinx
mapping. These widths are **not LUT counts, physical bus widths, or timing
estimates**. Subsequent optimization and mapping can eliminate bits and combine
logic.

An access site is a scheduled read/write request, not a physical RAM port or
one transfer made by every command. Alternatives in both branches count;
a loop body counts once per generated implementation, not once per iteration.
A wide access can require multiple eight-byte requests. Reusing function text
does not remove the sites at its separately scheduled callers.

The reduction is 54 reads and 42 writes: exactly six fewer rotation-body
copies at nine reads and seven writes per copy, including their node-access
helpers. Five call sites per direction became two hardware bodies per direction.
The other new shared helpers had only one enclosing instance already.

## Why The Remaining Access Count Is Higher

### Erase Has More Source-Level Work

libc++ `__tree_remove` in `.conda/include/c++/v1/__tree:331` contains separately
written left/right rebalancing branches. Each has its own sibling, parent,
child-color, recoloring, and rotation accesses. RbMap's `fix_erase` uses a
direction argument and `child[side]`, so one source body handles both directions.
The current converter does not recognize and merge those mirrored branches.

There is also a semantic difference: libc++ transplants the successor node when
erasing a node with two children. RbMap copies its key/value and then removes
the successor. The latter does not preserve the same node-handle guarantees.
The compiler cannot replace the standard algorithm with that shortcut.

libc++ `__remove_node_pointer` also advances an iterator and maintains the
cached begin pointer before calling `__tree_remove`. The iterator work cannot
all disappear just because our command ignores `erase`'s returned iterator:
it also supplies the new cached begin pointer.

As an indication of scale, current blocks labelled `__tree_remove` contain
99 access sites; RbMap blocks labelled `erase` and `fix_erase` contain 33.
These are **emitted block labels, not isolated method costs**. Block coalescing
can put caller operations into a block labelled with a callee's name. For
example, a `__parent_unsafe`-labelled block can contain a caller's write even
though that C++ getter only reads. The total site counts do not have this
attribution ambiguity.

### Repeated Loads Are Not Reused

Examples from the libc++ tree source include:

```cpp
if (__w->__left_ == nullptr || __w->__left_->__is_black_) { /* ... */ }
```

On the second branch, the already-read `__w->__left_` pointer is loaded again.
Similarly, `__tree_is_left_child(x)` reads `x->__parent_` and then its left link;
surrounding statements often reload that same parent.

`delayed_scheduler.cpp::read()` creates a new access for each lvalue read.
`resolveAccesses()` promotes statically known, nonescaping objects to direct
values, but otherwise emits a memory request and `@memory_clock@` for each
access. It does not perform general load value numbering or propagate a loaded
field through later statements/calls. Different temporary names do not tell
it that two pointers select the same unchanged field.

Once these accesses have become separate clocked transactions, ordinary
combinational synthesis cannot simply merge them: their requests, sampled
results, and continuation state belong to different cycles. Eliminate proven
redundant accesses before introducing memory boundaries, rather than expecting
Yosys to recover the original untimed C++ intent.

RbMap also has repeated loads, for example in `while (n->parent &&
n->parent->red)`. It benefits from more explicit locals (`p`, `g`, `sibling`)
and a smaller, direction-parameterized algorithm; it is not immune to the
lowering problem.

### Stores Are Followed By Avoidable Reloads

The rotation source has this pattern:

```cpp
__x->__right_ = __y->__left_;
if (__x->__right_ != nullptr)
    __x->__right_->__set_parent(__x);
```

The converter loads `y->left`, stores it into `x->right`, then reloads
`x->right` for the condition and again for the call. Keeping the loaded pointer
in a local would avoid those last two reads. This is a store-forwarding
opportunity, not a reason to replace the standard container.

### Other Differences Are Smaller

Bounded recursive destruction has eight generated depths, each reading two
child links: 16 static read sites. RbMap's iterative clear uses seven access
sites in its emitted blocks. Standard-map header/begin-node bookkeeping also
costs accesses. These contribute, but erase/rebalancing is the larger source
of duplicated alternatives and pointer queries.

Both node layouts remain 40 bytes and both designs use 16-bit address signals.
The eight-byte host pointer slots fit in one scheduled memory request each.
Packing pointers could improve capacity and data routing, but does not by
itself eliminate repeated dereferences or mirrored control paths.

## Why Each Site Enlarges Selection Logic

The emitter processes blocks in topological order and creates a new version of
every output a block might change. A memory field effectively becomes:

```systemverilog
next_address = previous_address;
if (block_active && fault == 0)
    next_address = this_request_address;
```

The same selection is repeated for request size, write data, read/write enables,
phase, activation bits, and retained values. Although a memory boundary prevents
a second request from executing in the same clock, the emitted network still
preserves procedural priority across all potential producers. Local function
guards and return-value routing add more muxes within those blocks.

The named request mux widths follow the site counts exactly in these netlists:
16 address bits and 32 size bits per access, one read/write-enable bit per
access, and 64 write-data bits per write. Thus their sum is
`49 * access_sites + 64 * write_sites`.

| Mux output-bit attribution | Map before | Map now | RbMap | Current excess |
| --- | ---: | ---: | ---: | ---: |
| Named memory-request fields | 29,514 | 22,122 | 13,214 | 8,908 |
| Named `active` and `phase` fields | 53,038 | 46,832 | 22,228 | 24,604 |
| Named `fault` fields | 352 | 544 | 32 | 512 |
| Other/internal outputs | 114,135 | 92,444 | 55,315 | 37,129 |
| Total | 197,039 | 161,942 | 90,789 | 71,153 |

Only 12.5% of the remaining excess is in explicitly named request fields;
34.6% is in named control fields. The other/internal category includes helper
return routing, temporaries, and data selection, potentially including more
control logic. It must not be presented as a clean, isolated datapath area.

This explains why replacing the RAM alone or counting read/write ports misses
much of the cost. It also does not prove that a large generic bus survives into
mapped hardware. For example, request size is emitted as 32 bits even though
the largest constant request size in this Map is 24 bytes. Explicit narrowing
may simplify the intermediate design; its actual LUT benefit needs measurement.

## Small Behavioral Probes

Five diagnostic designs were converted and run with Verilator against their
native C++ methods, 64 transactions each. All 320 comparisons passed, including
aliasing and nonaliasing writes. These use the same BRAM scheduler:

| Probe operation | Body accesses | Clocks per probe command |
| --- | --- | ---: |
| `return uint64_t(*p) + *p` | 2 reads | 3 |
| `v = *p; return uint64_t(v) + v` | 1 read | 2 |
| `*p = value; return *p` | 1 write, 1 read | 3 |
| `*p = value; return value` | 1 write | 2 |
| `v = *p; *q = operation; return uint64_t(v) + *p` | 2 reads, 1 write | 4 |

Each run contains 32 initialization commands and 32 probe commands; reset and
initialization are excluded from the clock column. The last probe deliberately
alternates `q == p` and `q != p`: reusing the old load unconditionally would
change the answer. These experiments establish missed reuse, not the total
LUT savings that a future general optimization will achieve.

The unchanged Map and RbMap BRAM regression executables also pass their
240-command workloads: 11,435 and 10,363 execution clocks respectively.
The measured snapshots match their current generated RTL byte for byte.

## Recommended Fix Order

1. **Add alias-aware load reuse and store forwarding before memory scheduling.**
   Track access identity, pointer/value versions, byte ranges, and writes in the
   structured access representation. Start conservatively: invalidate cached
   loads at unknown stores/calls and loop back-edges. Preserve null-check
   control flow, first-fault behavior, bounds checks, and partial-store semantics.
   Do not match textual temporary names or move dereferences before their guards.
   Reuse across clocks also requires exclusive ownership or an explicit memory
   stability contract if other agents can write the arena.
2. **Generate request and next-state selection from the control graph.** Avoid
   forwarding every candidate through a global priority chain. Prove that memory
   producers are mutually exclusive, and form selection at real control-flow
   joins or clock continuations. Keep same-clock arithmetic together; this is
   not a proposal to execute one source instruction per clock. Compare mapped
   LUTs and command clocks, not just shorter SV or lower generic mux counts.
   The earlier naive OR-network experiment increased area and is not a fix.
3. **Narrow internal control fields where their ranges are known.** Request
   size, phase, and caller selectors need design-derived widths. Do not truncate
   pointer payloads, byte counts, or public result types indiscriminately.
4. **Consider mirrored-branch sharing only with equivalence evidence.** It may
   reduce libc++ erase duplication, but the converter must preserve its node
   identity and iterator guarantees. Do not silently substitute RbMap's erase.

## Evidence And Reproduction

Local diagnostic files are under `build/hls/access-investigation/`:

- `analyze.py`, `results.json`: scheduled-site counts and generic mux breakdowns.
- `Map-before/`, `Map-after/`, `RbMap/`: Yosys scripts, logs, and generic JSON.
- `MemoryProbe.cpp`, `probes.cmake`, `probe-0/` through `probe-4/`: sources,
  generated RTL, build logs, and successful simulation logs.

```sh
python3 build/hls/access-investigation/analyze.py
cmake -P build/hls/access-investigation/probes.cmake
```

The analyzer reuses existing generic JSON; remove a diagnostic variant's
`generic.json` to regenerate it. The immutable source snapshots are in
`build/hls/rbmap-comparison-bram/` and `build/hls/map-call-sharing-bram/`.
The probes are investigation artifacts, not new production CTest cases.

## Follow-Up: Clock-Local Read Reuse

The new `hls/MemoryEffects.h/.cpp` analysis reuses a captured read within its
zero-time region. It follows helper calls, intersects facts at branch joins,
and invalidates samples on writes and clock boundaries. Multi-beat reads are
not cached across their constituent edges. Memory-writing helpers retain a
caller-independent body so this optimization does not multiply outlined code.
Neither container's source or workload changed.

| Measurement | Map before | Map after | RbMap before | RbMap after |
| --- | ---: | ---: | ---: | ---: |
| Static read sites | 237 | 212 | 129 | 104 |
| Static write sites | 93 | 93 | 61 | 61 |
| Scheduled blocks | 619 | 594 | 378 | 353 |
| Clocks for 240 commands | 11,435 | 10,737 | 10,363 | 9,310 |
| Maximum command clocks | 158 | 147 | 179 | 159 |

Both shared-register-memory and BRAM simulations match the native C++ results.
Current generated files and logs are in the corresponding
`build/hls/tests/std/hls_delayed_Map_shared_memory*-rtl/` and
`build/hls/examples/map/hls_delayed_RbMap_shared_memory*-rtl/` directories.
LUT synthesis was refreshed on 2026-09-30 with the unchanged Xilinx mapping
flows, 16-bit addresses, and allocation pools:

| Design | Storage | Previous LUTs | Current LUTs | Current flip-flops | RAMB36 |
| --- | --- | ---: | ---: | ---: | ---: |
| Map | Registers | 43,104 | 38,508 | 36,339 | 0 |
| Map | Block RAM | 23,366 | 22,983 | 3,014 | 16 |
| RbMap | Registers | 32,158 | 33,342 | 34,990 | 0 |
| RbMap | Block RAM | 15,262 | 14,580 | 2,065 | 16 |

Map's previous register-mode result predates both multi-cycle call sharing and
read reuse; its previous BRAM result already includes call sharing. The Map
BRAM reduction attributable to this follow-up is 1.6%. RbMap's BRAM count falls
4.5%, while its register-mode count rises 3.7% despite fewer execution clocks.
These are measured area outcomes, not a claim that eliminating each read site
saves a fixed amount of hardware. Generic mux-width totals have not been rerun.

All mappings pass `check -assert`. Exact input snapshots and reports are in
`build/hls/read-reuse-registers/` and `build/hls/read-reuse-bram/`; the
[full current tables](../../../doc/hls.md#current-utilization-2026-09-30) also include
the other standard-container tests. No placement/routing or timing closure
was performed.

The permanent regression is `hls/tests/DelayedMemoryEffects.cpp`, with native,
direct-register RTL, shared-memory RTL, and BRAM RTL checks. It includes exact
clock assertions: repeated reads and both arms following a dominating read
reuse one sample, but another memory transaction or a loop edge prevents reuse.
