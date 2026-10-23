# Reproducible full CVA6 comparison

These entry points use the full `cv32a6_imac_sv32` testharness, not a partial
replay. The default workload is the self-checking 32×32 integer matmul: 32,768
MACs, checksum `0xd687fdc5`, 396,778 total / 396,768 work cycles and 297,074
integer architectural commits with the reference source/toolchain.

## Prerequisites

Use an initialized CVA6 checkout (including its RTL submodules), built hdlcpp
and cpphdl tools, Clang with C++23 support, Verilator, and an RV32 bare-metal
toolchain plus Spike/FESVR libraries. The local defaults are:

| Environment override | Default |
| --- | --- |
| `CVA6_SRC` | `cva6` beside the cpphdl checkout |
| `CPPHDL` | `<cpphdl>/build/cpphdl` |
| `HDLCPP` | `<cpphdl>/hdlcpp/build/hdlcpp` |
| `CPPHDL_CXX` | `<cpphdl>/.conda/bin/clang++` |
| `VERILATOR` | `<CVA6_SRC>/tools/verilator-new/bin/verilator`, otherwise PATH |
| `RISCV` | `$HOME/riscv` |
| `CVA6_WORK_ROOT` | `/tmp/cpphdl-cva6-<uid>` |
| `ELF` | `<work-root>/elf/matrix_multiply.riscv` |

The tested original Verilator is 5.049; the older local 5.034 cannot build this
full model reliably. Rebuild your tools after changing compiler sources.
All artifacts, snapshots, objects and traces stay below `/tmp`. Source checkout
files are not modified. Builds are serial and guarded against low RAM, low
temporary space, oversized files and timeouts. Errors propagate to the caller.
The temporary-space guard accounts for Linux per-user quotas as well as `df`.

## Build and run

From this directory (or invoke the scripts by absolute path):

```sh
./.build_cva6.sh
./.build_cva6_hdlcpp.sh
./.compare.sh
```

`.build_cva6.sh` snapshots the original RTL/harness and builds the shared guest
ELF. `.build_cva6_hdlcpp.sh` regenerates ordinary C++ from that snapshot, without
running any simulation optimizer. `.compare.sh` builds the five execution
variants, validates them against original Verilator, and prints build times,
median simulation-work times, cycle counts and PASS/FAIL. It does **not** invoke
either preparation script or `.build_all.sh`. `.build_all.sh` performs both
preparation steps followed by all five builds.

Each build also has an independent run entry point:

| Build | Run | Meaning |
| --- | --- | --- |
| `.build_cva6_cpphdl.sh` | `.run_cva6_cpphdl.sh` | Default C++HDL runtime; no comb or native-graph optimizer |
| `.build_cva6_cpphdl-optimize-combs.sh` | `.run_cva6_cpphdl-optimize-combs.sh` | Only `--optimize-combs`; no L1, math or threaded optimization flags |
| `.build_cva6_cpphdl-native-graph.sh` | `.run_cva6_cpphdl-native-graph.sh` | Native graph, one lane/thread |
| `.build_cva6_cpphdl-native-graph-4threads.sh` | `.run_cva6_cpphdl-native-graph-4threads.sh` | Native graph, four lanes/threads |
| `.build_cva6_verilator.sh` | `.run_cva6_verilator.sh` | Original SystemVerilog through original Verilator harness, one thread |

For bounded C++ compilation, both non-graph C++ modes use hdlcpp's build-only
`--optimize` specialization/splitting pass. This does not replace their runtime
scheduler or enable cpphdl comb optimization in the default execution mode.
Constructor/assignment units retain hdlcpp's generated `-O0`/no-inline build
settings; the host runner, cycle units and generated comb kernels use `-O2`.
Compiler-derived type identities deduplicate explicit instantiations before
bounded unity compilation, avoiding repeated full-header parsing without
rewriting any model equations or allocating a multi-gigabyte PCH.
Optimized unity batches contain at most four source units (eight for constructor
units); compiler memory exhaustion retries smaller batches automatically.
Content-keyed partition checkpoints avoid repeating failed large batches when
an interrupted build resumes. Constructor units retain the stage's address-space
limit; multi-source constructor batches and optimized batches have a stricter
local limit and split before exhausting the host's available RAM.
If one generated unit still exceeds that bound, partitioning continues at
explicit member-instantiation boundaries. Mixed singleton units separate
construction (`-O0`) from cycle methods (`-O2`), rather than optimizing large
constructor callback graphs together with simulation work.
Completed object files are compressed and content-checked between compilation
and linking: concrete template names otherwise consume hundreds of megabytes
per object. Linking uses anonymous Linux memory files, then strips the final
symbol table. No model operations are replaced. Verified compressed objects
survive failed builds; successful builds remove their temporary object cache.
Comb collection checkpoints also survive interrupted builds. Their cache keys
cover model/runtime headers, collection sources, optimizer binary and collection
driver; only atomically completed, checksum-verified collections are reused.
Multi-alias collections use a local 4 GB address-space limit and split after
memory exhaustion; single aliases retain the stage's 6 GB limit. Completed
child collections and split checkpoints survive retries as well.
Non-opaque markers explicitly require complete template definitions with
`sizeof`: a pointer declaration alone need not instantiate the marked class,
which otherwise leaves missing children when bounded collections are merged.
The comb collection path avoids a multi-gigabyte PCH; ordinary C++ execution is
not silently replaced by the faster native graph backend.

All scripts accept `--help`, `--work-root PATH`, `--force`, `--timeout SECONDS`,
`--keep-build-files`, and `--keep-traces`. Build caching checks input and binary
hashes. The per-stage timeout defaults to four hours to accommodate ordinary
full-model template compilation on one build CPU. Run scripts reject stale
binaries, changed source snapshots, compiler binaries and FESVR inputs. Temporary compilation outputs
are removed after successful linking, and comparison traces are removed only
after complete commit validation. Compact JSON reports and logs remain.

Comparison defaults to five alternating trials for each pair, CPU 2 for serial
execution (or the first allowed CPU if unavailable), and four allowed CPUs for
the four-thread candidate. Override with `--trials N`, `--cpu N`, `--cpus4 LIST`
or `--max-cycles N`. Four-thread timings must not be pinned to a single CPU.
The native build verifies the emitted lane count, rather than labeling a
silently reduced graph as four-threaded. DPI calls and their dependent cones
stay on the caller lane; independent pure cones execute in worker lanes.
Compilation/loading/initialization are excluded from simulation-work timing;
build and whole-process times are recorded separately. Failed validation never
produces a speed ratio. Compare requires matching checksum, reset/work/total
cycles and all ordered integer RVFI commits, plus actual native graph edges.
Build durations describe the successful invocation, which may reuse verified
objects or collection checkpoints from an earlier interrupted attempt; they are
not necessarily cold-build costs.

**Tracing caveat:** the original Verilator binary retains its architectural
trace formatting/output. C++ commit logging is enabled for validation only.
This intentionally repeats the accepted harness-level comparison, not a
trace-free engine benchmark. Exact wall times vary with host load; equivalence
and cycle counts, rather than a particular number of seconds, are enforced.

The small original ELF can be selected with `ELF="$PWD/matrix_multiply.riscv"`.
Use `python3 -B -m unittest discover -s . -p 'test_*.py'` for helper regressions;
set `CPPHDL_TEST_CXX` to enable the compiler-backed harness tests as well.
Set `CPPHDL_TEST_TOOL` to the cpphdl executable to exercise actual bounded AST
collection, including the incomplete-template regression.
Set `CPPHDL_TEST_HDLCPP` as well to verify concrete specialization preserves
the precedence of substituted non-type parameter expressions. Type parameters
remain type expressions; no numeric casts are introduced by this fix.

## Qualification (2026-10-22)

Fresh conversion emitted 287 headers with zero failures. The actual
`.compare.sh --trials 1` result, excluding compilation and initialization, is:

| Mode | Work seconds | Result |
| --- | ---: | --- |
| Original Verilator | 7.983710 | PASS |
| Default C++HDL runtime | 1089.619771 | PASS |
| Only `--optimize-combs` | — | BLOCKED: collection memory |
| Native graph, one thread | 4.859101 | PASS |
| Native graph, four threads | 3.185211 | PASS |

Every passing candidate matches the checksum, 396,778 total / 396,768 work
cycles and all 297,074 ordered integer commits. An additional five-trial native
qualification measured medians of 4.576255 seconds (one thread) and 3.097285
seconds (four threads); reference medians were 8.096051 and 8.004287 seconds.
These measurements retain the tracing caveat and constructor build policy
described above. All 59 helper regressions pass, including actual AST collection
and converter specialization.

Not all five modes currently pass. Comb collection completes 29 checkpoints,
then its next shard trips the available-RAM guard. Isolating its aliases shows
that `hpdcache_sync_buffer<0, cpphdl_opt_t205::mem_resp_metadata_t>` alone
exhausts a 4,000,000,000-byte address-space limit, but succeeds at the normal
6,000,000,000-byte limit using 4.04 GB resident memory. Collection now splits
multi-alias shards after local memory exhaustion, retaining the larger budget
for single aliases. These adaptive collection changes are being qualified;
the table above records the preceding complete comparison, not an unverified
comb-only performance claim. No native graph substitution is used.

The full comparison JSON is `<work-root>/comparison.json`; isolated memory
measurements and the single-alias reproducer are under
`<work-root>/qualification/combs-memory/`. A failed aggregate comparison exits
nonzero even when the other four rows pass.
