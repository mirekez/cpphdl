# Chipyard Rocket Simulation with C++HDL

This integration exports Chipyard FIRRTL directly to C++HDL and builds Rocket64
simulators. The SystemVerilog/Verilator flow is retained for comparison.

## Requirements

Use a prepared Chipyard checkout with its Conda environment, RISC-V toolchain,
and required submodules installed. The default location is
`$HOME/chipyard/chipyard`; `CHIPYARD_SOURCE_DIR` selects another checkout when
creating the `firtool/chipyard` link. An existing link is not retargeted.

Build/run scripts enforce the exact revisions in [revisions.json](revisions.json),
including nested submodules and the Rocket/Diplomacy overrides. CIRCT uses the
matching `firtool-1.75.0` SDK. Wrong revisions stop execution; existing repositories
are not switched or cleaned, and local patches at the pinned commits are preserved.
The installer builds a separate exporter without replacing Chipyard's firtool.

## Build and Run

Run these commands from `firtool/`, after configuring the main C++HDL build:

```sh
cmake --build ../build --target cpphdl --parallel 2
./.build_all.sh --run-tests
```

This validates the checkout, applies the integration/exporter patches, prepares
the SDK, builds firtool, then builds and tests all four backends. Omit
`--run-tests` to build only. Existing build artifacts are reused incrementally.

| Backend | Flow | Build Script | Run Script |
| --- | --- | --- | --- |
| Verilator | FIRRTL -> SystemVerilog -> Verilated C++ | `.build_rocket64_verilator.sh` | `.run_rocket64_verilator.sh` |
| Plain C++HDL | FIRRTL -> C++HDL hierarchy | `.build_rocket64_cpphdl.sh` | `.run_rocket64_cpphdl.sh plain` |
| Optimized combs | C++HDL -> scheduled comb functions | `.build_rocket64_cpphdl-optimize-combs.sh` | `.run_rocket64_cpphdl-optimize-combs.sh` |
| Native graph | C++HDL -> per-module graphs -> linked executable | `.build_rocket64_cpphdl-native-graph.sh` | `.run_rocket64_cpphdl-native-graph.sh` |

"Verilator" means Chipyard's RTL simulation flow. "Native graph" runs
hardware through C++HDL's graph runtime without Verilator, retaining C++ host
models for UART, TSI and DRAM. Individual build scripts do not run tests.

### Useful Controls

- `CHIPYARD_BUILD_JOBS`: Verilator build parallelism.
- `CPPHDL_BUILD_JOBS`, `CPPHDL_FIRTOOL_JOBS`, `CPPHDL_GRAPH_JOBS`: C++ compilation,
  exporter compilation, and graph-extraction jobs, respectively. Defaults are 1.
- `CPPHDL_OPTIMIZE_THREADS`: native-graph execution threads, including the caller;
  selected at build time, default 1. Three or more threads enable host-work overlap.
- `CPPHDL_REGENERATE=0`: reuse exported C++HDL;
  `CPPHDL_REOPTIMIZE_COMBS=0`: also reuse an optimized comb schedule.
- `CPPHDL_CONFIG`: configuration for C++HDL backends, default `RocketConfig`.
- `CPPHDL_TOOL`, `CPPHDL_GRAPH_CXX`: override the converter and graph host compiler.
- `CPPHDL_TIMEOUT`, `CPPHDL_MAX_CYCLES`: C++HDL run limits;
  `RV64_TIMEOUT`, `RV64_MAX_CYCLES`: Verilator run limits.

For example, build and run a three-thread graph simulator:

```sh
CPPHDL_BUILD_JOBS=2 CPPHDL_GRAPH_JOBS=2 CPPHDL_OPTIMIZE_THREADS=3 \
  ./.build_rocket64_cpphdl-native-graph.sh
./.run_rocket64_cpphdl-native-graph.sh
```

Artifacts and logs are under `chipyard/cpphdl-build/RocketConfig/`: `plain/` for
plain C++, `native-graph/` for graphs, and the directory itself for optimized
combs. Graph extraction caches are in `native-graph/runtime/partitions/`.
Verilator artifacts are under `chipyard/sims/verilator/`.

## Current Measurements

Measured on **2026-10-12**, on a four-vCPU Ryzen 5 5600H VM. Each executable was
compiled from fresh model/object directories with **two build jobs**. Runs were
sequential, with no competing builds; execution times are medians of three runs.

The workload was the same `rocket64-mmul.riscv` ELF: **16x16 integer matrix
multiplication, eight rounds**. Both flows used ELF preload and the default
`mm_magic_t` memory model, without DRAMSim timing or waveform tracing. Verilator
5.022 used GCC 13.2 at `-O3`; native graph used Clang 21.1.3 at `-O2`, with the
default lane policy. Thread counts below are execution threads, not build jobs.

| Backend | Threads | Clean C++ Compile/Link | Run Median |
| --- | ---: | ---: | ---: |
| Verilator | 1 | 111.64 s | 163.57 s |
| Verilator | 2 | 115.23 s | 105.93 s |
| Native graph | 1 | 34.68 s | 16.30 s |
| Native graph | 2 | 41.67 s | 11.93 s |
| Native graph | 3 | 44.88 s | **11.68 s** |
| Native graph | 4 | 45.72 s | 15.22 s |

**Conversion is separate from C++ compilation:** Verilator's SystemVerilog-to-C++
generation took 19.97 s for one thread and 20.13 s for two. The graph flow took
18.35 s to export FIRRTL to C++HDL, 423.47 s to extract all 389 module types, and
3.64 s to link/emit the three-thread model. Extraction was fresh and shared across
the graph thread variants; subsequent builds can reuse unchanged partitions.
These timings exclude Chisel elaboration, FIRRTL-to-SystemVerilog export,
toolchain installation, building the converters, and CMake configuration.

Three-thread native graph had the lowest measured runtime: **9.07x faster than
two-thread Verilator**, and 14.00x faster than one-thread Verilator. Two-thread
graph was almost as fast while using less CPU time; four threads were slower.
Native graph compiles and runs faster here, but its cold frontend conversion is
more expensive. These are complete workload timings with different simulation
engines, not a universal speedup or proof of cycle-by-cycle equivalence.

All **18 timed runs passed** with signature `0xe49d58d75696cd28`; graph runs each
completed in 695870 cycles. Plain C++ and optimized-comb backends also passed the
matrix smoke test, but were not included in this timed comparison.

To reproduce the Verilator preload setting, pass `+loadmem=<ELF>` inside
`+permissive ... +permissive-off`; the benchmark also used `+verilator+seed+1`.
The default `.run_rocket64_verilator.sh` does not add preload, so its startup time
can be longer. The graph runner preloads automatically. Changing Verilator's
`VERILATOR_THREADS` requires regenerating its model, not just rerunning make
against cached artifacts.

## Integration Checks

From the repository root, run the focused build/patch/exporter regressions:

```sh
ctest --test-dir build --output-on-failure \
  -R '^firtool_(versions|reuse_chipyard|build_all|chipyard_patch|rocket_backends|emitter_support)$'
```
