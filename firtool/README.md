# C++HDL simulation of Chipyard Rocket

`./.build_chipyard.sh` reuses an already prepared Chipyard checkout through
`firtool/chipyard`, builds the direct C++HDL Rocket simulator, then runs the RV64
matrix test. The default checkout is `$HOME/chipyard/chipyard`; set
`CHIPYARD_SOURCE_DIR` on the first run to select another existing checkout.
An existing link is never retargeted. Set `CHIPYARD_RUN_SMOKE_TESTS=0` to build
without running the simulator.

The three build entry points select distinct cpphdl backends:

| Script | Backend | Default host compiler flag |
| --- | --- | --- |
| `.build_rocket64_cpphdl.sh` | Direct generated C++, `_assign` / `_work` / `_strobe`; no cpphdl optimizer | `-O2` |
| `.build_rocket64_cpphdl-optimize-combs.sh` | `cpphdl --optimize-combs TestHarness` | `-O2` |
| `.build_rocket64_cpphdl-native-graph.sh` | Per-module C++ graph extraction, linked native graph | `-O2` |

Build scripts do not run simulation. The baseline requires cpphdl headers but
no cpphdl compiler executable. The other scripts use `../build/cpphdl` by
default, overridable with `CPPHDL_TOOL`; rebuild that tool after source changes:

```sh
../.conda/bin/cmake --build ../build --target cpphdl --parallel 1
./.build_rocket64_cpphdl.sh
./.run_rocket64_cpphdl.sh plain
./.build_rocket64_cpphdl-optimize-combs.sh
./.run_rocket64_cpphdl-optimize-combs.sh
./.build_rocket64_cpphdl-native-graph.sh
./.run_rocket64_cpphdl-native-graph.sh
```

All modes use the same FIRRTL and matrix ELF. `CPPHDL_OPT_LEVEL` overrides the
host compiler flag for direct and comb builds; keeping it equal makes backend
performance comparisons useful. Native graph's existing driver compiles runners
at `-O2`. `CPPHDL_TIMEOUT` and `CPPHDL_MAX_CYCLES` bound matrix runs.

The existing comb artifacts stay at `chipyard/cpphdl-build/RocketConfig/`, so
previous CMake caches remain usable. Direct artifacts and logs live in `plain/`
under that directory; graph artifacts live in `native-graph/`. Each backend has
its own generated model and runtime directory. `CPPHDL_OUTPUT_DIR` and
`CPPHDL_BUILD_DIR` override these defaults; use the same overrides when running.
`CPPHDL_REGENERATE=0` reuses an existing exported model;
`CPPHDL_REOPTIMIZE_COMBS=0` reuses an existing comb schedule.

Graph extraction lowers each reachable generated module type independently,
then links its instances into one native graph. Child work enables and reset
arguments cross explicit partition boundaries. The generated runner connects
the existing UART, DRAM and TSI host models; ordinary hardware executes as a
graph. Native evaluation is split into smaller functions to limit compiler
memory use while retaining `-O2`.

`CPPHDL_CONFIG` selects the Chipyard configuration (default `RocketConfig`) for
both build and run scripts. The native-graph build also accepts these optional
extensions, without changing TestChipIP or other external projects:

- `CPPHDL_COMBINATIONAL_HOST="TypeA TypeB"`: settle the named external types
  before sampling each clock edge. Supply a `firtool_cpphdl_external::eval(Type&)`
  overload in `cpphdl_external_models.h` for each type. It may update combinational
  outputs, but must not advance state, consume transactions, or perform I/O.
  The runner rejects boundaries that do not settle within 16 passes. Work and
  strobe still run once per simulation cycle under the existing host enables.
  Leave this unset for ordinary clocked UART, TSI and DRAM models.
- `CPPHDL_EXTRA_SOURCES` and `CPPHDL_EXTRA_INCLUDES`: colon-separated paths for
  custom host implementations and headers. Use absolute paths; spaces are allowed.
- `CPPHDL_EXTRA_DEFINITIONS`: shell-quoted compile definitions, without `-D`.

The partition tool exposes the same opt-in as repeatable `--combinational-host`.
The installer applies the runtime-options upgrade to older backend installations
as well as fresh checkouts, and stops on conflicting local script edits.
DRAM instances retain their existing independent backing; these options do not
implicitly connect separate memory ports to one shared memory image.

After linking and validation, native emission removes registers that cannot
affect an output, memory operation, or host effect, including through future
register updates. Clock triggers, resets, memory bounds checks and host call
ordering remain observable. Partition files retain all state for later linking;
internal register fields in the emitted model are not a stable debug interface.

`CPPHDL_OPTIMIZE_THREADS` now also selects native graph evaluation threads,
including the calling thread (default 1, supported range 1–256):

```sh
CPPHDL_OPTIMIZE_THREADS=3 ./.build_rocket64_cpphdl-native-graph.sh
./.run_rocket64_cpphdl-native-graph.sh
```

The count is selected at build time. `CPPHDL_GRAPH_JOBS` still controls graph
extraction jobs, and `CPPHDL_BUILD_JOBS` controls host compilation jobs.
Standalone native compilation accepts `cpphdl --native-graph --optimize-threads=3`
with its usual source, output and runner arguments. Graph-link also accepts an
optional thread count after its chunk-size argument; that overrides the environment.

Parallel native evaluation uses persistent workers with complete combinational
cones feeding outputs, register updates, and memory operations. The partitioner
groups overlapping cones and balances estimated work. Shared pure operations
can be duplicated into separate lane storage, keeping producer and consumer
logic together without exchanging intermediate values between workers. Only
the dispatch and final join synchronize lanes. Each lane assembles its assigned sinks locally, including register updates,
instead of returning intermediate values to the caller for assembly. Register banks are laid out by owner, with separate
cache lines at lane boundaries. Workers read the current bank and write the
inactive bank. Registers use the smallest unsigned byte/word type that holds
their width, grouped by storage size within each lane; reads explicitly widen
to 64 bits before graph arithmetic. The serial backend uses the same compact
register storage. RAM remains read-only during evaluation.

For linked host models with three or more requested threads, the caller owns
host-visible outputs, all memory validation, and staged RAM writes. Other lanes
prepare next-state registers. After completing its lane, the caller validates
and publishes outputs, then runs external host models while the register lanes
are still working. RAM writes and register-bank publication wait until every
lane finishes. This overlaps host work that previously ran after the join.
One- and two-thread linked models retain their existing synchronous schedules.

The `evaluate_with_host(callback)` continuation may read published output ports
and update independent external models. It must not change graph inputs,
clocks, RAM or state, or reenter an executor shared by a model copy. The generated
runner honors this boundary. Failed graph validation skips the callback and
publishes neither outputs nor state. If a host callback throws, workers drain
and the graph transaction commits before the exception propagates, matching
synchronous graph-then-host execution. Ordered updates to the same register
stay on one lane. Host transfers use byte/word packing in all thread modes.

The latest 2026-10-09 comparison restricted every process to CPUs **0,1,2**;
CPU 3 was excluded. Four complete runs per configuration, in alternating order,
measured these medians on the same RV64 matrix workload at `-O2`:

| Threads | Before this change | Host overlap + compact registers | New wall-time range |
| --- | ---: | ---: | ---: |
| 1 | 16.28 s | 16.19 s | 15.86–16.60 s |
| 3 | 12.81 s | 10.81 s | 9.77–11.21 s |

Three-thread wall time fell **15.6%**; scaling against the current single-thread
runner is **1.50x** (previously 1.27x in this same comparison). Median total CPU
time fell from 37.29 to 30.86 seconds for three threads, versus 16.17 seconds for
one. This improves throughput but still falls well short of linear scaling.
The original three-thread range was 11.39–14.02 seconds; VM timing variation
remains substantial. All 16 runs passed with signature `0xe49d58d75696cd28`
and exactly 695870 cycles. No compilation ran during these measurements.

The register-bank payload shrank from 31896 to 6856 bytes (before lane alignment).
Separate two-round comparisons measured compact storage at 8.39 versus 9.87
seconds with host overlap held constant, and host overlap at 10.33 versus 11.59
seconds with compact storage held constant. Those pilots occurred at different
times; the four-round table above is the final comparison. Compiling host wrappers
together, preparing next-cycle inputs during the callback, separating input/output
cache lines, and assigning additional register cones to the caller were tested
and dropped because they did not produce a repeatable gain above 10%.

Results, hashes, process snapshots, profile samples and experiment scripts are
in `benchmarks/20261009-host-overlap/`; `summary.json` contains the table data.
The validated three-thread executable is selected by the normal
`.run_rocket64_cpphdl-native-graph.sh` runner. It was rebuilt from the unchanged
saved linked graph and existing external-model objects, avoiding a full frontend
re-extraction. Its runtime attempt contains `provenance.json`, including the
previous executable for rollback. To reproduce the CPU restriction, run:

```sh
taskset -c 0,1,2 ./.run_rocket64_cpphdl-native-graph.sh
```

Mixed-width registers (including high bits and overflow), explicit/named clock
edges, resets, ordered RAM writes, validation rollback, callback exceptions and
model copies pass the UBSan host-overlap regression. The original C++ hierarchy
comparison and 24576 phase evaluations also pass. Remaining costs include serial
host-input preparation, per-cycle synchronization, and duplicated combinational
cones; the final three-thread model schedules 45323 nodes versus 33867 serially.

The 2026-10-09 state-ownership comparison on a four-vCPU VM measured the following
median wall times (`-O2`, two complete quiet rounds per configuration):

| Threads | Previous cone scheduler | Worker-owned state + packed host transfers |
| --- | ---: | ---: |
| 1 | 18.25 s | 16.51 s |
| 2 | 17.02 s | 13.40 s |
| 3 | 17.74 s | 12.55 s |
| 4 | 20.37 s | 14.50 s |

In that earlier measurement, three threads reduced wall time by 29% versus the previous three-thread runner,
and 31% versus the previous one-thread baseline. Relative to the improved
one-thread runner, three threads provide 1.32x throughput, using more total CPU
time. Four threads still trail three; threading remains opt-in with default 1.
All runs produced the same signature and 695870 cycles. Competing workloads
interrupted other rounds; those timings are retained separately and excluded
from this table. Raw comparisons, ranges, build artifacts and checks are in
`benchmarks/20261009-state-ownership/`. The earlier cone-scheduling comparison
is retained in `benchmarks/20261009-thread-diagnosis/`.

Graph attempts preserve linked models, generated runners and stage logs in
`native-graph/runtime/attempt.*/`; source wrappers, extraction logs and cached
module graphs and link plans live in `runtime/partitions/`. A failed graph build returns failure
and does not run another backend. A successful build publishes the runner as
`runtime/cpphdl-rocket64-graph-sim`. `CPPHDL_GRAPH_CXX` selects its host compiler. Optional
`CPPHDL_GRAPH_MAX_VMEM_KB` caps its virtual memory (for example, `6291456` for
6 GiB); no memory cap is imposed by default. Core dumps are disabled to protect
disk space during compiler experiments. `CPPHDL_GRAPH_JOBS` controls extraction
parallelism (default one); each worker needs its own frontend memory.

Initial validation on 2026-10-08, before ROM preservation: all three backends passed the existing 16x16, eight-round
RV64 matrix test at `-O2`, with signature `0xe49d58d75696cd28`. The direct backend
used 695720 cycles; comb and native graph each used 695870 cycles. Native graph
took 240.30 seconds on this host. Its successful build with cached extraction
took 371.10 seconds and peaked at 2.32 GiB RSS under a 4 GiB virtual-memory cap;
that build time excludes initial module extraction.

That graph build extracted 389 module types and linked 139337 nodes with 5397 state
groups. Partitioning removes the whole-hierarchy AST bottleneck. Narrowing
right shifts also retain only the bits needed by their result, reducing the
boot-ROM extraction from about 1.94 million nodes before allocation failure to
32273 nodes. The frontend handles firtool's owned constructors, memoized getters,
byte memories, masked concatenation slices, constexpr word factories, and wide
equality/unsigned ordering. Reduced value, hierarchy, host-boundary and ROM
scaling regressions pass. These results validate this Rocket workload, not every
possible design or peripheral interaction.

Constant ROM lookup preservation now keeps firtool's supported `array_get`
helper as indexed reads from static memory. The actual helper body is checked;
modified helpers and nonconstant packed arrays retain ordinary C++ lowering.
ROM contents survive module serialization and linking, including multiword
rows. Writable RAM retains its existing transaction behavior. Rocket's boot-ROM
partition falls from 32273 nodes to 18. The `cpp_graph_constant_memory` and
partition tests cover values, bounds, wrapping indices, serialized contents,
changed helpers, and independent RAM/ROM instances.

The rebuilt Rocket graph has 106103 nodes and the same 5397 state groups.
A sequential, uninstrumented comparison on 2026-10-08 passed the identical
695870-cycle matrix workload in 84.19 seconds after ROM preservation versus
275.38 seconds for the saved old runner (3.27x faster). Both produced signature
`0xe49d58d75696cd28`. This is one run per version on the shared host. Full
extraction/link/compile took 786.16 seconds with two extraction workers and one
compiler worker, peaking at 1339284 KiB RSS under a 4 GiB virtual-memory cap.
Measurements, hashes and validation logs are in
`benchmarks/20261008-memory-preserved/`.

The reuse path preserves Chipyard's Conda environment, submodules, local FPGA
and Ethernet changes, and native simulator. It does not clone another Chipyard
checkout or run fresh-checkout cleanup. Native helper functions remain available
for `.build_rocket64_native.sh`.

The exporter uses a shallow checkout of CIRCT commit
`481cb60add7358934414a3c6b396f5d29ad934fe` and the matching firtool 1.75.0 prebuilt
shared SDK (about 93 MB compressed). The SDK is installed separately at
`chipyard/tools/circt-cpphdl-sdk`; the download is checksum-verified and removed
after extraction. `CPPHDL_CIRCT_PREFIX` selects an existing compatible SDK.
The normal Chipyard firtool binary is not replaced.

Compilation defaults to one job and disables the large optimized-model PCH.
Override `CPPHDL_BUILD_JOBS`, `CPPHDL_FIRTOOL_JOBS`, or
`CPPHDL_USE_OPTIMIZED_PCH` when resources permit.

The integration patch adds the `rocket64-mmul` test without replacing local DMA
or DDR test targets. Check script dispatch and bootstrap preservation with:

```sh
python3 tests/test_rocket_backends.py
python3 tests/test_reuse_chipyard.py
```
