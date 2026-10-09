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

[`dram_runtime.cpp`](../dram_runtime.cpp) is a manual reproducer for shared DRAM
backing, byte masks and independent response lifetimes. It currently exits with
code 1 on the first cross-instance read; it is not a passing CTest regression.

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
