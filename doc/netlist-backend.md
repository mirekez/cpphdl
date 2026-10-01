# Experimental netlist simulation backend

For the working opt-in SV -> word C++ -> CppHDL pipeline, see
[word-model-backend.md](word-model-backend.md). It uses a new Yosys-assisted
hdlcpp output representation. The legacy C++ -> SV round-trip described here
is separate and its representative-bus lowering limitations remain.

`tools/cpphdl-netlist.py` prototypes a replacement execution representation, not
another optimization pass over the packed-proxy runtime:

```
authoritative CppHDL C++
  -> existing cpphdl SystemVerilog conversion
  -> sv2v
  -> Yosys RTLIL: processes, flattened connectivity, word-level optimization
  -> driver-separated nets
  -> CXXRTL native C++
```

The execution graph contains bit-vector operations and explicit register state,
not C++ port callbacks, packed-field proxy objects, or cached comb methods.
Connectivity determines sharing and scheduling. Splitting internal nets at
driver boundaries prevents unrelated fields in a packed object from acquiring
false scheduling dependencies. Arithmetic and mux cells remain word-level;
there is no blanket gate mapping or LUT expansion.

This backend is **experimental and opt-in**. It does not replace the default
optimizer, and the representative converted CVA6 bus cannot yet complete this
round-trip. Do not use the original-RTL feasibility numbers below as a working
CppHDL conversion result. Removing the old execution path before establishing
equivalence would hide that missing functionality rather than fix it.

## Use

Required external tools: a built `cpphdl`, `sv2v`, Yosys with `write_cxxrtl`, and
CXXRTL runtime headers matching Yosys. The experiment used sv2v 0.0.13, Yosys
0.52-2, and Clang 21.1. No downloads or installations occur in the driver.

Choose a new or empty output directory **outside the source checkout**:

```sh
python3 tools/cpphdl-netlist.py \
  --cpphdl /path/to/cpphdl --sv2v /path/to/sv2v --yosys /path/to/yosys \
  --top WordGraph --output /tmp/cpphdl-netlist-word \
  tests/netlist/WordGraph.h -- -I"$PWD/include" -w

clang++ -std=c++23 -O2 -Iinclude -I/tmp/cpphdl-netlist-word \
  -I/path/to/yosys/include/backends/cxxrtl/runtime \
  tests/netlist/Run.cc /tmp/cpphdl-netlist-word/model.cc \
  -o /tmp/cpphdl-netlist-word/run
/tmp/cpphdl-netlist-word/run
```

The manifest records commands, source-file hashes, generated-artifact hashes,
and completion/failure status. These are audit aids, not a complete cache key:
transitively included C++ headers are not hashed. Missing dependencies,
converter assertions (including those with exit status zero), unsupported
syntax, and failed Yosys checks stop generation. There is no fallback to
original RTL or a handwritten block.

The generated model uses CXXRTL's `step()`/commit contract. A register commit
can leave a combinational output alias needing another evaluation with the
same clock level; see `tests/netlist/Run.cc`. Do not introduce another rising
edge to refresh outputs. This is synthesized, two-state cycle simulation, not
a replacement for arbitrary C++ side effects, four-state event simulation,
delays, assertions, or unsupported HDL constructs.

## Tests

Optional CTest entries are `netlist_driver`, `netlist_converter`, and
`netlist_word_graph`. Configure `CPPHDL_NETLIST_SV2V`,
`CPPHDL_NETLIST_YOSYS`, and `CPPHDL_CXXRTL_RUNTIME` when tools are not installed
in standard locations. The converter test additionally needs Verilator for
syntax checking only; Verilator is not the execution backend.

- Six driver tests cover package ordering, dependency cycles, duplicate
  packages, empty output, stale output rejection, and logged converter failure.
- Converter regressions cover long specialization filenames and omitted loop
  initializer/increment clauses.
- The C++-origin word-graph test checks 20,000 cycles against both original C++
  and an independent scalar oracle: all 16 lanes of a 1,024-bit input, upper
  word bits, selectors, stalls, resets, and pre/post-edge register outputs.

## Representative bus experiment, 2026-09-17

Artifacts and logs are outside the checkout:
`/home/me/cpphdl-validation-20260917/netlist/`.
No full CVA6 regeneration was run.

The C++ round-trip exposed and fixed three converter defects: a null module
context during member/type rendering, shifted expression slots when a `for`
clause is absent, and overlong generated filenames. Conversion now advances
to a further blocker: resolving the `fifo_v3` specialization whose logic width
contains a cast/constant expression. Its manifest is failed, not runnable.

There are additional front-end restrictions: the tested omitted-initializer
loop is valid emitted SV but sv2v/Yosys cannot synthesize that form here; the
existing converter also mis-emits a declaration inside this comb's loop
initializer. The passing fixture declares the index separately and assigns
it in the loop initializer. It does not establish general loop support.

Separately, the **original SystemVerilog** small bus was lowered through
sv2v/Yosys/CXXRTL to test the architecture before investing in more conversion
work. A generic constant-indexed-literal folding adapter was required between
sv2v and Yosys. This adapter is recorded in the external artifacts and is not
part of the C++ driver. No hardware behavior was replaced manually.

Five alternating-order trials, CPU 2, identical captured trace and replay
driver timing region (input application, evaluation, output hashing, and state
advance; file reading and correctness checking excluded):

| Execution path | Median seconds |
| --- | ---: |
| Original-SV Verilator reference | 0.162909 |
| Existing production CppHDL replay | 7.066880 |
| Original-SV CXXRTL, unsplit internal nets | 3.001579 |
| Original-SV CXXRTL, driver-separated nets | 1.318287 |

Every trial checks all 4,052 output bits on all 46,264 cycles and reports
checksum `0261761b2a1c459e`. Driver splitting reduces CXXRTL time by 56.1%.
That feasibility path is 5.36x faster than the existing CppHDL replay but
still 8.09x slower than Verilator. This is not a completed CppHDL speedup.

Blindly splitting every internal bit was rejected: generated C++ grew from
1.91 MB to 8.06 MB and Clang exhausted its compilation stack. Driver splitting
instead produced 1.73 MB C++ and compiled in about 82 seconds with 455 MiB
peak RSS. It still reports twenty feedback aliases; these require a more
precise representation than simply dividing every packed bus into bits.

A separate diagnostic split only the twenty feedback wires reported by
CXXRTL. Five further alternating-order trials gave medians of 1.129795 s
for that variant, 1.313539 s for driver-only splitting, and 0.161673 s for
Verilator. Every replay passed the same complete output/cycle checks. This
is another 14.0% reduction, but still 6.99x slower than Verilator. The
diagnostic selector depends on CXXRTL's log format and remains an external
experiment, not a production parsing/scheduling mechanism. The checked-in
driver uses the conservative driver-boundary pass.

Another rejected experiment removed twenty redundant, unannotated internal
aliases from Yosys JSON and re-imported it. Re-importing changed the internal
wire representation, brought back feedback, and grew C++ to 2.94 MB; compilation
was stopped after exceeding 2 GiB RSS. There is no equivalent-output timing
result for that experiment and none of its rewriting is in the driver.

The production acceptance gate remains: complete conversion from authoritative
CppHDL C++, check every replay output and cycle, then repeat the timing test.
Do not delete the current backend or regenerate full CVA6 before that gate.
