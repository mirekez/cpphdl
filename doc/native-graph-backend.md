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

The root may be an `extern` declaration of a module template specialization;
cpphdl completes its type and instantiates port bindings without constructing
the simulation hierarchy. Nested module arrays remain hierarchy rather than
packed data. Projected ports such as `request_in__field_addr` become
`request__field_addr`, preserving the original direction. Constant `bits(high,
low)` reads and partial next-state writes lower to bit views, not runtime proxy
objects. Direct writes to current register state remain rejected.
Packed `cpphdl::array<..., true>::bits(high, low)` uses the same lowering,
including nested arrays and packed-struct field offsets, without interpreting
the library's `.data` or element storage. Constant slices support wide values;
dynamic slices remain limited to 64-bit receivers. This works with either
the default layout or `CPPHDL_NATIVE_PACKED`.

Nonempty concatenations accept temporary `logic<0>` operands, including
zero-count `repeat()` results produced by hdlcpp for parameterized sign extension.
These operands contribute no bits; C++ operand evaluation and side effects are
preserved. This does not allow zero-width ports, registers, addressable storage,
standalone replications or all-empty concatenations. `cpp_graph_zero_concat`
checks C++/graph values, wide packing, evaluation order and invalid storage;
`cpp_graph_zero_repeat_verilator` compares hdlcpp-generated C++, the graph model
and original RTL with equal and unequal extension widths when both tools are built.

This frontend remains an explicit subset, not a general C++ optimizer. It
rejects nonempty module constructors, explicit register initialization,
negative-phase lifecycle methods, dynamic structural/commit conditions,
unsupported calls and control flow, incomplete writes and live cycles.
Keep legacy optimization available while expanding coverage. The
`cpp_graph_pipeline` test checks ordinary conversion with RTL removed,
C++-mutation authority, hierarchy/captured bindings, partial state and
unsupported cases; the direct SV graph tests remain an independent reference.
An external function without a C++ body is rejected with a named diagnostic
unless explicitly supported as a host effect. It is never replaced with a
constant or silently skipped. Full testharness DPI/host services still need
backend support before the small bus replay can become a full-system simulation.

### Switches and early exits

The ordinary C++ graph frontend supports value and void returns from switches,
conditional early returns, grouped labels, fall-through, no-default switches,
and nested switches. Break exits only the innermost switch or loop; continue
inside a switch targets its enclosing loop. Exit predicates guard subsequent
writes and host effects, not just the function's returned value. Callee exits
do not terminate the caller, and each switch selector is evaluated once.

Loops still require statically evaluable bounds and a local induction increment;
this is not support for arbitrary runtime-bounded C++ loops. Dynamic control in
structural assignment or register-commit phases remains unsupported.

The `switch_control_cpp`, `switch_control_graph`, and `switch_control_verilator`
regressions run the same source over exhaustive selectors, branch flags and
representative data values. The RTL frontend preserves switch fall-through by
emitting case suffixes and uses scoped exits for conditional switch breaks.
`switch_control_effects` additionally compares ordered libc random calls and
their counts against C++, including discarded calls and effects after exits.

### Deferred SRAM storage

Ordinary `cpphdl::memory<logic<WIDTH>, SIZE, DEPTH>` now lowers in cpphdl,
without changing hdlcpp's public memory API. The frontend describes a memory
once; the backend allocates heap-backed rows and emits indexed native word
loads/stores. Depth is not expanded into wires, muxes or registers. The exact
`memory<logic<64>, 1, 33554432>` shape uses 256 MiB of runtime data, not a
depth-sized compiler graph or stack object.

`operator[]` reads committed rows. `pending()` forwards earlier enabled queued
writes at the same address, and assignment to a memory-row proxy queues a
snapshot. Row slices only modify that snapshot until it is assigned back.
Writes retain source order and commit at `_strobe`'s `apply()`; later writes to
the same address win. Read addresses, write addresses and write data are
evaluated before commit. `eval(false)` never commits memory writes. Active
out-of-bounds accesses throw rather than truncating the address; inactive
branches do not access storage. Memory-dependent getters are re-lowered to
retain the current path guards and pending-write history; identical committed
loads can still share backend nodes.

The supported subset requires unpadded `logic` elements and statically shaped
module memories without explicit member initializers. Checkpoint overloads,
memory copies, compound row assignments
and nonstandard commit lifecycles remain unsupported. Writes without a strobe
`apply()` are rejected. Native storage starts at zero, the backend's two-state
initialization convention; this does not model unknown/uninitialized RTL data.

`CppGraphMemory.cc` checks 4,000 transactions each with 64- and 128-bit rows,
including byte merging, aliased ports, snapshots, repeated pending getters,
separate memories and output-only settling. `CppGraphMemoryLarge.cc` checks
17- and 33,554,432-row memories, endpoint accesses, 64-bit address bounds and
constant graph size. Fresh `CppGraphMemory.sv` conversion checks 4,000 dual-port
transactions against ordinary C++ and an independent oracle. Runtime checks
use ASan/UBSan. The actual CVA6 `tc_sram.sv`, freshly converted with a small
16-row/two-port specialization, also passed the same 4,000-transaction oracle.
Its full-depth single-port specialization also lowers: 471 source nodes,
39 scheduled nodes and an 11,651-byte native model header. This validates the
SRAM block, not the complete CVA6 system.

### Transactional random calls

The ordinary C++ frontend supports the external C function `long random(void)`
inside `_work`, including getters reached from that transaction. It emits an
ordered, guarded host-effect node that calls the actual runtime `::random()`.
It does not invent a PRNG, evaluate randomness during conversion, or merge
identical calls. A discarded return value still advances the host generator.
Nested `if`, conditional expressions, built-in short-circuit operators and
switch arms retain their execution predicates.

`eval(true)` executes those effects once when their guards hold. `eval(false)`
executes none; consequently `step()` does not consume a second random value
while settling outputs. The runner owns libc seeding and the process-global
random stream, just as it does for the ordinary C++ model. This establishes
C++-to-graph behavior, not equivalence to another simulator's `$random` stream.

Random calls in output-only/structural/commit lowering, host effects reaching
outputs through graph aliases without a register, and repeated uses of an
effectful memoized getter are explicitly rejected. Pure getters can still be
shared, but the frontend must not reuse the result of an effectful getter as
though it were a pure producer. `srandom`, seeded SV random calls and arbitrary
DPI functions are not covered by the random intrinsic. The differential regression
checks libc results and invocation counts under ASan/UBSan, including disabled
branches, unused results and repeated output settling.

### JTAG DPI calls and work storage

The C++ frontend also supports the external C ABI
`int jtag_tick(unsigned char*, unsigned char*, unsigned char*, unsigned char*, unsigned char)`
inside `_work`. The generated model declares and calls the linked function;
the runner must supply it. No replacement implementation or constant return
value is generated. The existing hdlcpp DPI adapter is lowered as ordinary C++.
The host call receives the four current output bytes and the TDO input, and
its signed 32-bit return value and four updated bytes are copied back in order.
Calls share the effect-order chain with `random()`, retain branch guards and
unused-result effects, and never execute during output-only settling.
Aliased or dynamically addressed output pointers, incorrect ABIs and calls
outside work transactions are rejected rather than approximated.

Generated SimJTAG retains some clocked blocking assignments in ordinary module
fields rather than `reg<>`. Writes to these fields in `_work` now create retained
transaction state. Within the transaction, direct reads see preceding writes;
inactive branches hold the old value, and `step()` exposes committed outputs.
Mixed work/combinational writers and explicit field initialization are rejected.
Pure cached getters record the storage versions they read, including dependencies
through nested getters. A changed work-storage version creates a fresh producer;
earlier uses keep their original value and unchanged producers remain shared.
Effectful getter reuse remains rejected. This is generic C++ work-storage
lowering, not a handwritten replacement for SimJTAG.

The JTAG regression compares ordinary C++ against native code using the maintained
CVA6 DPI adapter, a nontrivial linked test callback and the actual libc random
stream. It checks all output bytes, signed return values, multiple calls,
partial callback writes, state retention, call order, cached reads between writes
and settling under ASan/UBSan.

### SimDTM DPI call

The native frontend supports the existing external C `debug_tick` ABI:

```cpp
int debug_tick(unsigned char* req_valid, unsigned char req_ready,
               int* req_addr, int* req_op, int* req_data,
               unsigned char resp_valid, unsigned char* resp_ready,
               int resp, int resp_data);
```

This is a bounded DPI integration for CVA6, not a general FFI or a replacement
debugger. The linked runtime supplies the function; the compiler emits no stub.
The normal hdlcpp adapter is lowered unchanged. A single effect node owns the
call and its signed 32-bit result. Five word-sized projections expose updated
pointer arguments, so the graph keeps its native-word representation without
repeating the call. Pointer locals start with their current values to preserve
outputs the callback leaves untouched. Calls retain branch guards and ordering
with other host effects, even with unused return values; settling makes no calls.
Nonmatching ABIs, aliased or dynamic output addresses and calls outside `_work`
are rejected. The same retained-state and pure-getter versioning rules apply.

`CppGraphDebug.cc` compares raw calls and the maintained adapter against ordinary
C++ under ASan/UBSan, including signed inputs/results, all output bits, partial
updates, multiple calls, reset/disabled cycles, cached intermediate reads and the
actual libc random stream. Separate rejection tests cover unsupported boundaries.
Immediately invoked and locally stored helper lambdas reuse the captured
environment used for port bindings, rather than becoming hardware ports.
Clang-resolved generic specializations use ordinary argument binding: reference
arguments retain aliases, while value arguments and copy captures are snapshots.
Mutable runtime lambdas and indirect callable dispatch remain unsupported.

### Compile-time aggregates

The ordinary C++ frontend uses Clang's evaluated values for `constexpr` objects
and structural template parameters. It does not re-execute their generated
initializer lambdas as hardware. Packed records use `__hdlcpp_offset_*` and
`_size_bits()` metadata, independently of C++ padding or declaration order.
Nested arrays, implicit array fillers, logic storage and native integer wrappers
are serialized recursively; integers wider than 64 bits retain every bit.
Missing, overlapping, incomplete and out-of-bounds record layouts are rejected.

Parameterless helper calls with a `void` return type may perform procedural
array copies or assignments without returning a value. Port bindings and
value-returning closures still require a return value.

`CppGraphConstants.cc` checks all 290 bits of nested ordinary and template
constants, including a 128-bit integer, signed fields, wrappers and array
fillers, alongside clocked data, void helper array copies, immediate aggregate
initializers, generic setters and repeated calls with copy captures. An external CVA6
check also compares the unchanged generated `DebugHartInfo` and every bit of
the 17,217-bit `cva6_cfg_t` against ordinary C++.

Runtime `.bits(high, low)` selections on words up to 64 bits lower to shifts
and masks. Writes retain neighboring bits, including unaligned and full-word
ranges, rather than assuming array-element alignment. Wider and nested dynamic
ranges remain rejected. `CppGraphRanges.cc` checks every valid endpoint pair at
8, 32 and 64 bits against ordinary C++ under sanitizers. An external test also
checks the unchanged generated `dm_sba` byte-enable logic against ordinary C++
and an independent oracle.

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

The full `cv32a6_imac_sv32` ordinary-conversion trial on 2026-09-19 completed
regeneration but did not produce a native executable. Its initial
`SimJTAG::random_bits_comb_func` failure is fixed by transactional host effects.
The JTAG follow-up also lowers the real `jtag_tick` ABI, retained work fields and
versioned pure getters. Retrying the unchanged C++ bundle passes SimJTAG and its
DMI/JTAG consumers. The SimDTM follow-up adds `debug_tick` and fixes a null-type
crash on immediately invoked helper lambdas. Aggregate lowering now handles
`DebugHartInfo`, `cva6_cfg_t`, void copies and HPDcache's generic setters. Getter
recognition excludes nested lambda returns. Dynamic word ranges also handle the
debug byte-enable mask. That full trial then stopped inside Clang's integer
constant evaluator; no full native executable or runtime result was obtained.
The 2026-09-20 retry regenerated 287 headers without conversion failures. With
the same frontend and input, an 8 MiB stack crashed after 153 seconds; a 32 MiB
stack got past that crash and reported an unsupported `byteswap` after 201
seconds. These are frontend times, not simulation timings. The larger stack
was a diagnostic workaround. Expression lowering now uses Clang's existing
stack-growth mechanism when space runs low, including exception propagation
back from the stack worker. `CppGraphDepth.cc` reproduces the crash with nested
calls/casts: 240 calls now lower and match ordinary C++ with an 8 MiB stack;
300 calls still fail cleanly at the existing call-depth limit.

`cpphdl::byteswap` now lowers directly to a logical bit permutation instead of
interpreting `logic::get` and its implementation-specific byte storage. This
also handles packed structs through their logical layout. Partial-byte widths
preserve the C++ helper's zero-fill and truncation behavior, rather than assuming
an involution. `CppGraphByteswap.cc` checks 2,000 one-hot/random inputs per width
at 1, 7, 8, 9, 16, 24, 32, 64, 65 and 128 bits, plus slices and packed structs,
against ordinary C++ and a bit-level oracle under ASan/UBSan. Successful width
cases are removed immediately so sanitized executables do not accumulate and
exhaust the temporary filesystem's user quota.
The full retry with both fixes regenerated 287 headers with zero failures in
1,208 seconds. Native lowering got past byte reversal and stopped after 199
seconds at `i_sram.i_tc_sram_wrapper[0].i_tc_sram::_work`: the `.data` member of
`array<1, array<1, logic<64>, true>, true>` still requests user-struct field
metadata instead of being treated as the packed array's storage view. The
frontend had reached 212,185 nodes and 446 registers. This trial still used a
32 MiB stack; default-stack validation is currently the small depth regression.
The final ordinary-C++ graph regression suite passed. All temporary binaries,
generated headers and diagnostic files from this retry were removed.
The subsequent hdlcpp fix emits `member.bits(...)` and `member._next.bits(...)`
for nested packed-array writes, using the public slice API rather than `.data`.
Small generated-C++ tests validate combinational and nonblocking writes under
sanitizers. The next full retry regenerated 287 headers with zero failures in
1,019 seconds and confirmed no `.data.bits()` in the generated SRAM header.
On the default 8 MiB stack, graph lowering stopped after 190 seconds because
the public packed-array `bits()` method was still interpreted through its
private `.data` storage. Direct slice lowering now covers this API instead.
`CppGraphPackedRanges.cc` checks all valid 64-bit ranges three times (6,240
transactions per layout), against ordinary C++ and independent bit-level
expectations, including field offsets, overlapping copies, 128-bit constant
slices and the SRAM's exact nested-array `_next.bits()` shape. Both storage
layouts pass ASan/UBSan. `CppGraphPacked.sv` additionally checks 2,000
combinational/partial-next-state samples after fresh ordinary hdlcpp conversion
and removal of the SV source. The subsequent full retry regenerated all 287
headers with zero failures in 1,018 seconds. On the default 8 MiB stack,
lowering passed the packed-array slice and stopped after 189 seconds at
`tc_sram::rdata_d_comb_func`: indexing
`cpphdl::memory<cpphdl::logic<64>, 1, 33554432>` is unsupported. It reached
212,573 nodes, 4,988 cells, 3,816 methods and 446 registers; sampled peak
frontend RSS was 4,734,628 KiB. These are frontend measurements, not simulation
timings. The subsequent memory lowering described above passes the isolated
SRAM block, including its full-depth specialization. The 2026-09-23 full retry
rebuilt both tools and passed the 64-/128-bit memory tests and 33,554,432-row
runtime bounds tests before regenerating 287 headers with zero failures in
1,097 seconds. Full lowering no longer reported unsupported SRAM indexing:
it reached 324,503 nodes, 7,350 cells, 5,524 methods and 890 registers, then
failed with `std::bad_alloc` after 282 seconds. This run used the default
8 MiB stack and a 5,600,000 KiB virtual-address safety limit (5.34 GiB);
sampled peak frontend RSS was 5,035,664 KiB (4.80 GiB). The resource monitor
did not terminate the process. No allocation backtrace was captured, so the
failing allocation has not been identified; this is not evidence of another
unsupported memory API. Peak monitored temporary-file size was 221.73 MiB.
No full-system native executable was produced, and all temporary artifacts
were removed.
The memory-fix retry regenerated all 287 headers with zero failures in 988
seconds. Shared getter snapshots and width-specialized conversions passed the
previous OOM point, reaching 328,400 nodes, 7,450 cells, 5,626 getters and 915
registers. Peak sampled frontend RSS was 4,367,032 KiB, with 3,218,049,846
AST bytes. It stopped after 232 seconds on the existing unsupported early
return inside `ariane_pkg::extract_transfer_size`'s switch, not an allocation
failure. A controlled retry using the same generated headers and nonrecursive,
on-demand function-body instantiation reached exactly the same node/cell/getter/
register and dependency counts and diagnostic in 197 seconds, at 3,011,348 KiB
peak RSS and 2,001,701,686 AST bytes: another 31.0% RSS reduction. Both attempts
used the same 5,600,000 KiB virtual-address limit and default 8 MiB stack;
neither was killed by the monitor. The latest peak is 2.87 GiB versus the
previous 4.80 GiB OOM attempt, but these are different stopping points, not
a completed full-system memory or runtime comparison. Switch-return lowering
blocked these attempts. It is now covered by the standalone control-flow
regressions above. The subsequent full retry rebuilt every cpphdl translation
unit and hdlcpp from current sources, passed the ordinary graph suite and
memory-scaling checks, plus 8,192 switch-control comparisons in each of the
ordinary-C++ and native-graph flows and 2,048 ordered host-effect checks.
Fresh `cv32a6_imac_sv32` conversion generated 287 headers with zero failures in
1,090 seconds. Full lowering passed `extract_transfer_size`, reaching 1,282,182
nodes, 8,080 cells, 6,235 cached methods and 978 registers in 277 seconds.
Peak sampled frontend RSS was 3,257,312 KiB (3.11 GiB), with 2,026,867,510 AST
bytes. The unchanged 5,600,000 KiB address-space limit and default 8 MiB stack
were used; the monitor did not kill the process.

That retry's blocker was `unsupported C++ hardware type: struct cpphdl::logic<0>` in
`cva6_mmu::lsu_exception_o_comb_func`. The generated `tval` sign-extension
concatenation contains a repeated prefix of length `CVA6Cfg.XLEN-CVA6Cfg.VLEN`,
which is zero for this 32/32 configuration. A freshly converted standalone
module with parameters `XLEN=32`, `VLEN=32` and
`assign result_o = {{XLEN-VLEN{addr_i[VLEN-1]}}, addr_i};` reproduces the same
failure in a 16-node graph: hdlcpp emits `cat{logic<(XLEN-VLEN)*(1)>(...), ...}`
and cpphdl rejects the resulting empty operand. No production workaround was
applied during this retry. The full parse also warns about shifts by 64 after
a `uint64_t` cast in generated `axi_riscv_amos_alu.h`; those warnings are not
the stopping diagnostic and have not yet been validated for correctness.
No full native executable was produced. All build/generated artifacts were
kept in a real `/tmp` workspace and removed after recording these results.
There is no full-system native-graph/Verilator performance ratio yet.

### Zero-width fix retry (2026-09-24)

Fresh builds pass the zero-concat sanitizer and rejection checks, 8,192
SV/C++/native-graph/Verilator samples across four width configurations, the
switch-control checks, memory-scaling checks and the full ordinary graph suite.
Full regeneration produces 287 headers with zero failures in 1,147 seconds.
The MMU zero-width concatenation is no longer the blocker.

Full lowering now reaches 1,297,094 nodes, 8,137 cells, 6,288 cached methods and
981 registers before stopping after 317 seconds. Sampled peak frontend RSS is
3,261,480 KiB (3.11 GiB), with 2,035,256,118 AST bytes. The same 5,600,000 KiB
address-space limit and 8 MiB stack apply; no resource guard terminated it.
Peak monitored workspace size is 225.09 MiB.

That retry's next blocker was invalid hdlcpp output for the byte-permutation conditional
at `core/alu.sv:274`. Generated `core/alu.h:461` uses a
`cpphdl::sv_bits_runtime(...)` true branch returning `uint64_t` and a
`logic<8>(0)` false branch. Clang rejects the conditional because both types
convert to each other. The later graph diagnostic, `missing instantiated C++
body: ...::xperm8_result_comb_func`, is secondary to that compiler error.
The emitter must give both branches a consistent SV-sized type; changing the
assignment target alone does not resolve the C++ conditional's type.

A standalone conversion reproduces the same error:

```systemverilog
module Xperm #(parameter int XLEN = 32) (
    input logic [XLEN-1:0] operand_a,
    input logic [XLEN-1:0] operand_b,
    output logic [XLEN-1:0] result_o
);
    for (genvar index = 0; index < XLEN / 8; index++) begin
        assign result_o[index << 3 +: 8] =
            (operand_b[index << 3 +: 8] < XLEN / 8)
            ? operand_a[operand_b[index << 3 +: 8] << 3 +: 8]
            : 8'b0;
    end
endmodule
```

After `hdlcpp Xperm.sv`, a seed including `generated/Xperm.h` and declaring
`extern Xperm<> cpphdl_top;` fails with
`cpphdl --lower-cpp-graph seed.cc graph.cc cpphdl_top -- -std=c++23 -I"$HOME/cpphdl/include" -I.`.
Ordinary Clang `-fsyntax-only` also rejects a source that includes the generated
header and constructs a local `Xperm<> model;`, without any graph lowering.
No production workaround was applied. No full-system executable or runtime
comparison was obtained; temporary build/generated files were kept in real
`/tmp` and removed after recording the results.

### Xperm conditional fix retry (2026-09-24)

Fresh hdlcpp and cpphdl builds confirm that the ambiguous conditional is fixed.
The expanded Xperm fixture passes 4,096 samples per XLEN (32 and 64) in both
ASan/UBSan builds (-O0 and -O2): 16,384 checks total. An additional 8,192
C++/Verilator samples pass, covering normal, reversed, nested, widened and
explicitly signed conditional branches.

Native-graph lowering of the original small Xperm module still fails for both
XLEN values with `expected a static C++ value`. The 32-bit attempt reaches
26 nodes and reports `cpphdl_top::result_o_comb_func`; there is no longer a
Clang conditional-type error. Full CVA6 regeneration was deliberately not
started because this small test is still blocked.

The failure is in cpphdl's graph frontend, not the fixed conditional emission:
`CppGraph.h` handles `cpphdl::sv_bits_runtime` by calling `integer()` on both
slice endpoints. The inner byte-selection endpoints depend on `operand_b`,
so they cannot be evaluated as elaboration-time constants. The graph already
handles dynamic `.bits()` ranges separately, but this helper path does not
use that lowering.

A reduced test without any conditional isolates the missing helper support:

```cpp
#include "cpphdl.h"
using namespace cpphdl;
class RuntimeSelect : public Module {
public:
    _PORT(logic<32>) data_in;
    _PORT(logic<2>) index_in;
    _PORT(logic<8>) result_out = _ASSIGN(selected());
    logic<8> selected() {
        uint64_t first = uint64_t(index_in()) * 8;
        return logic<8>(cpphdl::sv_bits_runtime(data_in(), first + 7, first));
    }
};
extern RuntimeSelect cpphdl_top;
```

Run `cpphdl --lower-cpp-graph RuntimeSelect.cc graph.cc cpphdl_top -- -std=c++23 -I"$HOME/cpphdl/include"`.
This fails after 12 nodes with the same diagnostic. Two controlled alternatives
both lower and pass 4,096 native-model/oracle samples: retaining the helper but
making `first` constant (8), and retaining the dynamic index but replacing the
helper expression with `logic<8>(data_in().bits(first + 7, first))`. These are
diagnostic test variants, not production substitutions. No production code
was changed. All temporary files were kept in real `/tmp` and cleaned up.
There is no new full-system timing result.

### Direct bit-slice emission (2026-09-24)

hdlcpp now emits public `.bits(high, low)` reads instead of
`sv_bits_runtime`. Fixed-width results are materialized as `logic<width>`;
a changing position no longer makes an indexed part-select's width dynamic.
Source values without the public packed interface are first materialized as
`logic` at their source width. Existing narrow shift/mask optimizations remain.
Generated-loop slices whose widths really vary retain their numeric result
and width-aware reductions and concatenations.

The obsolete `sv_bits_runtime` library function, graph special case and
emitter-specific call repair have been removed. Regenerate older hdlcpp output
that uses that function; there is no compatibility shim. The separate
fixed-width `sv_bits<WIDTH>` API is unchanged.

The Xperm regression now includes the ordinary-C++ to native-graph flow for
both 32-bit and 64-bit inputs, including reversed/nested conditionals, widened
results and signed branches. Expression tests additionally check 65-bit
upward/downward selections from a 128-bit source at every offset from 0 to 63,
generated prefix/suffix reductions with widths 1 through 64, and native-integer
slices against independent oracles and Verilator. This change does not extend
the graph backend's existing support for wide dynamic slices beyond 64 bits;
the 65-bit cases validate generated C++ execution and Verilator equivalence.

Validation passes the full ordinary graph suite, the four targeted emitter
tests, packed-struct/array expressions, native packed layout, blocking array
updates and constant-width references. Xperm passes 16,384 ASan/UBSan ordinary
C++ samples (-O0 and -O2), 8,192 native-graph samples and 8,192 C++/Verilator
samples. The extended expression fixture passes both optimization levels,
ASan/UBSan and both Verilator modes. The full `test_modules` executable still
stops at the unrelated `testTypeTemplateCastShiftKeepsTargetWidth` text
assertion; the pre-change installed converter produces a byte-identical header
for that case. No full CVA6 regeneration or performance comparison was run.
Build and test artifacts were kept in real `/tmp` and removed after validation.

### Full CVA6 retry after direct bit-slice emission (2026-09-24)

Rebuilt both tools and regenerated all 284 conversion sources, producing 287
headers without conversion failures. The first graph attempt exposed an hdlcpp
regression: SV metadata described constant parameters and constant-array
elements as `logic`, although their generated C++ storage was native integer.
The emitter now materializes these sources as `logic<source_width>` before
calling `.bits()`. The Xperm fixture includes a parameter, localparam and
constant-array slice; it failed with the previous converter and passes with
the fix in ordinary C++ (16,384 sanitizer samples), Verilator (8,192 samples)
and native graph (8,192 samples). Extended expression tests also pass.

Every conversion source was then regenerated again, reusing the unchanged
interface metadata: 287 headers, no failures, 309.226 seconds. Full C++ graph
lowering progressed for 315.834 seconds, reaching 1,325,690 nodes, but failed
with `unsupported C++ hardware type: struct cpphdl::logic_bits<32>` in the
CVA6 frontend boot-address adapter. Sampled peak process RSS was 3,281,352 KiB
(3.13 GiB); this was a frontend error, not a memory/quota guard termination.
These are conversion/lowering measurements, not simulation timings. There is
still no new executable, cycle/output comparison or Verilator speed ratio.

The underlying blocker is cpphdl's bound-port call handling, not unsupported
SV syntax. `ariane_soc::ROMBase` is an unsigned integer bound to a
`function_ref<logic<32>>`. `CppGraph.h` returns the binding closure's `Item`
directly, losing the port's declared result type. Consequently the subsequent
`.bits()` sees an integer receiver, misses the graph primitive and interprets
the library body, eventually failing on `logic_bits<32>`. Casting all proxy
types to their template width would not fix the missing port conversion.

Small reproducer (save as `/tmp/PortBits.cc`):

```cpp
#include "cpphdl.h"

class Child : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<32>) address_in;
    _PORT(cpphdl::logic<32>) result_out = _ASSIGN(selected());
    cpphdl::logic<32> selected() {
        return cpphdl::logic<32>(address_in().bits(31, 0));
    }
};

class PortBits : public cpphdl::Module {
public:
    Child child;
    _PORT(cpphdl::logic<32>) result_out = _ASSIGN(child.result_out());
    void _assign() {
        child.address_in = _ASSIGN(uint64_t(0x10000));
    }
};
extern PortBits cpphdl_top;
```

Run `cpphdl --lower-cpp-graph /tmp/PortBits.cc /tmp/PortBits.graph.cc cpphdl_top -- -std=c++23 -I"$HOME/cpphdl/include"`
with a fresh output path. It fails in 1.240 seconds with the same diagnostic.
Changing only the binding value to `cpphdl::logic<32>(0x10000)` makes graph
lowering pass in 1.235 seconds. Both variants compile and execute as ordinary
C++, returning the expected `0x10000` after `_assign()`. The typed binding is
only a diagnostic control, not a generated-model workaround.

The full graph regression suite and memory-scaling regressions passed before
full regeneration. All large artifacts were confined to an owned directory
in real `/tmp`, guarded for quota, disk and available RAM, and removed after
recording these results.

### Full CVA6 retry after bound-port conversion fix (2026-09-24)

Rebuilt both tools from current sources. The new port-binding regression
passes 4,096 samples each in ordinary C++, both integer/typed native-graph
variants and Verilator. Xperm, expression-helper, memory-scaling and the full
ordinary graph regression suite also pass. The former boot-address port
conversion error no longer stops full lowering.

Full regeneration produced 287 headers from all 284 conversion sources with
zero failures in 1,148.076 seconds. Graph lowering reached 1,431,835 nodes,
8,640 cells, 6,776 cached methods and 1,047 registers before failing after
354.597 seconds. Sampled peak process RSS was 3,304,312 KiB (3.15 GiB), with
2,077,991,431 bytes of reported AST allocation. No resource guard terminated
the run. These are frontend timings, not simulation timings; no full-system
executable or new Verilator runtime comparison was obtained.

The next blocker is `wide or nested dynamic C++ bit range unsupported` at
generated `core/cache_subsystem/cva6_icache.h:280`, in `cl_sel_comb_func`.
The original `core/cache_subsystem/cva6_icache.sv:428` selects a fetch word:

```systemverilog
assign cl_sel[i] = cl_rdata[i][{cl_offset_q, 3'b0}+:CVA6Cfg.FETCH_WIDTH];
```

For this configuration, the source cache line is 128 bits and the fetch word
is 32 bits. hdlcpp emits a valid `logic<32>(line.bits(first + 31, first))` read.
The position varies at runtime; the selected width does not. cpphdl rejects
it because its `.bits()` handler tests the receiver width against 64, not the
result width. This case is not nested. Removing the guard alone is insufficient:
the current implementation uses full-source-width shifts, and graph arithmetic
also rejects operands/results wider than 64 bits. A word-wise or mux-based
wide-source read lowering is needed; truncating the source before selecting
would lose upper-half and cross-word data.

Small SV reproducer, `WideSelect.sv`:

```systemverilog
module WideSelect #(
    parameter int LineWidth = 128,
    parameter bit Fixed = 0
) (
    input logic [LineWidth-1:0] line_i,
    input logic [3:0] offset_i,
    output logic [31:0] data_o
);
    assign data_o = line_i[(Fixed ? 32 : {offset_i, 3'b0}) +: 32];
endmodule
```

Use this `slice_seed.cc` in the same temporary working directory:

```cpp
#include "generated/WideSelect.h"
extern WideSelect<LINE_WIDTH, FIXED_SELECT> cpphdl_top;
```

Run `hdlcpp WideSelect.sv`, then
`cpphdl --lower-cpp-graph slice_seed.cc dynamic.graph.cc cpphdl_top -- -std=c++23 -DLINE_WIDTH=128 -DFIXED_SELECT=0 -I"$HOME/cpphdl/include"`
with a fresh output path. In the tested `--native-graph` flow this fails after
26 graph nodes in 1.457 seconds with the same diagnostic.

The 128-bit dynamic fixture passes 3,328 valid byte-offset samples against an
independent byte-based oracle in ordinary generated C++ with ASan/UBSan and
against Verilator. The test includes upper-half reads and slices crossing the
64-bit word boundary. Two controlled variants lower and execute correctly in
native graph and ordinary C++, both with ASan/UBSan: `FIXED_SELECT=1` on the
same 128-bit source (3,328 samples), and `LINE_WIDTH=64` with dynamic position
(1,280 samples). These are diagnostic controls, not full-model substitutions.
No production workaround was applied. Owned build/generated artifacts stayed
in real `/tmp` and were removed after recording these results.

### Full CVA6 retry after wide-source slice fix (2026-09-24)

Fresh tool builds pass the wide-slice regression: 38,912 oracle samples each
in ordinary C++, native graph and Verilator, covering source widths through
257 bits, selected widths through 96 bits, mutable/const sources, cross-word
reads and zero padding. Unsupported wide writes still fail closed. The exact
`WideSelect.sv` reproducer above now lowers to 121 graph nodes and passes
3,328 generated-C++/native-graph/oracle samples with ASan/UBSan. Port-binding,
Xperm, expression-helper, memory-scaling and full graph regressions also pass.

Full regeneration produced all 287 headers from 284 conversion sources with
zero failures in 1,243.390 seconds. Lowering waited for RAM while an unrelated
workload was active; no unrelated processes were stopped. Once sufficient RAM
became available, it ran for 408.633 seconds and passed the previous I-cache
wide-read failure. It then failed after 1,433,750 graph nodes with
`unsupported C++ hardware type: struct cpphdl::logic<0>` in
`i_icache_hpdcache_data_upsize::rdata_o_comb_func`. Sampled peak process RSS was
3,135,852 KiB (2.99 GiB); this was a frontend error, not a resource-guard stop.
Contention means these frontend times/RSS values are not controlled performance
comparisons. No full-system executable or simulation-speed result was obtained.

The new blocker is an hdlcpp packed-range conversion bug. The arbiter uses
`hpdcache_data_upsize<64,128,1>`. In the original module:

```systemverilog
localparam int PTR_WIDTH = $clog2(DEPTH);
typedef logic [PTR_WIDTH-1:0] bufptr_t;
```

At `DEPTH=1`, the declared range is `[-1:0]`, which contains two bits, not
zero. hdlcpp instead emits `using bufptr_t = logic<PTR_WIDTH>;`, creating
zero-width read/write pointer registers. This is distinct from the previously
fixed zero-count concatenation case: these are declared storage objects, not
empty concat operands. cpphdl's rejection prevents execution of an already
incorrectly converted model. Allowing zero-width storage or clamping the width
to one would not preserve the SV declaration's behavior. Range-size lowering
must preserve endpoint signedness and inclusive ascending/descending bounds;
`$bits` must agree with the resulting type.

Verified reproducer, `PointerRange.sv`:

```systemverilog
module PointerRange #(parameter int DEPTH = 1) (
    input logic [1:0] data_i,
    output logic [1:0] data_o,
    output logic [31:0] width_o
);
    localparam int PTR_WIDTH = $clog2(DEPTH);
    typedef logic [PTR_WIDTH-1:0] bufptr_t;
    bufptr_t pointer;
    assign pointer = data_i;
    assign data_o = pointer;
    assign width_o = $bits(bufptr_t);
endmodule
```

Run `hdlcpp PointerRange.sv` in a temporary working directory. It emits
`using bufptr_t = logic<PTR_WIDTH>;` and `width_o_comb = PTR_WIDTH;`.
For graph reproduction, save this `pointer_seed.cc` alongside it:

```cpp
#include "generated/PointerRange.h"
extern PointerRange<TEST_DEPTH> cpphdl_top;
```

Then run
`cpphdl --lower-cpp-graph pointer_seed.cc pointer.graph.cc cpphdl_top -- -std=c++23 -DTEST_DEPTH=1 -I"$HOME/cpphdl/include"`
with a new output path. It reproduces the `logic<0>` error after 16 nodes in
1.643 seconds. Controls with `TEST_DEPTH=2` and `TEST_DEPTH=3` lower successfully.

Independent ordinary-C++ (ASan/UBSan) and original-SV/Verilator executions give:

| DEPTH | C++ width / outputs for inputs 0,1,2,3 | SV width / outputs |
| --- | --- | --- |
| 1 | 0 / 0,0,0,0 | 2 / 0,1,2,3 |
| 2 | 1 / 0,1,0,1 | 1 / 0,1,0,1 |
| 3 | 2 / 0,1,2,3 | 2 / 0,1,2,3 |

No production workaround was applied during this retry. All owned temporary
build/generated files were kept in real `/tmp` and removed after reporting.

## Tests

### Frontend memory scaling

Function bodies are instantiated on demand by the graph interpreter, without
recursively draining Clang's pending instantiation queue. Graph primitives
bypass the ordinary runtime's type-erased callable machinery; eagerly building
its implementations needlessly retained over a GiB of ASTs in the CVA6 retry.
The 256-instance templated-port regression gives identical graph files, with
peak RSS 210,656 -> 151,512 KiB and AST bytes 119,537,664 -> 67,108,864 when
changing only this instantiation policy. Explicit body requests and necessary
constant-expression/template argument instantiation remain enabled.

The graph frontend stores cached-getter dependencies as shared producer snapshots
and direct field reads, not a transitive field map copied into every ancestor.
Refreshing a getter replaces its snapshot; parents retain the versions they
actually observed. Blocking-state freshness checks traverse the shared DAG once
per check. Forward references snapshot active producers rather than forming
owning cycles. `getter_reads` and `getter_edges` in the final statistics count
the currently cached snapshots; `ast_bytes` reports Clang's AST allocator.

The public `logic` type also selects its narrow integer conversions once per
width through empty base specializations. Keeping those conversion operators
templated preserves normal arithmetic overload resolution. Repeated SFINAE on
seven width-gated operators previously retained large amounts of Clang AST
storage even when the graph was tiny. Non-template conversions were rejected
because they made existing scalar arithmetic ambiguous; a trait-only rewrite
saved less than 10% and was reverted. Header tests cover layout, implicit and
constexpr conversions, and arithmetic in both C++17 and C++23.

On the 160-stage, 64-field-per-stage regression (20,802 graph nodes), three
CPU-pinned runs of the old frontend/headers versus the revised production build,
both without allocation-profiling instrumentation, had median peak RSS
**1,216,512 -> 293,756 KiB (75.9% lower)** and median lowering time
**15.23 -> 5.85 seconds (61.6% lower)**. All six graph files had identical
SHA-256 hashes. Separate instrumentation measured AST allocation falling from
997,720,064 to 174,587,904 bytes;
dependency entries fell from 837,360 to 10,401 direct reads plus 159 edges.
An isolated conversions-only experiment used 398,040 KiB; sharing dependencies
reduced that by another 26%. These are frontend measurements on a controlled
small reproducer, not full-CVA6 simulation timings.

`CppGraphMemoryChecks.py --cpphdl /path/to/cpphdl --work /tmp/graph-memory`
checks 80- and 160-stage circuits under a 768 MiB address-space limit and asserts
linear dependency counts. It also limits incremental AST growth for 256
templated port instances, subtracting the common header baseline. It is
registered as `cpp_graph_memory_scaling` on Linux.
`CppGraphDependencies.cc` checks 4,000 diamond-shaped getter transactions against
ordinary C++ and independent expectations, including branching blocking writes
and refreshing a child while a parent still holds an older dependency snapshot.

`native_graph_pipeline` is enabled with `CPPHDL_BUILD_HDLCPP=ON` and Python;
it has no Yosys/CXXRTL dependency. It checks 20,000 randomized clock/reset,
signed-shift, aggregate, partial-NBA and control-flow samples against an
independent oracle under ASan/UBSan, plus negative cases and C++ authority.
All build, timing and profiling artifacts should stay outside the checkout.
