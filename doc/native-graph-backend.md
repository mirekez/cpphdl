# Native value-graph backend

`cpphdl --native-graph` implements an opt-in simulation representation with
two inputs: ordinary CppHDL C++ elaborated by Clang, or graph-construction C++
from the preserved `hdlcpp --native-graph` Slang frontend. Both use the same
CppHDL graph compiler and a host C++ compiler. **Neither invokes sv2v,
Yosys, CXXRTL, or Verilator.** The older callback/L1 path remains available;
this is a replacement representation, not another callback scheduling pass.

## Ordinary C++ frontend

`cpphdl --native-graph --top ROOT_VARIABLE` also accepts ordinary CppHDL C++:

```sh
hdlcpp /path/to/Design.sv
# model.cc includes generated/Design.h and defines: Design root;
cpphdl --native-graph --top root --frontend-flag=-I/external/work \
  --cxx clang++ --output /external/work/native \
  --runner /path/to/Run.cc /external/work/model.cc
```

There is **no graph flag on hdlcpp** in this flow. Clang elaborates the actual
C++ templates inside cpphdl; `CppGraph.h` lowers hardware types, connections,
procedural values and explicit register transactions into the same graph
backend. The generated C++ and its included headers are authoritative. The
second stage neither reads the original SV nor reconstructs SV from C++.

Ordinary hdlcpp output includes `__cpphdl_net_STORAGE` metadata for methods
consisting solely of concurrent continuous assignments. This leaves their
existing C++ bodies and ABI unchanged. It distinguishes declarative net
equations from imperative code that retains previous storage: the optimizer
must not infer concurrency merely from a getter's name. cpphdl checks each
marked net for at most one effective write per bit, resolves forward references, and
requires an acyclic live dependency graph. Unmarked incomplete writes are
rejected, not silently reinterpreted as nets.
Unwritten bits may be discarded only when dead; a live undriven bit remains an error.

The C++ frontend's lifecycle differs from the SV frontend's clock-pin API:
`eval(false)` evaluates outputs; `eval(true)` performs one `_work(work_reset)` /
`_strobe()` transaction with pre-commit outputs; `step()` additionally settles
post-commit outputs. `work_reset` is an explicit one-bit input. Original C++
conversion removes some SV clock information; this frontend does not invent
clock edges or asynchronous events. The bus runner selects this contract
with `-DUSE_CPP_GRAPH`.

This frontend remains an explicit subset, not a general C++ optimizer. It
rejects nonempty module constructors, explicit register initialization,
negative-phase lifecycle methods, dynamic structural/commit conditions,
unsupported calls and control flow, incomplete writes and live cycles.
Keep legacy optimization available while expanding coverage. The
`cpp_graph_pipeline` test checks ordinary conversion with RTL removed,
C++-mutation authority, hierarchy/captured bindings, partial state and
unsupported cases; the direct SV graph tests remain an independent reference.

## Preserved graph-construction pipeline

```sh
hdlcpp --native-graph --top Top --output /external/work/design.cc \
  -Irtl/include rtl/design.sv
cpphdl --native-graph --cxx clang++ --output /external/work/build \
  --runner /path/to/runner.cc /external/work/design.cc
```

The first output is an authoritative C++ graph-construction program. Its
embedded, text-readable graph records contain resolved bit widths, operations,
connections, ports and state transitions. Keeping these records as data avoids
creating an enormous C++ AST of template and initializer-list expressions.
The second stage compiles that C++ program with `cpphdl_graph.h`, executes the
CppHDL graph optimizer/code generator, and optionally builds the runner with
the resulting `model.h`. Original SV, frontend intermediates and netlist
sidecars are not consulted. The regression deletes its RTL before this stage
and verifies that a deliberate C++ graph mutation changes simulation outputs.

## Why this representation

* Slang performs parameter, generate, type and interface elaboration once.
  Field accesses refer to resolved symbols and bit offsets, not inferred C++
  helper names or runtime pack/unpack operations.
* Blocking assignments and ordered partial writes become SSA values.
  Branch results are merged explicitly. A complete if/else does not retain a
  fictitious dependency on the old output, as guarded-write chains can do.
* Hierarchy ports, slices and concatenations are bit-reference wiring, not
  runtime aggregate conversions. NBA destinations are separate from current
  values; disjoint processes can own disjoint fields of one packed array.
* The CppHDL compiler resolves wiring, folds constants, shares equivalent
  combinational producers, and schedules only live outputs and next-state
  values. Word-level false cycles are split at bitwise producers; real live
  combinational cycles and incomplete writes fail instead of being guessed.
* The result is one static schedule of native unsigned word operations.
  There are no per-field callbacks, memoization timestamps, dynamic scheduler,
  installed-runtime patches or handwritten hardware replacements.
* State advance and output-only settling are emitted as separate compile-time
  phases, allowing the C++ compiler to eliminate next-state calculations from
  the latter. These are two fixed compiled functions, not runtime template
  resolution. The public `step()` still performs both phases.

## SV-fronted simulation contract

The generated `cpphdl_native::Model` exposes each top port as a
`std::array<uint32_t, N>` in least-significant-word-first order. The final word
is normalized to the declared width. `model.step()` evaluates the input event,
commits all nonblocking state simultaneously, and reevaluates combinational
outputs. Call it after changing inputs or a clock level.

`eval(false)` is the lower-level combinational operation; `eval(true)` also
commits state, but its outputs describe the **pre-commit** values. Do not
substitute it for `step()` when post-edge/reset outputs are observable. The
settled small-bus benchmark uses `step()` on both clock phases.

This is explicitly a **two-state synthesizable subset**, not a complete
SystemVerilog simulator. Unknown literal bits and out-of-range reads become
zero, registers start at zero, and negative-edge history starts high so an
initial active-low reset is observed. Resolved SV widths and signedness govern
operations; top port names must be C++-compatible identifiers. Arithmetic is
currently limited to at most 64 bits, while wiring,
packed aggregates, muxes and bitwise operations support wider values.

Unsupported cases fail closed: live latches or combinational cycles;
state-derived clocks/resets requiring extra event deltas; timed assignments;
inout/tri-state ports; hardware declaration/output-port initializers; dynamic
loops/range selections; nonconstant system tasks; wildcard cases; clocked
blocking assignments; signed dynamic division; and functions with side effects,
static lifetime, or conditional early returns. Some valid RTL therefore still
needs additional compiler support. Do not use this mode as an implicit fallback
for unsupported legacy models. Synthesis `translate_off` regions must be
excluded explicitly, e.g. `--translate-off-format pragma,translate_off,translate_on`.

## Small CVA6 replay

The existing replay harness supports `-DUSE_NATIVE_GRAPH`. A source-controlled
regeneration/comparison entry point is:

```sh
python3 -B hdlcpp/tests/cva6/check_native_graph.py \
  --cva6-source /path/to/cva6 --hdlcpp /path/to/hdlcpp \
  --cpphdl /path/to/cpphdl --cxx clang++ \
  --trace /external/matmul-bus.bin --output /external/new-comparison \
  --reference /external/original-verilator/VXbarBench \
  --cxxrtl-reference /external/cxxrtl/run --trials 5
```

This regenerates only the small original PULP bus hierarchy, copies the original
testharness address map, records source/build commands and hashes, and times
alternating CPU-pinned runs. Each executable must first validate every output
bit against the same trace. The optional references must be the matching
original-SV replay executables. Conversion, compilation, trace loading and
full-output validation are outside the work timer. Input application, model
evaluation, output reads and checksum accumulation are inside it.

It intentionally does not import legacy callback demand-context restrictions:
there are no callbacks to classify. It simulates the same hardware ports and
state using a different scheduling representation. No small-block result is
evidence of full-CVA6 correctness or performance; full-core regeneration is a
separate validation step.

## Tests

`native_graph_pipeline` is enabled with `CPPHDL_BUILD_HDLCPP=ON` and Python;
it has no Yosys/CXXRTL dependency. It checks 20,000 randomized clock/reset,
signed-shift, aggregate, partial-NBA and control-flow samples against an
independent oracle under ASan/UBSan, plus negative cases and C++ authority.
All build, timing and profiling artifacts should stay outside the checkout.
