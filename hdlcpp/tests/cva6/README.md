# Fixed CVA6 Bash fixture

This directory runs one configuration: `cv32a6_imac_sv32`, one core, a 32x32
integer matrix multiply repeated once (32,768 MACs). It uses the qualified
`~/cva6` checkout at `9c2cf60a83a0a6a7fc5dcd973a5a5f6c0f42cb75`, including
its existing harness/instrumentation changes and checked-out dependencies.
`checkout.sha256` identifies the exact source files; a different working tree
is rejected, not silently treated as the same experiment. No checkout is
modified and no repository artifacts are generated in this directory.

## Build and run

```bash
cd ~/cpphdl/hdlcpp/tests/cva6
./.build_cva6.sh
./.build_cva6_hdlcpp.sh
./.build_cva6_verilator-4threads.sh
./.build_cva6_cpphdl-native-graph.sh
./.build_cva6_cpphdl-native-graph-4threads.sh
./.build_cva6_cpphdl-optimize-combs.sh
./.build_cva6_cpphdl.sh
./.run_cva6_verilator.sh
./.run_cva6_verilator-4threads.sh
./.run_cva6_cpphdl-native-graph.sh
./.run_cva6_cpphdl-native-graph-4threads.sh
./.run_cva6_cpphdl-optimize-combs.sh
./.run_cva6_cpphdl.sh
```

`.build_cva6.sh` verifies and snapshots the particular checkout under `/tmp`,
builds the fixed RV32 ELF, and builds original Verilator. The independent
`.build_cva6_verilator.sh` rebuilds only that simulator from the snapshot.
`.build_cva6_hdlcpp.sh` regenerates ordinary C++ from SystemVerilog, including
metadata inference and field-trait convergence. It does not optimize simulation.

The four C++ build scripts consume that same ordinary conversion:

| Suffix | Execution |
| --- | --- |
| `cpphdl` | Default C++HDL; no comb or native-graph optimization |
| `cpphdl-optimize-combs` | Only `--optimize-combs` |
| `cpphdl-native-graph` | Native graph, one thread |
| `cpphdl-native-graph-4threads` | Native graph, four threads |

Non-graph builds use HDLCPP's build-only specialization to split templates;
initialization/binding units use `-O0`, cycle methods and comb kernels `-O2`.
Native graph and the host runner use `-O2`; original Verilator keeps its original
Makefile build policy. No backend is substituted for a requested mode.

Each run requires checksum `0xd687fdc5`, 396,778 total cycles (10 reset plus
396,768 work), and 297,074 integer commits. C++ runs first compare the complete
ordered commit stream with Verilator, then run a separate timed trial without
C++ commit logging. Original Verilator retains its trace formatting overhead.
Only the simulator's work timer is reported, not compilation, startup, or the
separate validation run. Results are in `<work-root>/runs/<mode>/result.txt`.

For the full six-mode table, including original Verilator with four simulation
threads, run `./.compare.sh`. It builds the shared conversion and every backend,
runs each workload, and writes `comparison.tsv` under the work root. The extra
standalone entry points are `.build_cva6_verilator-4threads.sh` and
`.run_cva6_verilator-4threads.sh`. Both Verilator modes retain their original
trace formatting, and the four-thread trace must match the one-thread trace.
The build makes a temporary copy of the host harness for both thread counts,
omitting one optional debug-print column (`iq_push`): its internal signal is
optimized away in the threaded model. The RTL, workload loop, timing and commit
tracing are unchanged, and the qualified source checkout is not modified.

Use `./.compare.sh --rebuild` for fresh compilation measurements: this regenerates
the conversion and discards each backend's partial object cache. Compilation
time includes backend lowering, compiler work, and linking. Shared SV-to-C++
conversion is reported separately, as are simulator work times; source
preparation, ELF compilation, and C++ validation runs are excluded. Compiler
jobs remain serial in every mode; four threads refers to simulation.

A cache hit retains the timing recorded with that exact successful build; it
does not report the cache lookup as compilation. Older builds without timing
records say `unmeasured`. A resumed build is labelled `resumed` and records only
that successful attempt. All reported work times are new runs.
Changes to reporting or validation do not invalidate compiled models; build
functions, model inputs, tools, and runtime headers remain in the cache key.
C++ validation output is buffered in memory before a checked log write, and
missing or malformed commit records fail validation. Timed runs are unbuffered.

## Files and dependencies

The dot-prefixed entry points are Bash. `build.sh` shares the fixed build,
resource-limit, cache, and run logic; `convert.sh` performs only conversion.
There is no Python workflow, replay directory, or support directory here.
The C++HDL tool may use its own internal implementation outside this fixture.

`conversion.in` is readable fixed configuration data: conversion settings,
manual metadata, source/header order, and the existing inactive-peripheral,
clock-cell, DPI and RVFI adapters. Inferred metadata is regenerated, not frozen.
The `run_*` C++ sources are the actual host/model adapters, not replacement CPU
implementations. `link_objects.cpp` only exposes compressed objects as anonymous
Linux files for the linker, avoiding multi-gigabyte temporary symbol tables.

Required tools: Bash, GNU awk/sed/coreutils, tar, gzip, flock, taskset, GNU make,
Clang with C++23 support, HDLCPP, C++HDL, Verilator,
and the qualified `~/riscv` RV32 compiler/FESVR installation.

Defaults can be relocated with environment variables, not a general-purpose
command-line framework:

```bash
export CVA6_SRC=$HOME/cva6
export CVA6_WORK_ROOT=/tmp/cva6-bash-$UID
export RISCV=$HOME/riscv
export CVA6_CPU=2 CVA6_CPUS4=2,3,4,5
```

`HDLCPP`, `CPPHDL`, `CPPHDL_CXX`, and `VERILATOR` override tool paths.
Each entry point accepts `--help`. Use the same environment for build and run.
The workspace must be below `/tmp`; builds are serialized, capped at 6 GB
address space, and stopped below 600 MiB available RAM or 512 MiB temporary
space. Large compiler/collector batches split on memory exhaustion. Verified
compressed checkpoints survive failure; build objects and validated traces are
removed after success. Build/run stamps reject changed inputs or binaries.

## Regression checks

Small Bash regressions live with the compiler tests, not in this fixture:

```bash
bash ../../../tests/hdlcpp/Cva6ScriptsTest.sh ../../../.conda/bin/clang++
bash ../../../tests/hdlcpp/SpecializationPrecedenceTest.sh ../../build/hdlcpp ../../../.conda/bin/clang++
```

## Full-run qualification, 2026-10-27–28

The six-mode comparison uses the same qualified checkout and ELF. Shared fresh
HDLCPP conversion took **2070.827828 seconds** (284 sources, seven metadata
passes); add this once when measuring the complete C++ build flow.
The complete `.compare.sh` run exited successfully. All six modes produced
checksum `0xd687fdc5`, 396,778 total cycles, and the same 297,074 ordered integer
commits on one simulated core.

| Mode | Backend compilation (seconds) | Build | Work time (seconds) | Validation |
| --- | ---: | --- | ---: | --- |
| Original Verilator, one thread | 213.878239 | fresh | 8.056821 | PASS |
| Original Verilator, four threads | 218.583698 | fresh | 17.101960 | PASS |
| Native graph, one thread | 228.252101 | fresh | 4.662279 | PASS |
| Native graph, four threads | 243.057878 | fresh | 3.747385 | PASS |
| C++HDL `--optimize-combs` | 17846.411969 | fresh | 1053.322812 | PASS |
| Default C++HDL | 5429.854042 | resumed | 2684.232810 | PASS |

The default build resumed verified object checkpoints after resource
interruptions. Its reported compilation time covers only the successful
attempt; earlier interrupted attempts add approximately 8251 seconds based on
their filesystem timestamps. It is not a cold-build measurement. That final
compiler invocation used system jemalloc through `LD_PRELOAD`; the resulting
simulator has no jemalloc dependency and every simulation unsets `LD_PRELOAD`.

Execution measurements are single timed trials of the same ELF and workload,
excluding builds and separate C++ commit-validation runs. Original Verilator
still formats its trace, so these are fixture execution times, not a comparison
of trace-free simulation kernels. Verilator warned that its scheduler could not
provide the requested four-thread parallelism. Measurements were taken on a
shared machine with other work running, rather than an otherwise idle host.
The measured binaries also used a separate local native-thread wait-policy
update with a default 4096-iteration spin window. That pre-existing runtime
change is outside this fixture commit; a checkout with the older 64-iteration
window can produce different four-thread native timings.

The final comparison reuses the exact checksum-verified binaries and their
recorded build times, and performs new validation and timed runs. Successful
builds remove intermediate objects, and successful validation removes its large
traces. Compact logs, binaries, source snapshot, and results remain under
`/tmp/cva6-bash-1000`.

The Bash fixture regression and specialization-precedence regression passed.
The existing chunked-host, native-split, and C++ graph suites also passed after
updating their adapter lookup to read `conversion.in`.
