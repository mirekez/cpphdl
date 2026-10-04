# Small Red-Black Map

`RbMap.h` is a deliberately small C++17 ordered map, separate from the standard
container tests. Its actual C++ methods are lowered by `cpphdl::hls::Clocked`;
there is no handwritten replacement RTL or special converter path for the tree.

```cpp
RbMap<uint32_t, uint32_t> map;
map.insert_or_assign(7, 42);
auto* node = map.find(7);  // nullptr when absent; node->value is the mapped value
map.erase(7);             // true only if the key was present
map.clear();
```

The API is `insert_or_assign`, `find`, `erase`, `size`, `clear`, and `first`/`next`
for ordered traversal. `root()` is an inspection point for the invariant tests.
Keys are unique and use their native comparison operators. Do not modify keys
through node handles or retain handles across erase/clear. Copying is disabled;
the map owns and destroys its nodes. This is not a complete STL-compatible map.

Insertion and deletion both rebalance the red-black tree. A single rotation
method handles both directions; the rebalancing methods similarly share their
mirrored cases. Parent links allow iterative traversal and clear, with no
recursion limit, auxiliary stack, or sentinel allocation.

## Clocked Example

`DelayedRbMap.cpp` wraps the tree in `RbMapMethods`, which exposes the same command
operations as `hls/tests/std/DelayedMap.cpp`: insert/update, ordered checksum,
fill existing values, lookup, erase, clear, and size.

The two configurations use the same memory scheduling contract:

```cpp
cpphdl::hls::ClockedDelayer<RbMapMethods, 0, 16, 4096, true, false> registers;
cpphdl::hls::ClockedDelayer<RbMapMethods, 0, 16, 4096, true, true> block_ram;
```

Both have 16-bit address signals and a 4,096-byte allocation pool. The converter
keeps the map header in registers; the current byte arena is 4,112 bytes,
including the 16-byte reserved prefix. Host-layout node fields are not compacted by selecting
16-bit addresses. The shared port schedules memory accesses across clocks;
these are not single-clock container operations. The native wrapper is a
transaction reference, not a cycle-accurate model of that schedule.

Allocation is currently monotonic in generated RTL, just as in the `std::map`
example. `erase` and `clear` remove nodes but do not reclaim arena space. Reset
reinitializes the arena. Exhaustion produces the HLS allocation fault, not an
unbounded allocation or a fixed element-count limit. Native C++ uses ordinary
`new`/`delete` and releases its allocations normally.

## Build and Test

From the repository root, with the usual HLS/libc++ prerequisites installed:

```sh
cmake -S . -B build -DCPPHDL_BUILD_HLS_TESTS=ON
cmake --build build --target cpphdl hls_delayed_RbMap_shared_memory \
    hls_delayed_RbMap_shared_memory_bram hls_rbmap_invariants
ctest --test-dir build -R '^hls_(delayed_RbMap.*|rbmap_invariants)$' --output-on-failure
```

The native and Verilator transaction tests use the independent `std::map`
reference and the same 240-command workload as the standard-container example.
They also check response backpressure, input changes while busy, reset during
an operation, and reuse after reset. Generated RTL is under:

- `build/hls/examples/map/hls_delayed_RbMap_shared_memory-rtl/generated/`
- `build/hls/examples/map/hls_delayed_RbMap_shared_memory_bram-rtl/generated/`

Each test directory contains conversion, Verilator, build, and simulation logs.
The simulation log reports total execution clocks and the longest command.

Generated SystemVerilog snapshots are also available beside the example:

- [generated/registers/](generated/registers/): register-backed arena.
- [generated/bram/](generated/bram/): block-RAM-oriented arena.

Each directory includes its top module, scheduled worker and required packages.
Compile one variant at a time; the two directories define the same top module.
These are HLS RTL outputs, not technology-mapped netlists. The regressions above
regenerate and test the corresponding build-directory copies.

`RbMap_test.cpp` separately checks the native tree after each mutation against
`std::map`: strict ordering, parent links, root color, no adjacent red nodes,
equal black height, traversal, size, and lookup. It covers all 14,400 pairs of
five-key insertion/deletion permutations, ascending/descending insertion, repeated
root deletion, boundary keys, duplicate updates, repeated clear, and 20,000 seeded
random mutations.

## Current Latency

Both register-backed and BRAM simulations give these results for the shared
240-command workload, excluding response backpressure and reset clocks:

| Design | Execution clocks | Longest command |
| --- | ---: | ---: |
| `std::map` | 10,737 | 147 |
| `RbMap` | 9,310 | 159 |

These include clock-local read reuse; the earlier totals were 11,435 and
10,363 clocks respectively. The smaller implementation improves the workload
total but not the worst command latency.

## Synthesis Comparison

Refreshed on 2026-09-30 with shared multi-cycle calls and clock-local read reuse.
Yosys 0.69+62 targets Xilinx 7-series primitives using the unchanged synthesis
flow. All four mappings pass `check -assert`; regenerated inputs match the RTL
used by the passing native/Verilator comparisons:

| Arena storage | Design | LUTs | Flip-flops | CARRY4 | RAMB36 |
| --- | --- | ---: | ---: | ---: | ---: |
| Registers | `std::map` | 38,508 | 36,339 | 597 | 0 |
| Registers | `RbMap` | 33,342 | 34,990 | 592 | 0 |
| Block RAM | `std::map` | 22,983 | 3,014 | 597 | 16 |
| Block RAM | `RbMap` | 14,580 | 2,065 | 592 | 16 |

`RbMap` reduces LUT usage by 13.4% with register storage and 36.6% with block
RAM. Both BRAM designs infer actual RAMB36E1 primitives, but their identical
16-block allocation remains large for a 4 KiB pool. These are synthesis cell
counts, not placed-and-routed area or timing results.

Before read reuse, standard Map's BRAM result was 23,366 LUTs after call sharing;
it now uses 1.6% fewer LUTs. RbMap's BRAM count falls from 15,262 to 14,580,
but its register-backed count rises from 32,158 to 33,342 (3.7%). The reduced
read count and execution clocks therefore do not imply an area improvement in
every configuration. See the [read-reuse investigation](MEMORY_ACCESS_ANALYSIS.md#follow-up-clock-local-read-reuse)
and the [earlier call-sharing measurements](AREA_ANALYSIS.md#follow-up-shared-multi-cycle-bodies).

Both designs use 32-bit keys and values, 16-bit address signals, the same
4,096-byte allocation pool, and the same command interface and workload.
Generated bookkeeping makes their total arenas slightly different:
4,160 bytes for `std::map` and 4,112 bytes for `RbMap`. Node layouts remain
host-derived; equal pool sizes do not imply equal node capacity.

The shared synthesis flow disables DSPs, I/O pads, clock buffers, and optional
SAT-based sharing. LUT totals sum LUT1 through LUT6; carry and RAM primitives
are counted separately. Reproduce from the repository root after generating
both designs' register and BRAM variants:

```sh
python3 hls/tools/synth_std.py --yosys build/tools/oss-cad-suite/bin/yosys \
    --storage registers --containers Map RbMap --memory-mib 6144 --append \
    --output build/hls/read-reuse-registers
python3 hls/tools/synth_std.py --yosys build/tools/oss-cad-suite/bin/yosys \
    --storage bram --containers Map RbMap --memory-mib 6144 --append \
    --output build/hls/read-reuse-bram
```

Each output directory includes a summary and `results.json`, plus per-design
RTL snapshots, input hashes, synthesis scripts, logs, and cell statistics.

See [the area investigation](AREA_ANALYSIS.md) for operation-removal experiments,
netlist measurements, and the main causes of the block-RAM LUT difference.

Keep the command workload, 16-bit addresses, allocation pool, RAM backend, and
synthesis flow matched when comparing area and latency. A shorter C++ source
does not itself establish a smaller or faster synthesized design.
