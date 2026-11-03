# OpenC910 conversion and execution comparison

This fixture is under qualification. Do not use timings from a failed build or
validation as performance results. `.compare.sh` prints execution times only
after checksum, retirement-trace and cycle-count checks pass.

The source is [XUANTIE-RV/openc910](https://github.com/XUANTIE-RV/openc910), pinned
to `b91c90914c19f114d35c8f6b73408eb241ed847c`. The upstream `smart_run` SoC holds
hart 1 in reset. Its physical RTL still contains two cores; the workload runs on
one active core in both flows.

The workload is the unchanged `../cva6/matrix_multiply_large.cpp`, compiled for
RV64 with `MATRIX_SIZE=32` and `MATRIX_REPEATS=1`. Both executables load the same
image. The expected line is:

```
CVA6_MMUL size=32 repeats=1 macs=32768 checksum=0xd687fdc5
```

Run from this directory:

```bash
./.build_openc910.sh
./.compare.sh
```

The preparation script downloads a sparse checkout and builds the bare-metal
image. `.compare.sh` builds and runs original Verilator with one and four
threads, converts the RTL with HDLCPP, then builds/runs native graph with one
and four threads, optimize-combs, and default C++HDL. Individual `.build_*` and
`.run_*` scripts perform those stages separately. Run original Verilator first
to establish the validation trace for subsequent backends.

Builds, generated sources, binaries and logs go to
`OPENC910_WORK_ROOT=/tmp/openc910-bash-$UID`. The scripts cap each stage's total
process-group resident memory at the machine's full physical RAM, without a
free-memory reserve or virtual-address-space cap. They also monitor writable
space, including the current user's tmpfs quota. Compilation is
serial; HDLCPP converts up to two independent modules concurrently and publishes
their metadata before converting parents. Verilator intermediates are removed
after successful validation.

Native lowering uses a precompiled-header cache assembled one dependency level
at a time, then releases it after serializing the hardware graph. This bounds
Clang's parsing memory and frees temporary space for native compilation. Both
thread counts share the checked hardware graph and use C++HDL's standard emitter
to generate their respective schedules. The first native build includes header
preparation and frontend lowering in its build time; later native builds reuse
the graph if the converter output, model wrapper, compilers and library headers
are unchanged. Delete `stamps/native-frontend` to force a fresh native frontend
measurement. All caches can be deleted with the rest of the work root.

The defaults use `~/cpphdl/.conda/bin/clang++`, `~/cpphdl/build/cpphdl`,
`~/cpphdl/hdlcpp/build/hdlcpp`, `~/cva6/tools/verilator-new/bin/verilator`, and
`~/riscv/bin/riscv32-unknown-elf-*` (this compiler also supports RV64). Override
with `CPPHDL_CXX`, `CPPHDL`, `HDLCPP`, `VERILATOR`, and `OPENC910_CROSS`.
`OPENC910_CPU` defaults to CPU 2; `OPENC910_CPUS4` defaults to CPUs 2–5.

The original testbench is copied with retirement observation ports and explicit
initial values for its two watchdog counters. Without those initial values,
zero initialization triggers its no-retirement watchdog at cycle 1, before the
first reset. No processor RTL is replaced. `model_top.h` exposes the converted
SoC's signals; `run_cpphdl.cpp` supplies the host-side memory load, reset sequence,
console and success monitor. The conversion emits ordinary C++ before any
C++HDL optimization.

The upstream bench leaves GPIO pads externally undriven. Conversion explicitly
marks those three hierarchy ports with `HDLCPP_OUTPUT_ONLY_INOUTS`, preserving
internal drive and readback with undriven bits zero in the two-state model.
This assumption does not cover a testbench that externally drives GPIO.

Each run script performs a traced validation and an untraced timed execution.
They must have identical cycle and retirement counts. Other backends must also
match original Verilator's ordered retirement PCs and counts. Execution time
excludes allocation and image loading but includes reset and simulation through
the success signature. Compilation and execution are reported separately.
