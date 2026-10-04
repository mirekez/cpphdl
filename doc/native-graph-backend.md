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

The default C++ frontend's lifecycle differs from the SV frontend's clock-pin API:
`eval(false)` evaluates outputs; `eval(true)` performs one `_work(work_reset)` /
`_strobe()` transaction with pre-commit outputs; `step()` additionally settles
post-commit outputs. `work_reset` is an explicit one-bit input. Original C++
conversion removes some SV clock information; this default mode does not invent
clock edges or asynchronous events. The bus runner selects this contract
with `-DUSE_CPP_GRAPH`.

The shared graph also supports named clock domains and synthesis. These use
explicit clock/reset ownership rather than the default transaction contract;
see [lowering](lowering.md) and [synthesis](synthesis.md). Native model emission
is separate from the graph core, so simulation-only helpers are not required by
the synthesis backend. Eager identity/constant folding and segmented node storage
remain shared by both consumers.

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

### Declared-range correction

hdlcpp now uses the same inclusive signed-bound calculation for syntax-derived
dimensions and textual type fallback. It no longer replaces `[N-1:0]` with
width `N` without proving the range direction. Generated types call
`__hdlcpp_range_width(int32_t(left), int32_t(right))`, a `consteval` function
which widens the endpoints before computing `abs(left-right)+1`. This is a
compile-time type calculation, not a simulation helper or a runtime `.bits()`
replacement. Literal bounds are folded; dependent bounds remain dependent on
the C++ template specialization. Constant specialized widths also remain
foldable for direct child-port binding. Template argument parsing now reuses
the nesting-aware parser so commas within width expressions are not treated
as additional array dimensions.

`tests/hdlcpp/DeclaredRangesChecks.py` exercises depths 1, 2, 3 and 5,
ascending/descending ranges, negative endpoints, equal endpoints, `$bits`,
multidimensional packed arrays and clocked storage with repeated resets.
Its five configurations each check 256 transactions against an independent
oracle. Run it with `--hdlcpp`, `--cxx` and `--work /tmp/declared-ranges`;
optional `--cpphdl` tests native graph execution, or `--verilator` compares
the original SV. The ordinary-C++ mode tests both `-O0` and `-O2` with
ASan/UBSan; graph execution also uses ASan/UBSan.

All five configurations pass in all three modes. Constant-width,
expression-helper, packed-struct-array, native-layout and blocking-array-update
regressions also pass. The broader 335-case module suite has 23 failures, all
also present when run with the previously installed converter; it is not a
clean full-suite pass.

Only the isolated CVA6 upsizer was regenerated, not the full design. Its
`bufptr_t` is now two bits at depth one, and graph lowering passes the
zero-width-storage error. The next rejection is the existing unsupported
wide dynamic slice **write** in `_work` (1,329 nodes, five registers).

Ordinary-C++ upsizer testing also exposed an independent cpphdl runtime issue:
`sv_cast<logic<32>>(packed_array[index])` can copy a packed element proxy's
object representation instead of its value. This reproduces without hdlcpp:

```cpp
cpphdl::array<1, cpphdl::logic<1>, true> values{};
auto view = values[0];
auto converted = cpphdl::sv_cast<cpphdl::logic<32>>(view);
```

With `-O0`, `uint64_t(view)` is zero but `uint64_t(converted)` can be nonzero
and equals the first four raw bytes of the proxy object. Sanitizer or optimized
builds can mask the failure, so a passing upsizer run under those settings is
not evidence of full runtime equivalence. The range fix does not modify this
separate library conversion path, and no full-CVA6 equivalence or speed result
is claimed.

### Full retry after the range fix (2026-09-25)

Both tools were rebuilt from the current checkout in real `/tmp`. The declared
range regressions passed again in ordinary C++ (`-O0`/`-O2`, ASan/UBSan), native
graph (ASan/UBSan), and original SV/Verilator before full regeneration.

| Stage | Result | Elapsed |
| --- | --- | --- |
| CVA6 regeneration, `cv32a6_imac_sv32` | 287 headers, 284 conversion sources, zero failures | 1,374.537 s |
| Full native-graph lowering | Frontend rejection after 1,841,047 nodes | 392.234 s |

Sampled peak lowering-process RSS was 3,716,120 KiB (3.54 GiB); no resource
guard fired. The main retry's peak logical workspace was 212.51 MiB. These
frontend timings were collected with unrelated workloads active and are not
simulation-speed comparisons. No full-system executable, simulated cycle count,
or equivalent-output comparison was obtained. Owned temporary artifacts were
removed after recording the result.

The previous upsizer zero-width pointer failure is passed. The first full-design
rejection is now in
`issue_stage_i.i_issue_read_operands::fu_data_n_comb_func`:

```text
C++ graph: unsupported C++ hardware type: struct cpphdl::logic<0>
```

This is a different case: the PC sign-extension replication in
`core/issue_read_operands.sv:717` is empty when `XLEN == VLEN == 32`.
hdlcpp's generated scalar concatenation helper receives, schematically:

```cpp
__hdlcpp_concat_2({uint64_t(cpphdl::repeat<0, 1>(sign)), 0,
                  uint64_t(pc), 32});
```

The helper skips the zero-width contribution, but graph lowering rejects its
argument before reaching that helper. The graph frontend's `valueWidth` permits
zero-width temporaries only while lowering a recognized `cat` expression.
The ordinary scalar cast is outside that context. Do not relax the restriction
on zero-width addressable storage to fix this temporary-expression case.

Minimal C++ reproduction (save as `EmptyRepeatCast.cc`):

```cpp
#include <cpphdl.h>
using namespace cpphdl;
struct EmptyRepeatCast : Module {
    _PORT(logic<32>) pc_in;
    _PORT(logic<32>) result_out = _ASSIGN_COMB(result_func());
    logic<32> result;
    logic<32>& result_func() {
        result = uint64_t(repeat<0, 1>(logic<1>(pc_in()[31]))) | uint64_t(pc_in());
        return result;
    }
};
extern EmptyRepeatCast cpphdl_top;
```

Run `cpphdl --native-graph --top cpphdl_top --output /tmp/empty-repeat-cast EmptyRepeatCast.cc`.
It reproduces the rejection after two nodes. Ordinary C++ passes 256 input
samples with output equal to input. Replacing only the result expression with
`cat{repeat<0, 1>(logic<1>(pc_in()[31])), pc_in()}` lowers successfully, as does
a small SV packed-array sign-extension example that hdlcpp emits using `cat`.
No production code was changed during this retry.

### Full retry after the empty-scalar-conversion fix (2026-09-25)

Both tools were rebuilt in real `/tmp`. The exact `EmptyRepeatCast` reproducer
now passes 2,048 ordinary-C++/native-graph/oracle samples with ASan/UBSan.
Zero-concat tests pass another 2,048 samples, including ordered side effects;
eight invalid zero-width-storage/port/standalone cases remain rejected.
Four SV replication configurations pass 2,048 C++/graph/Verilator samples each.
All five declared-range configurations also pass graph/C++/oracle checks.

| Stage | Result | Elapsed |
| --- | --- | --- |
| CVA6 regeneration, `cv32a6_imac_sv32` | 287 headers, 284 conversion sources, zero failures | 1,196.726 s |
| Full native-graph lowering | Frontend rejection after 2,012,709 nodes | 413.198 s |

Sampled peak lowering-process RSS was 3,801,328 KiB (3.63 GiB), with no resource
guard stop. The main retry's peak logical workspace was 223.70 MiB. These are
frontend measurements, not simulation timings. No full-system executable or
equivalent-output/cycle comparison was obtained. All owned temporary build and
generated artifacts were removed after recording the result.

The previous empty-replication failure is passed. The next failure is:

```text
C++ graph: direct current-state C++ mutation unsupported: $local9458
```

The innermost failing method is the generated HPDcache retry-table adapter
`hpdcache_rtab_i::__port_bind_pop_mux_i_data_i_in_packed_array_comb_func`.
It copies a register-backed array before packing its elements:

```cpp
auto __cpphdl_src = req_q;
```

Since `req_q` has type `reg<array<..., rtab_entry_t>>`, `auto` preserves the
`reg<>` wrapper in the local copy. The graph frontend initializes local
variables through `write()`, which rejects any target whose type is `reg<>`,
even when its key is `$local...` and no design register is being mutated.

Minimal reproducer, `LocalRegCopy.cc`:

```cpp
#include <cpphdl.h>
using namespace cpphdl;
struct LocalRegCopy : Module {
    _PORT(logic<8>) data_in;
    _PORT(logic<8>) result_out = _ASSIGN_COMB(result_func());
    using payload_t = array<1, logic<8>>;
    reg<payload_t> state;
    logic<8> result;
    logic<8>& result_func() {
        auto snapshot = state;
        result = pack_value<8>(snapshot[0]);
        return result;
    }
    void _work(bool) { state._next[0] = data_in(); }
    void _strobe() { state.strobe(); }
};
extern LocalRegCopy cpphdl_top;
```

Run `cpphdl --native-graph --top cpphdl_top --output /tmp/local-reg-copy LocalRegCopy.cc`.
It fails after three nodes with `direct current-state C++ mutation unsupported:
$local1`. Ordinary C++ passes 2,048 clocked transactions under ASan/UBSan.
Changing only the local declaration to `payload_t snapshot = state;`, or to
`const auto& snapshot = state;`, lets graph lowering and all 2,048 runtime
transactions pass too. These controls isolate the wrapper-copy rejection;
they were not applied to generated CVA6 headers or production sources.

### Full retry after the local-register-copy fix (2026-09-25)

Fresh tools pass all four local-copy configurations (4,096 clocked transactions
each) in ordinary C++, native graph and Verilator. Tests exercise independent
current/next snapshots, by-value calls, returns and local strobes. Five illegal
register-write cases remain rejected. Zero-concat and declared-range graph
regressions also pass before full regeneration.

| Stage | Result | Elapsed |
| --- | --- | --- |
| CVA6 regeneration, `cv32a6_imac_sv32` | 287 headers, 284 conversion sources, zero failures | 1,100.375 s |
| Full native-graph lowering | Frontend rejection after 2,313,018 nodes | 728.746 s |

Sampled peak lowering-process RSS was 4,096,180 KiB (3.91 GiB), with no resource
guard stop. The main retry's peak logical workspace was 212.51 MiB. No full
simulation or equivalent-output/cycle performance comparison was obtained.
All owned build/generated artifacts stayed in real `/tmp` and were removed.

The local-register-copy failure is passed. The next rejection is an unsupported
anonymous nested type, `icache_rtrn_t::(unnamed ... cva6.h:3579:5)`, reached from
`i_axi_arbiter::icache_miss_resp_o_inv_all_comb_func`. The original declaration
is the nested packed `inv` record in `core/cva6.sv:189`. Its generated C++ record
has no packed-width or field-offset metadata. The containing `icache_rtrn_t`
also omits `inv` from its width, field count, offsets and packing operations.
Allowing an arbitrary C++ struct through cpphdl's width check would not repair
that incorrect SV-to-C++ representation.

Minimal hdlcpp reproducer, `NestedPacked.sv`:

```systemverilog
module NestedPacked #(
    parameter int WIDTH = 8,
    localparam type reply_t = struct packed {
        struct packed {
            logic all;
            logic [WIDTH-1:0] idx;
        } inv;
        logic [3:0] tail;
    }
) (
    input logic all_i,
    input logic [7:0] idx_i,
    output logic [12:0] packed_o,
    output logic [31:0] bits_o
);
    reply_t reply;
    always_comb begin
        reply = '0;
        reply.inv.all = all_i;
        reply.inv.idx = idx_i;
        reply.tail = 4'ha;
    end
    assign packed_o = reply;
    assign bits_o = $bits(reply_t);
endmodule
```

Run `hdlcpp NestedPacked.sv` in a temporary directory. The emitted `reply_t`
contains `inv`, but `_size_bits()` returns **4**, `__hdlcpp_field_count` is **1**,
and `pack()` packs only `tail`. For `all_i=1, idx_i=8'ha5`, generated C++ returns
`bits_o=4, packed_o=0x000a`; the correct result is `13, 0x1a5a`.

This was checked against Verilator using the equivalent `typedef struct packed`
declaration in the module body, with the same fields, assignments and ports.
That reference returns `13, 0x1a5a`. The installed Verilator internally errors
on the tiny nested `localparam type` form itself, so this is explicitly a
typedef-reference comparison, not a successful run of that exact source text.
hdlcpp's typedef variant has a separate emission failure: it leaves raw SV
`struct packed { logic all; logic [7:0] idx; }` inside generated C++.

A C++-only reduction confirms the immediate graph rejection:

```cpp
#include <cpphdl.h>
using namespace cpphdl;
struct Reply {
    struct { logic<1> all; logic<8> index; } inv;
    static constexpr size_t _size_bits() { return 9; }
};
struct NestedAggregate : Module {
    _PORT(logic<1>) result_out = _ASSIGN_COMB(result_func());
    logic<1> result;
    logic<1>& result_func() {
        result = (decltype(Reply{}.inv){}).all;
        return result;
    }
};
extern NestedAggregate cpphdl_top;
```

The reduction fails after one node on the anonymous inner type. Ordinary C++
returns zero. Adding only the
inner `_size_bits()` moves the diagnostic to `missing packed field metadata:
all`; a named inner type with width 9 and offsets `all=8, index=0` lowers and
returns zero in C++ and graph under ASan/UBSan. No production code was modified
during this retry; the nested packed representation remains to be fixed.

### Nested packed-struct lowering fix (2026-09-25)

hdlcpp now uses one recursive aggregate emitter for typedefs, header and body
localparam types, default type parameters, and anonymous aggregate variables.
Each anonymous nested record becomes a named inner C++ type with the same
packed-width, field-offset, pack/unpack and layout-copy contract as its parent.
Nested fields participate in the parent's width and packing in SV declaration
order. The reproducer's layout is therefore 13 bits, not 4; `inv` occupies the
upper 9 bits and `tail` the lower 4. cpphdl needs no exception for anonymous
objects and no host-`sizeof` layout fallback.

Nested field types are registered for expression lowering, with `typename`
preserved in dependent C++ declarations. Packed-array dimensions and their
declared lower bounds are retained. Cross-file traits export the named inner
records as well as the outer record; field lookup splits off the package scope
without discarding the nested type path. A separate package/consumer test
confirmed that omitting this metadata incorrectly indexes `logic [15:8]` at
positions 8 through 15 instead of translating them to 0 through 7.
Nested lvalues also retain the already-normalized parent expression: a write
to `packet.left.pair[3].hi` (or its bit zero) must use element 1 for a `[3:2]`
array, rather than reconstructing the original SV index after visiting `.hi`.

`tests/hdlcpp/NestedPackedChecks.py` covers six declaration forms, narrow and
wide values, packing/unpacking, zero initialization, sibling nested records,
two levels of nesting, packed arrays with nonzero bounds, and separately
converted package/consumer files. Its CTest entries are
`hdlcpp_nested_packed_cpp`, `hdlcpp_nested_packed_graph`, and
`hdlcpp_nested_packed_verilator`. The Verilator comparison uses the equivalent
typedef declaration for the header/local/default type-parameter forms because
the installed Verilator internally errors on the tiny localparam-type form;
the other forms use the original declarations.

Validation with a fresh hdlcpp build passed all 13 configurations (512 input
samples each) in ordinary C++ at both `-O0` and `-O2` with ASan/UBSan, native
graph with ASan/UBSan, and the Verilator reference comparison. The cross-file
test separately passed all 2,048 value/index combinations at both optimization
levels with sanitizers. Existing `native_packed_layout` (including sanitized
legacy/native layouts), `packed_struct_array_expr`, `constant_widths`,
`expression_helpers`, and `blocking_array_update` scripts also passed.
The older `test_structs` text-golden suite still stops at its unrelated
pre-existing range-rendering expectation in `hdlcpp/tests/test_structs.cpp:569`;
that declaration contains no nested struct. The previous declared-range change
no longer emits the expected `,array<(((uint64_t)(1)` text.

This is a small-regression fix, not a new full-CVA6 conversion or performance
result. No full CVA6 regeneration was run for this change. All owned build and
generated artifacts were kept in real `/tmp` and removed after validation.

## Tests

### Full retry after the nested-struct fix (2026-09-25)

Both tools were rebuilt from current sources outside the checkout. The nested
packed regression passed before full conversion. The original target remains
`cv32a6_imac_sv32`, with the generated `ariane_testharness<>` as the graph root.

| Stage | Result | Wall time |
| --- | --- | --- |
| Full SV regeneration | 284 sources, 287 headers, zero failures | 1,039.738 s |
| Initial full lowering | C++ parsing failed in RVFI | 26.134 s |
| Metadata-aware regeneration after the fix below | 24 sources, zero failures | 440.562 s |
| Full lowering retry | Graph rejection after 1,715,774 nodes | 342.952 s |

The initial parse failure was six undeclared `is_compressed` references in
`cva6_rvfi.h`. Normalizing array indices exposed an hdlcpp declaration-scanner
bug: a statement such as
`entries[(unsigned)(PORTS - 1)].is_compressed = is_compressed[0];`
contains whitespace in the index, so `declarationName` incorrectly classified
the field name as a local declaration. That protected the RHS signal from
rewriting to `is_compressed_comb_func()`. The fix rejects member and qualified
lvalue suffixes when recognizing declarations. It does not modify generated
C++ or replace any CVA6 hardware.

The new declaration unit test failed before the fix and passed afterward;
the complete comb unit suite passed at `-O0` and at `-O2` with ASan/UBSan.
`NestedPackedDeep` now deliberately shares the name `hi` between a signal and
an indexed nested field. All 13 nested-packed configurations passed again in
ordinary C++ (`-O0`/`-O2`, sanitized), native graph (sanitized), and the same
Verilator-reference matrix described above. The cross-file checks also passed.

After RVFI parsing was repaired, the full graph attempt reported:

```text
C++ graph: nodes=1715774 cells=8701 methods=6828 registers=1049
           getter_reads=8925 getter_edges=10875 ast_bytes=2121586933
C++ graph: C++ write out of bounds
  in elaboration_root::_work
  in elaboration_root.i_ariane::_work
  in elaboration_root.i_ariane.i_cva6::_work
  in elaboration_root.i_ariane.i_cva6.issue_stage_i::_work
  in elaboration_root.i_ariane.i_cva6.issue_stage_i.i_scoreboard::_work
  in elaboration_root.i_ariane.i_cva6.issue_stage_i.i_scoreboard::mem_n_comb_func
```

The bounds diagnostic comes from the constant-offset write check in
`CppGraph.h`; it does not identify the individual assignment. It has not yet
been reduced far enough to attribute it to emitted C++ versus graph lowering.
The regenerated I-cache return type contains the named inner type and layout
metadata, but this full attempt stops in the scoreboard before the previous
I-cache rejection point. Small-test success is not an end-to-end CVA6 pass.

Peak sampled lowering-process RSS was **3,422,632 KiB (3.26 GiB)**. No resource
guard fired. Peak logical workspace was **371.10 MiB**, including the small
regression builds. All owned artifacts stayed in real `/tmp` and were removed.
There is still no full simulation, cycle count, or equivalent-output Verilator
timing comparison; the failed lowering times are not execution benchmarks.

### Scoreboard array-write bounds (2026-09-25)

The scoreboard contains `mem_n[8'(trans_id_i[i])-1].sbe.valid = 1'b1`.
With a zero transaction ID, this selects an element outside the array. SV
ignores that write, but hdlcpp emitted an ordinary C++ array reference with
an unsigned underflowed index. A reduced SV version of this operation passed
4,096 Verilator checks, crashed the generated C++ under ASan, and reproduced
the graph rejection when the ID was bound to zero. The enhanced diagnostic
identified offset 21,474,836,478, width 1, in a 40-bit array.

hdlcpp now guards direct indexed-array assignments before forming their
lvalues. It materializes the RHS and each normalized index once, retains
known 64-bit index values without losing SV's 32-bit arithmetic wrap, and
writes only when every array
coordinate is valid. This also covers addressable nested fields and `_next`
writes. There are no generated lambdas or handwritten scoreboard replacements.
CppGraph's strict bounds rejection remains enabled; its diagnostic now includes
the storage key, offset, write width, and storage width, with an overflow-safe
comparison.

The new RHS temporary exposed an existing late-binding distinction between
ordinary assignments and local initializers. Both must read this comb's
partially updated result, not a projected getter's final value. Self-read
rebinding now happens before either path. The existing deep nested-field
regression caught the incorrect double transformation before this correction.

The first full regeneration also exposed two assignment-repair passes treating
typed local initializers as lvalues, producing invalid
`decltype(Type variable)` expressions. Those passes now distinguish declarations
from assignments. The regression includes aggregate casts and nested conditional
RHS expressions that failed to compile before the correction. It also checks
that an explicitly 32-bit index addition still wraps to zero, rather than
accidentally becoming a rejected 64-bit index.
RHS materialization uses the existing `sv_cast` helper's assignment semantics:
generated packed records accept raw logic through `operator=` without
necessarily providing a converting constructor. The CSR PMP writes exposed
this difference. Keeping the initialized temporary in one declaration also
preserves field-projection dependencies. The regression covers implicit
logic-to-record writes as well as explicit casts.
Bare aggregate initializer lists are materialized with their explicit target
type instead of being passed to a template that cannot deduce their source
type. This case is exercised by ID-stage issue entries and by the regression's
nested positional assignment pattern.

`ArrayWriteBoundsChecks.py` covers variable and constant-zero IDs, underflow,
upper bounds, indices above 32 bits, enabled/disabled writes, and clocked
state updates. It tests the packed eight-entry scoreboard shape and a
three-entry unpacked array with literal nonzero bounds. All 56,320 samples
passed in each of C++ `-O0`, C++ `-O2`, native graph, and the RTL comparison;
the C++ and graph runs used ASan/UBSan. The installed Verilator 5.034 crashes
on direct 64-bit nested-field indices. Only those additional reference writes
use an explicit validity guard before a 32-bit cast (`BOUNDS_RTL_SAFE_INDEX`);
the scoreboard predecessor write remains unchanged. Comparisons use exactly
the declared signal bits, excluding unused C++ storage padding.

All 13 existing nested-packed configurations also passed in C++ at both
optimization levels and in graph mode, including the cross-file checks.
`native_packed_layout`, `packed_struct_array_expr`, `constant_widths`,
`expression_helpers`, and `blocking_array_update` passed. This is not a claim
that scheduled-memory indices or all parameterized packed-range cases are
fixed; the guarded path handles direct array assignments.

The full CVA6 retry produced 287 headers without conversion failures. After
the integration corrections above, the full C++ syntax check passed in
26.827 s. The latest native-graph lowering attempt reached 4,194,304 nodes,
10,951 cells, and 8,652 methods before reporting `std::bad_alloc` after
937.130 s. Peak sampled process RSS was 4,838,336 KiB under a 5,600,000 KiB
address-space limit; the external resource guard did not terminate it.
The previous out-of-bounds diagnostic did not recur in this attempt, but
lowering did not finish. There is therefore still no full simulation or
equivalent-output/cycle-count performance comparison. All owned build and
generated artifacts stayed in real `/tmp` and were removed after validation;
peak logical workspace size was 406.73 MiB.

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

#### Construction-time graph memory (2026-09-25)

The scoreboard retry's failure at exactly 4,194,304 nodes prompted two isolated
checks. On this toolchain `sizeof(Node)` is 144 bytes. Growing a vector from
2^22 to 2^23 entries needs its old 576 MiB allocation and a new 1,152 MiB
allocation simultaneously, before counting operands, aliases, or Clang's AST.
A storage-only reproducer fails at the identical node count under a 1 GiB
address-space cap. Segmented node storage crosses that boundary under the same
cap (4,194,305 nodes, 626,596 KiB peak RSS). This removes the allocation spike,
not the inherent cost of storing live nodes.

More importantly, graph construction previously retained nodes and per-bit map
entries even when immediate simplification replaced every output with constants
or existing bits. `Graph::add` now applies the existing simplification rules
before retaining a node. Fully folded nodes allocate no persistent storage or
aliases. Partially folded nodes keep their required aliases; state, inputs,
unresolved wires, host effects, and memory accesses retain their identities.
Late hierarchy connections still use the same simplifier during `optimize()`;
there is no second set of optimization rules or new user flag.

Measured with fresh before/after builds:

| Check | Before | After |
| --- | ---: | ---: |
| 8,192 direct graph masked-write identities: peak RSS | 219,384 KiB | 4,068 KiB |
| Same check: nodes / aliases | 57,346 / 2,629,632 | 2 / 0 |
| 4,096 ordinary C++ masked-write iterations: peak RSS | 356,072 KiB | 162,644 KiB |
| Same C++ lowering: nodes | 282,658 | 16,389 |
| Converted array-write fixture: nodes | 3,411 | 700 |

The C++ stress reduction is 54.3% in peak RSS. Its input and output bit vectors
are identical after lowering, proving the expected identity for all input
values. These are construction/lowering measurements, not simulation-speed
claims. The tiny converted array fixture remains dominated by the common Clang
baseline (137,268 versus 136,052 KiB); its node reduction is not a large total
memory reduction at that scale.

`graph_construction_memory` checks growth past 262,144 nodes and 65,536 repeated
identities under a 96 MiB address-space cap. Its sanitized serialization/emission
round trip checks 65,536 mixed-operation samples, including partial folding,
late wire connections, and a 129-bit operation. `cpp_graph_memory_scaling` also
checks the ordinary C++ stress case. Frontend diagnostics now include logical
node bytes, operand-vector capacity bytes, and alias count alongside AST bytes;
these are component measurements, not a substitute for process RSS.

The full `cv32a6_imac_sv32` retry regenerated all 287 headers without failures
in 1,035.748 s. Native lowering then reached 9,147 methods and 1,429 registers,
versus 8,652 methods and 1,357 registers at the preceding allocation failure.
It stopped after 1,084.721 s on the separate unsupported operation
`write to a wide dynamic slice`, in
`i_cache_subsystem.i_axi_arbiter.i_icache_hpdcache_data_upsize::_work`.
There was no allocation failure or external resource-guard termination.
The address-space cap remained 5,600,000 KiB. Peak sampled process RSS was
4,169,848 KiB (3.98 GiB), versus 4,838,336 KiB (4.61 GiB) in the previous
failed attempt: 13.8% lower while reaching further into the design. These
attempts stop at different operations, so their elapsed times are not an
equivalent-work speed comparison.

At the new stop, statistics report 1,629,866 nodes, 234,700,704 logical node
bytes, 951,379,040 operand-capacity bytes, 563,371 aliases, and 2,269,919,830
AST bytes. This is a reduction in graph construction overhead, not elimination
of the substantial Clang and live-operand footprint. Full simulation remains
blocked by the wide-slice write, so there is no cycle/output/timing comparison.

The construction, frontend-memory, converted bounds, nested-packed,
ordinary-C++ graph pipeline, ordered switch-effect, and wide-slice read
regressions passed, including their ASan/UBSan cases. All owned artifacts stayed
in real `/tmp` (638.13 MiB peak logical workspace) and were removed afterward.

`native_graph_pipeline` is enabled with `CPPHDL_BUILD_HDLCPP=ON` and Python;
it has no Yosys/CXXRTL dependency. It checks 20,000 randomized clock/reset,
signed-shift, aggregate, partial-NBA and control-flow samples against an
independent oracle under ASan/UBSan, plus negative cases and C++ authority.
All build, timing and profiling artifacts should stay outside the checkout.

### HPDcache upsize wide-write investigation (2026-09-25)

This blocker belongs to CppHDL's C++-to-graph lvalue lowering, not to an inability
of the ordinary C++ runtime to store or update 128-bit values. No production
lowering change was made during this investigation.

For `cv32a6_imac_sv32`, `AxiDataWidth` is 64 and `IcacheLineWidth` is 128.
The `i_icache_hpdcache_data_upsize` instance has `WR_WIDTH=64`, `RD_WIDTH=128`,
and `DEPTH=1`. Its buffer is a packed `[0:0][1:0][63:0]` object: two 64-bit
words, not a large RAM. The RTL write in `hpdcache_data_upsize.sv:158` is:

```systemverilog
buf_q[wrptr_q][words_q[wrptr_q]] <= wdata_i;
```

`hdlcpp_expr.cc::packedArrayElementWriteTarget` flattens nested packed-element
assignments to the public `bits()` API. Removing only expression spelling
noise, the generated write is:

```cpp
low = (wrptr_q * 2 + words_q[wrptr_q]) * 64;
buf_q._next.bits(low + 63, low) = wdata_i_in();
```

The selected width is **64**, but the receiver width is **128**. The latter
is what triggers the unsupported path. In `CppGraph.h`, dynamic `bits()` on a
receiver wider than 64 bits calls `wideSliceRead`, creates a value rather than
an addressable destination, and sets `readOnlyWideRange`. `write()` explicitly
rejects that value. Its storage key and writeback identity were not retained.
Removing the rejection is not a fix: it would leave a non-addressable temporary.
The ordinary `logic_bits` proxy, in contrast, retains its parent and endpoints
and updates the parent on assignment.

Fresh tools reproduced the same rejection when lowering just the saved full-run
`hpdcache_data_upsize<64,128,1>` header, with its sole needed package typedef
supplied as `hpdcache_uint = cpphdl::logic<32>`. A fresh conversion of a reduced
SV nested packed-array write also emits dynamic `bits()` and reproduces it.
No full CVA6 regeneration was needed.

Diagnostic substitutions isolated the unsupported representation:

| Equivalent write representation | Ordinary C++ | Native graph |
| --- | --- | --- |
| Dynamic `bits()` on nested packed 128-bit buffer | Pass | Rejected |
| Dynamic `bits()` on plain `logic<128>` | Pass | Rejected |
| Nested element indexing: `pending[0][word] = data` | Pass | Pass, 33 construction nodes |
| Branch selecting constant `[63:0]` or `[127:64]` slice | Pass | Pass, 25 construction nodes |

The reduced original SV, freshly hdlcpp-generated C++, all four ordinary-C++
forms, and both supported graph forms passed the same 32,768-vector oracle
sequence. It covers reset, disabled-write retention, both destination words,
preservation of the other word, and two ordered writes to the same word
(last assignment wins). C++ and graph runners used ASan/UBSan. The node counts
include that control/test logic and precede final graph optimization; these are
not execution-speed measurements. A separate dynamic 32-bit write into
`logic<64>` lowers successfully, confirming the receiver-width boundary.
These are reduced-write checks, not proof of the full upsize controller or CPU.

The existing minimal negative fixture also reproduces the diagnostic:

```sh
/path/to/cpphdl --lower-cpp-graph tests/graph/WideSliceWriteReject.cc \
  /tmp/new-wide-write.graph.cc cpphdl_top -- -std=c++23 -Iinclude
```

The output path must not already exist. That fixture selects 32 bits from a
128-bit receiver; the same limitation applies to the actual 64-bit selection.

The appropriate automatic fix is to retain a mutable slice descriptor (base
storage, receiver offset/extent, and both endpoints), then lower writes rather
than eagerly replacing the proxy with a read value. For this aligned,
fixed-width case, prove the 64-bit width/alignment and reuse word-lane updates:
update the low word when the flattened offset is zero, the high word when it is
64, and preserve all other pending bits. The indexed experiment confirms the
existing backend can already express this with ordinary word nodes. Preserve
write order against `_next`, not just the old registered value.

General unaligned slices need a separate word-chunk mask/shift implementation
and bounds handling; the aligned fast path must not silently accept them.
Neither the RHS width alone nor the receiver width proves the selected width.
Do not truncate the receiver to 64 bits or remove the read-only guard without
implementing writeback. The substitutions above are diagnostic experiments,
not proposed handwritten replacements for the generated hardware model.
All investigation artifacts stayed in real `/tmp` and were removed afterward.

### Wide-write retry preflight (2026-09-26)

Freshly rebuilt tools pass the new `WideWriteChecks.py` suite: 28,672 oracle
samples each in ordinary C++ and native graph, including aligned/unaligned,
overlapping, and cross-word writes. Wide-read checks (38,912 samples per mode),
the construction-memory regression, and the converted array-bounds regression
(56,320 graph/C++ comparisons) also pass. Sanitized runners use ASan/UBSan.

The saved full-run `hpdcache_data_upsize<64,128,1>` model now completes C++ graph
extraction: 232 nodes, 10 methods, and five registers. It no longer rejects the
wide write. However, the following graph-to-model emission step fails with:

```text
undriven bit: cpphdl_top.used_d_comb:0
```

`used_d_comb_func()` assigns `used_d_comb` in both explicit switch cases and
in the default branch, corresponding to the occupancy update in the original
SV. This is not actually an incomplete combinational assignment. The standalone
`tests/graph/SwitchCompleteWrite.cc` reproduces the same problem with three
input ports and a complete switch:

```sh
/path/to/cpphdl --native-graph --top cpphdl_top --cxx clang++ \
  --output /tmp/new-complete-switch tests/graph/SwitchCompleteWrite.cc
```

Extraction succeeds with 17 nodes, but emission rejects
`undriven bit: cpphdl_top.next_comb:0`. Ordinary C++ passes all 16 combinations
of the two control inputs and two-bit current value against an independent
increment/decrement/hold oracle under ASan/UBSan.

The extracted graph has the equivalent shape below, where `case2` and `case1`
are the selector comparisons:

```text
matched = case2 OR case1
result = mux(NOT matched, current,
             mux(case1, current - 1,
                 mux(case2, current + 1, unwritten_previous_value)))
```

The final operand can only be selected when `matched && !case1 && !case2`,
which is impossible. Nevertheless, the graph retains that dependency and
the emitter rejects the undriven wire. Switch lowering/complete-write reasoning
must eliminate this false retention without accepting genuinely incomplete
assignments or breaking fallthrough and ordered effects.

As a diagnostic control, passing `--frontend-flag=-DINITIALIZE_FIRST` to the
same fixture initializes the result before the switch. That graph emits
successfully and passes all 16 oracle inputs alongside ordinary C++. This is
not a proposed production workaround or a change to the generated CVA6 model.

Full regeneration was deliberately not launched after this isolated required
block failed the small-test gate, following the small-first resource constraint.
Thus this entry is **not** a new full-CVA6 conversion/simulation result or a
performance comparison. No production lowering was modified. All owned build
and validation artifacts stayed in real `/tmp` and were removed afterward.

### Complete-switch fix: full CVA6 retry (2026-09-26)

Fresh serial builds of both tools pass the complete-switch suite (2,048 oracle
inputs per mode; five incomplete-write variants still rejected), switch control
(8,192 comparisons), ordered host effects (2,048 comparisons), wide writes,
construction-memory checks, and converted array bounds. A fresh standalone
conversion of the real `hpdcache_data_upsize<64,128,1>` emits its native model
successfully: 229 nodes, ten methods, five registers. Neither a default-value
workaround nor a handwritten hardware substitution was used.

The full `cv32a6_imac_sv32` native-harness regeneration was then actually run:
332 manifest sources, 284 conversion sources, 48 skipped, 287 generated headers,
zero failures, 1,234.285 seconds. Subsequent C++ graph extraction failed after
1,442.326 seconds with sampled peak process RSS 4,061,668 KiB (3.87 GiB).
It stayed within the 5,600,000 KiB address-space cap; no resource guard fired.
These are conversion times, not simulation benchmarks.

```text
C++ graph: nodes=1650409 cells=11576 methods=9147 registers=1431
getter_reads=12633 getter_edges=16096 ast_bytes=2269919830
node_bytes=237658896 operand_bytes=962666680 aliases=571456
C++ graph: unsupported C++ expression: StringLiteral
  at ordinary/generated/core/cva6.h:8862:56
  in elaboration_root::_work
  in elaboration_root.i_ariane::_work
  in elaboration_root.i_ariane.i_cva6::_work
```

The source is the mock tracer in `core/cva6.sv:1825`:
`byte mode = "";` followed by assignments of `"D"`, `"M"`, `"S"`, and
`"U"`. hdlcpp emits `u8 mode = static_cast<unsigned>((uint64_t)(""));` and,
for example, `mode = static_cast<uint8_t>((uint64_t)("D"));`.
Those are C++ pointer-to-integer casts, not SystemVerilog string-to-integral
conversions. The correct byte values are zero for the empty string and the
character codes for the one-character strings. This is an hdlcpp semantic bug,
not a request for cpphdl to interpret host addresses as hardware constants.
The trace writes themselves are commented out, but their local computations
remain and are visited by graph extraction.

The retained tiny reproducer is `tests/hdlcpp/ByteStringLiteral.sv`, with
`ByteStringLiteralRun.cc` as its independent four-input oracle. Fresh hdlcpp
conversion reproduces the pointer casts and native graph rejection in about
two seconds. Ordinary generated C++ returned 81, 83, 85, 87 in this run;
Verilator on the original SV returned the expected 68, 77, 83, 85. The erroneous
pointer-derived values are not portable or stable.

Reproduce with fresh tools, keeping generated artifacts outside the repository:

```sh
repo=/home/me/cpphdl
work=$(mktemp -d /tmp/cva6-byte-string.XXXXXX)
cd "$work"
/path/to/hdlcpp "$repo/tests/hdlcpp/ByteStringLiteral.sv"
printf '#include "generated/ByteStringLiteral.h"\nextern ByteStringLiteral cpphdl_top;\n' > seed.cc
/path/to/cpphdl --native-graph --top cpphdl_top --cxx clang++ \
  --output "$work/graph" "$work/seed.cc"
clang++ -std=c++23 -O1 -I"$repo/include" -I"$work" \
  "$repo/tests/hdlcpp/ByteStringLiteralRun.cc" -o "$work/run"
"$work/run"
verilator --cc --exe --build -j 1 -Wno-fatal --top-module ByteStringLiteral \
  --Mdir "$work/obj" -CFLAGS '-std=c++17 -DSTRING_RTL' \
  "$repo/tests/hdlcpp/ByteStringLiteral.sv" "$repo/tests/hdlcpp/ByteStringLiteralRun.cc"
"$work/obj/VByteStringLiteral"
```

Graph generation and the ordinary C++ oracle are expected to fail until the
hdlcpp bug is fixed; the Verilator oracle passes. These reproducers are not
registered as passing regressions. No production code was changed in this retry.
There is still no completed full native model, cycle/output comparison, or
full-CVA6 runtime speed result. Owned temporary build and conversion artifacts
were cleaned after collecting these measurements.

### hdlcpp integral string literals fixed (2026-09-26)

The preceding string-literal blocker is now fixed in hdlcpp. Integral emission
uses Slang's decoded literal bytes rather than a C++ string pointer. Empty
literals emit zero; characters are packed most-significant-byte first. Values
up to 64 bits emit native integer constants, while wider values use existing
fixed-width logic and concatenation. Declared literal widths retain eight bits
per decoded byte (eight bits for an empty literal), so concatenations and casts
apply normal packed truncation and extension.

Typed assignments, initializers, conditional branches, numeric operands, and
integral casts share this conversion. String-valued expressions retain string
emission; comparison lowering distinguishes numeric operands from string
parameters, including parameters normalized to `hdlcpp_fixed_string`.
Expression classification uses syntax kinds, not whether expression text
starts with a quotation mark, so `"A" + select_i` is treated as arithmetic.
No runtime helper, lambda, hardware substitution, or cpphdl change is needed.

The original generated tracer pattern now becomes:

```cpp
u8 mode = static_cast<unsigned>(0ull);
mode = static_cast<uint8_t>(68ull);
```

`tests/hdlcpp/ByteStringChecks.py` converts both `ByteStringLiteral.sv` and
`ByteStringContexts.sv` afresh. The four original cases and 32 context cases
pass independent expected-value checks in ordinary C++ at O0 and O2, native
graph versus ordinary C++, and Verilator on the original SV. C++ and graph
runners use ASan/UBSan. Coverage includes empty strings, decoded newline/tab,
quote/backslash, octal/hex escapes, embedded NUL, concatenation, width casts,
typed constants, parentheses, conditionals, arithmetic, numeric equality,
96-bit character data extended to 128 bits, and unchanged string-parameter
comparison behavior. The corresponding C++, Verilator, and graph checks are
registered with CTest.

Fresh-tool validation also passes the existing constant-width suite,
expression-helper suite (including Verilator references), and converted
array-bounds graph checks (56,320 comparisons). The latest validation completed
without resource-guard termination. All owned build artifacts stayed in
`/tmp` and were removed. Full CVA6 regeneration was not repeated during this
fix; passing the small regression is not a full-design simulation result.

### String-fix CVA6 retry: regeneration passed, RAM gate blocked (2026-09-27)

Fresh serial builds passed the string-literal checks in ordinary C++, native
graph, and Verilator, plus complete-switch, switch-control/effects, wide-write,
construction-memory, and converted array-bounds regressions. Fresh conversion
and native model emission for `hpdcache_data_upsize<64,128,1>` also passed.

The full `cv32a6_imac_sv32` native-harness SV conversion completed in
1,502.334 seconds: 332 manifest sources, 284 conversion sources, 48 skipped,
287 generated headers, zero failures. The regenerated `core/cva6.h:8862`
initializes the tracer's byte to `0ull`, and its assignments use
`68ull`, `77ull`, `83ull`, and `85ull`, confirming that this full conversion
contains the hdlcpp string fix.

Full C++ graph lowering was **not launched**. Its safety gate required
4,700,000 KiB available RAM before starting the previously approximately
3.87 GiB lowering workload. An unrelated `scalepnr` process grew to about
4 GiB RSS, leaving about 2 GiB available. The gate timed out after 900 seconds;
no unrelated process was stopped and no unsafe full lowering was attempted.
This is a resource-availability block, not a newly discovered compiler error.

There is therefore no new full-model emission, simulation, cycle/output check,
or performance comparison from this retry. All owned build/conversion artifacts
were confined to real `/tmp` (approximately 226 MiB at the end) and cleaned.
No production code was changed during this retry.

### Full retry after RAM became available: unary operator crash (2026-09-28)

Both tools were rebuilt serially in real `/tmp`. String checks in C++,
native graph, and Verilator; complete-switch and control/effect checks; wide
writes; construction-memory; converted array bounds; and freshly converted
standalone upsize model emission all passed before full regeneration.

Full `cv32a6_imac_sv32` native-harness SV conversion passed in 1,113.726 seconds:
332 manifest sources, 284 conversion sources, 48 skipped, 287 generated headers,
zero failures. This time enough RAM was available and full graph lowering
actually ran. It failed after 1,254.905 seconds with child status -11 (SIGSEGV),
at sampled peak process RSS 4,254,148 KiB (4.06 GiB). No resource guard fired.

A debugger replay reused the same generated headers, without another SV
regeneration, and reproduced the crash. The top frames were:

```text
Lowering::exprImpl(clang::Expr const*) + 28
Lowering::expr(clang::Expr const*)
Lowering::callExpr(clang::CallExpr const*)
```

The same binary crashes at exactly the same instruction and call-site offset
on `tests/graph/UnaryCatNegation.cc`, whose essential expression is:

```cpp
next = -cpphdl::cat{cpphdl::logic<1>(0), data_in()};
```

The call-site offset maps to `CppGraph.h:1036` using an O1 line-table build.
The overloaded-operator dispatcher includes `OO_Minus` in its binary branch
without testing arity, then unconditionally evaluates `call->getArg(1)`.
The free unary negation overload for `cpphdl::cat` has only one argument.
This out-of-bounds AST access passes an invalid expression pointer into
`exprImpl`; the tiny debugger run showed `0x7fff00000001`. This is a
cpphdl operator-dispatch bug, not an hdlcpp string regression or demonstrated
stack/memory exhaustion.

CVA6 contains the corresponding unary negations of 65-bit concatenations in
`core/cva6_rvfi.sv:195`, also at lines 200, 206, and 212. The generated
`compute_amo_wdata` contains `-(cat{...})`. The reduced nine-bit concatenation
demonstrates that a wide operand is not required to trigger the compiler crash.

Reproduce in a new temporary directory with freshly built cpphdl:

```sh
repo=/home/me/cpphdl
work=$(mktemp -d /tmp/cva6-unary-cat.XXXXXX)
/path/to/cpphdl --native-graph --top cpphdl_top --cxx clang++ \
  --output "$work/graph" "$repo/tests/graph/UnaryCatNegation.cc"
clang++ -std=c++23 -O1 -fsanitize=address,undefined -I"$repo/include" \
  "$repo/tests/graph/UnaryCatNegationRun.cc" -o "$work/native"
"$work/native"
```

The original C++ passes all 256 byte-input oracle cases under ASan/UBSan;
graph extraction crashes. As a diagnostic control, the fixture's
`BUILTIN_NEGATION` macro explicitly converts the nine-bit concatenation to
`uint64_t` before negation. Passing
`--frontend-flag=-DBUILTIN_NEGATION` and building the runner with
`-DBUILTIN_NEGATION -DNEGATE_GRAPH -fsanitize=address,undefined` makes native
graph emission and all 256 C++/graph comparisons pass. This is only a reduced
diagnostic control, not a production workaround or a claim that truncating a
65-bit SV operand to 64 bits is correct.

The needed dispatcher fix must distinguish unary from binary operators and
respect the C++ overload's result type and operand width. The failing fixture
is retained but not registered as a passing test. No production code was
modified. Full model emission and simulation remain blocked, so there is no
new cycle/output equivalence or runtime comparison. Owned temporary artifacts
(about 255 MiB at the end) were removed after preserving this evidence.
