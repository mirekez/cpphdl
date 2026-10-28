# OpenMP Loops as Parallel Hardware

These are ordinary C++ algorithms with OpenMP pragmas. GCC and Clang run the
iterations on host threads; C++HDL elaborates the bounded iterations into
independent hardware lanes inside `ClockedPipeline<PipelineMethods, 3>`.
The same source and `main()` test generated SystemVerilog and retimed gate-level
Verilog with Verilator. No OpenMP runtime is embedded in the hardware.

| Example | Parallel work |
| --- | --- |
| `ParallelFor.cpp` | Eight arithmetic lanes, disjoint array writes, static scheduling, SIMD sum |
| `Reduction.cpp` | Seven descending/strided lanes; independent `+`, `*`, `&`, `|`, `^` reduction trees |
| `Collapse.cpp` | A two-dimensional calculation, `collapse(2)`, and a matrix reduction |
| `Private.cpp` | Private scratch and firstprivate input; caller values remain unchanged |
| `Boundaries.cpp` | Empty and singleton loops, constexpr bound, broadcast array read, nonzero initial reduction value |
| `Tree.cpp` | Parametric XOR/sum lanes; scaling regression checks logarithmic reduction depth |
| `Float.cpp` | FP32 +/* reductions, private/firstprivate float, pure templated helpers, fixed loops and guarded neighbor snapshots |
| `Wide.cpp` | 224-bit typed records, math namespace templates, SV keywords, array aliases, loop declarations and two concurrent workers |
| `Grid.cpp` | 64x64 (4,096) lanes with a balanced reduction; native/RTL structural scaling |
| `StateLanes.cpp` | Eight disjoint persistent history registers and read-after-write within each lane |
| `RowContexts.cpp` | Per-row dynamic columns bounded by modulo/masks, including shared read-only index arrays |

## Build and Test

From the repository root, for example:

```sh
g++ -O2 -std=c++17 -fopenmp -Iinclude hls/examples/openmp/ParallelFor.cpp -o /tmp/openmp-example
OMP_NUM_THREADS=2 /tmp/openmp-example

clang++ -O2 -std=c++17 -fopenmp -Iinclude hls/examples/openmp/ParallelFor.cpp -o /tmp/openmp-example-clang
OMP_NUM_THREADS=2 /tmp/openmp-example-clang

build/cpphdl --generated-dir=build/openmp-rtl hls/examples/openmp/ParallelFor.cpp -- -Iinclude -fopenmp

build/cpphdl --synth --top PipelineTop --module PipelineTop \
  --output build/openmp-gates --retiming fit_pipeline_retiming \
  --clock-period-ns 2.0 hls/examples/openmp/ParallelFor.cpp -- -Iinclude -fopenmp

ctest --test-dir build -R '^hls_openmp_' --output-on-failure
```

Clang native compilation requires an OpenMP runtime and `omp.h` (usually
libomp); GCC uses libgomp. The converter needs `-fopenmp` after `--` to parse
the directives; it does not link an OpenMP runtime while converting the source.
The CTest runners use the configured HLS Clang/libc++ toolchain and additionally
test GCC when it is installed.

Each native and RTL run checks 1,000 randomized OpenMP results against an
independent scalar reference. The shared streaming test then exercises 1,024
consecutive input clocks, randomized branches and values, prolonged output
backpressure, reset during traffic, ordered tags, faults, and pipeline drain.
For the gates flow, the runner reads the actual latency from `timing.json` and
uses that latency in the same testbench. It checks II=1 and the estimated timing
target; the arithmetic-lane case also requires added pipeline stages. These
estimates are not FPGA timing closure.

The gate-test runner disables DFG optimization specifically for Verilator 5.050:
that release incorrectly folds a shared XOR cone in `RowContexts` to zero.
The same netlist passes with `-fno-dfg`; all data, backpressure, II=1 and timing
checks remain enabled. Other Verilator versions use their default optimizations.

## Hardware Contract

- Supported directives: `parallel for`, `parallel for simd`, and `simd`.
- Bounds and stride are compile-time integers. Induction variables have at most
  32 bits. Ascending/descending loops, `<`, `<=`, `>`, `>=`, `++`, `--`, `+=`,
  and `-=` are supported. The combined iteration count is at most 4,096 lanes.
- `collapse(1..3)` requires perfectly nested rectangular loops.
- Private/firstprivate and reduction clauses name automatic locals or function
  arguments, not globals. Each iteration has separate locals and scratch. Firstprivate
  values are copied from the enclosing calculation, not written back.
- Private values must be initialized in the lane before reading. A mutated
  firstprivate value must also be overwritten before reading: otherwise its
  behavior may depend on which earlier iterations a host thread executed.
- Unsigned integer reductions, up to 64 bits, use balanced trees. The initial
  enclosing value is combined exactly once, including when it is nonzero.
  Empty loops leave it unchanged. Unsigned arithmetic retains its C++ wrapping
  behavior; FP32 +/* reductions use a registered floating policy; signed/f64/custom reductions remain unsupported.
- Shared writes require a fixed array with bounded affine indices
  and constant guards that prove lane separation. Array reads/writes are checked for bounds and
  cross-lane dependencies. These small arrays become direct wires/values, not
  automatically multiported RAM. Larger lane counts cost more arithmetic area.
  Unsigned dynamic indices bounded by `% constant` or a nonnegative constant
  mask are also accepted when all possible addresses prove lane separation.
  Reads used to compute an index participate in the same dependency checks.
- `schedule(static[, constant_chunk])`, `shared`, `default(shared)`, and
  `default(none)` are supported. Host scheduling does not change lane allocation.
- Runtime bounds, dynamic scheduling, tasks, atomics, critical sections,
  lastprivate, pointer accesses and impure calls are rejected. Pure arithmetic
  value helpers and canonical fixed control loops are accepted after validation. This is a hardware subset of
  OpenMP, not a general host-thread emulator.

Fixed canonical loops in a pipeline are elaborated; delayed wrappers retain their sequential loop scheduling. A parallel loop
does not introduce an FSM back-edge: independent lane calculations can overlap.
Balanced reductions have logarithmic combine depth rather than a serial
accumulator dependency. `ClockedPipeline` supplies the streaming handshake and
stage boundaries; `fit_pipeline_retiming` subdivides the arithmetic further
using the selected clock-period estimate, while aligning result and tag paths.
Use `ClockedMemory`/a memory controller separately for variable-latency DDR
accesses; these pragmas do not invent additional external-memory ports.

FP32 operators and reductions use explicit `HLS_OVERRIDE_BITS` policies.
Pure helper calls are checked transitively before lane lowering. Constant `if`
clauses affect host team creation but preserve the full hardware lane count.
Reconvergent command control paths preserve the activation of their dominating
fork, with the current fault guard retained. This prevents independent arithmetic
lanes from acquiring false dependencies through inlined helper branch/return
predicates. It does not remove real accumulator or memory dependencies.

`StateLanes.cpp` also exercises the existing floating-feedback pipeline
contract: each invocation calculates from the state available at its input
stage, and its update commits later. It is not a promise of sequential C++
state evolution across overlapping calls. The shared test models those delayed
commits and adjusts their latency after retiming. Use `ClockedDelayer` if a call
must observe every preceding call's updates before it starts.

The rejection regressions are in `hls/tests/OpenMPReject.cpp`. Semantics for
[OpenMP loop constructs](https://www.openmp.org/spec-html/5.0/openmpsu41.html)
and [reductions](https://www.openmp.org/spec-html/5.2/openmpsu52.html) provide
the host-language contract; the checks above restrict it to independent RTL.
