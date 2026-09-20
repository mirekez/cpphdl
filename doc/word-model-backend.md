# Opt-in word-model conversion and execution

The new path is:

```
SystemVerilog
  -> hdlcpp --word-model (sv2v/Yosys-assisted elaboration and CXXRTL emission)
  -> authoritative C++ model.cc + model.h
  -> cpphdl --word-model (typed Clang AST lowering and host compilation)
  -> native model.o or a linked runner
```

This is a **different hdlcpp output representation**, not a repair of the
legacy callback C++ -> SystemVerilog round-trip. The first stage explicitly
uses sv2v/Yosys; this is not the existing Slang/textual C++ emitter. Neither
stage uses Verilator. The default hdlcpp emitter and CppHDL L1 scheduler remain
unchanged. No hardware block is handwritten or selected by design name.

The C++ model is the second stage's actual input. It does not read the original
RTL, Yosys netlist, frontend manifest, or a prebuilt simulation binary. The
regression deletes the frontend RTL/intermediates before building, then checks
that changing the generated C++ changes simulation behavior.

## Why this representation

The old execution path repeatedly requests aggregates through callbacks and
materializes packed values. Increasing comb fusion leaves most of that work.
The word frontend instead derives connectivity and register state from the
elaborated hardware. Process lowering preserves ordered partial writes before
scheduling. Driver-separated wires avoid dependencies between unrelated fields.
Hidden constant/replicated-single-bit aliases are split structurally so they
cannot introduce artificial feedback; no CVA6 names or diagnostic-log matching
are used. Real feedback is not deleted or assumed away.

CppHDL recognizes typed `cxxrtl::concat_expr::val()` calls through Clang's AST.
Their leaves become direct word placements into one destination, rather than
shifted/zero-extended full-width intermediates for every concatenation prefix.
Only recognized CXXRTL value, slice, and concatenation types are eligible.
Macro expansions, headers, unrelated `val()` methods and lvalue concatenations
are not rewritten. Host C++ compilation resolves the constant word placements;
there is no runtime graph interpreter or patch to the installed CXXRTL runtime.

## Use

Build `hdlcpp` and `cpphdl` normally. Dependencies are Python 3, sv2v, Yosys
with CXXRTL, a C++23 compiler, and the matching CXXRTL runtime headers.
The tested versions are sv2v 0.0.13, Yosys 0.52-2 and Clang 21.1.3.
No tool installs or downloads are performed by these commands.

Use output directories outside the checkout, and pass all required SV sources
and include paths. Output directories must be new or empty:

```sh
hdlcpp --word-model --top MyTop --output /tmp/mytop-cpp \
  --sv2v /path/to/sv2v --yosys /path/to/yosys \
  -I/path/to/rtl/includes -DSYNTHESIS /path/to/top.sv

cpphdl --word-model --output /tmp/mytop-native \
  --runtime /path/to/yosys/include/backends/cxxrtl/runtime \
  --cxx clang++ --runner /path/to/Run.cc /tmp/mytop-cpp/model.cc
```

`--runner` is optional. Without it, the result is `model.o`; with it, the result
is `run`. Compiler defines and include paths may follow `--`. Relative paths
in those extra flags are interpreted from the output directory; prefer absolute
paths. The backend force-includes the model's authoritative header when building
the runner, avoiding a stale sibling `model.h` silently changing its class
layout. By default this is the input source with its suffix changed to `.h`;
use `--header` for a differently named interface.

For lowering without building:

```sh
cpphdl --lower-word-model /tmp/mytop-cpp/model.cc /tmp/model-lowered.cc -- \
  -std=c++23 -I/tmp/mytop-cpp -I/path/to/cxxrtl/runtime
```

Compile the result with the original model header, CXXRTL runtime, and this
repository's `include` directory. Direct lowering refuses to overwrite its
output or input file. Parse and compile failures are not converted to success.

The generated model exposes CXXRTL ports and its `step()`/commit contract, not
`cpphdl::Module::_work()`/`_strobe()`. Drive clocks explicitly and do not advance
the clock to refresh combinational aliases after a register commit. The fixture
`tests/netlist/WordPipelineRun.cc` shows pre/post-edge checking.

## Supported scope and limitations

- Synthesizable, two-state cycle models supported by sv2v/Yosys/CXXRTL. This is
  not four-state/event-delay simulation or arbitrary C++ side-effect execution.
- Values must have normalized unused high bits, as required by the typed
  word representation. When driving `.data` directly, mask padding bits.
- Preprocessor definitions are explicit and recorded. The small CVA6 bus test
  uses `-DSYNTHESIS -DVERILATOR`; `VERILATOR` here is an RTL feature guard to
  exclude unsupported SVA syntax, not use of the Verilator executable/backend.
- The constant-indexed-literal adapter handles sv2v's known hexadecimal literal
  slices and rejects out-of-range selections. It leaves comments/strings alone.
  Other unsupported syntax fails conversion rather than being guessed.
- Assertions, black-box implementations and behavioral constructs are not
  supplied by this backend. Synthesis guards may exclude assertions; do not use
  this mode as an assertion-verification replacement.
- The manifests record commands, direct source/model/runtime hashes and status;
  they are audit records, not complete transitive-include cache keys.
- Successful model generation/build is separate from design equivalence. Always
  run the appropriate output/cycle checks before reporting throughput.
- Small-bus results do not establish full-CVA6 performance. Full-CVA6 conversion
  has not been regenerated for this change.

## Tests

With `CPPHDL_BUILD_HDLCPP=ON`, the optional `word_pipeline` CTest is available
when `CPPHDL_NETLIST_SV2V`, `CPPHDL_NETLIST_YOSYS`, and `CPPHDL_CXXRTL_RUNTIME`
resolve the external tools. It covers:

- 12,000 randomized concatenation/slice/assignment cases under ASan/UBSan,
  including nested materialization, macros and unrelated methods;
- syntax/missing-include failure and overwrite protection;
- literal normalization and structural alias selection;
- fresh SV -> C++ -> optimized native simulation for 20,000 cycles against an
  independent bit oracle, including non-word-aligned lanes, signed shifts,
  ordered packed writes, reset, enable/stall and pre/post-edge register state;
- C++-only compilation after deleting frontend intermediates, plus a mutation
  test proving that generated C++ controls the simulation result.

The complete small-CVA6-bus replay additionally checks all 4,052 output bits on
46,264 captured matmul cycles. Its external build and timing records are under
`/home/me/cpphdl-validation-20260918/hdl-word/` for this development run.

Five alternating-order trials on CPU 2, execution only (no conversion/build):

| Model | Median wall time |
| --- | ---: |
| Existing CppHDL L1 baseline | 4.668621 s |
| New automatic word-model pipeline | 0.329564 s |
| Original-SV Verilator | 0.167884 s |

The new pipeline is 14.17x faster than the L1 baseline and takes 1.96x
Verilator's time on this trace. Every trial has the same output/cycle checks
and checksum `0261761b2a1c459e`. Work-only profiling counts 51,477 executed
instructions per cycle, two model evaluations and two commits. This is a
small-block result, not a prediction for the full CVA6 core.
