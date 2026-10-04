# Shared Lowering, Simulation, and Synthesis

CppHDL needs two consumers of the same design semantics: a fast native simulator
and a hardware synthesis flow. They should share operation and dependency
analysis, but must not share an optimization objective or silently change each
other's clock behavior.

## Architecture

```text
C++ RTL / HLS method expansion
              |
    Typed operations and dependencies
    State, memory effects, clock contracts
              |
       Shared simplification
              |
      +-------+-------------------+
      |                           |
Native execution             Hardware scheduling
Word lowering                Resource and pipeline selection
CPU thread scheduling        RTL emission and gate mapping
      |                           |
Fast C++ model                Gate-level Verilog
```

The common representation describes what an operation computes, not how a CPU
executes it. Preserve widths, signed behavior, conditions, source names, and
dependencies. A hardware addition must not permanently become several unrelated
host-word instructions just because the simulation backend uses 64-bit words.

Useful shared work includes constant folding, resolving connections, equivalent
expression detection, live dependency traversal, and memory effect analysis.
Reuse a memory read only while its value is proven unchanged. A result valid
inside one zero-time region is not automatically reusable across a clock edge.

## Separate Optimization Policies

Native optimization preserves the existing cycle-by-cycle behavior. It minimizes
host instructions, redundant evaluation, memory traffic, and synchronization
between simulation threads.

Hardware optimization balances critical-path delay, area, latency, and throughput.
Sharing an arithmetic unit can reduce area but add selection logic or cycles.
Combining work into one cycle may improve simulation speed while making hardware
timing worse. These choices need distinct policies and cost models.

Moving existing registers is retiming. Introducing pipeline stages and scheduling
HLS work across them is a broader transformation: it may change transaction
latency. Neither is permission to alter an RTL interface's timing silently.

Before pipeline scheduling, define:

- Target clock period and the technology used for delay estimates.
- Permitted latency and initiation interval.
- Clock domains, edge polarity, reset semantics, and register enables.
- Memory ports, read latency, write ordering, and read/write collision behavior.
- Handshake backpressure and which externally visible operations must stay ordered.

CDC, reset, memory, and protocol boundaries are not interchangeable with ordinary
combinational dependencies. Unsupported contracts must produce an error.

## Current Implementation

`include/cpphdl_graph.h` holds the graph, shared simplification, serialization,
and live dependency ordering. Values are bit references; fields, concatenations,
and port connections do not imply runtime copies. Signed comparisons and shifts
are distinct operations.

Native C++ emission is in `include/cpphdl_graph_native.h`. The original
`Graph::emit()` API remains available through the main graph header. Defining
`CPPHDL_GRAPH_CORE_ONLY` excludes native emission for another backend.

Generated graph construction programs expose `makeGraph()`. Define
`CPPHDL_GRAPH_NO_MAIN` when linking one with a different backend. Ordinary native
graph commands continue to compile and execute their default main function.

The serialized clock contract distinguishes explicit event expressions, a
single rising-edge lifecycle transaction, and named clock edges. Named domains
carry clock names and frequencies; every state group records its clock and edge.
Old records without this metadata
remain usable by the native backend, but cannot silently enter synchronous
synthesis.

This is an incremental separation, not a finished abstract synthesis IR:

- Operations still use the existing 64-bit node encoding. Wider connections and
  bitwise values can span nodes; general arbitrary-width arithmetic remains work
  for a later representation revision.
- HLS scheduled blocks contain explicitly sized expression strings. On
  `--synth`, `synth/ScheduledGraph.cpp` lowers those expressions and the
  scheduler's control edges directly into the shared graph, before SV function
  sharing. Wrapper detection selects the appropriate scheduler automatically;
  ordinary SV conversion does not perform this extra graph export.
- Source locations, technology delay models and arithmetic resource binding
  remain planned. Opt-in pipeline insertion uses the estimates described below.

## Initial Synthesis Flow

`synth/` implements an experimental consumer of ordinary CppHDL AST lowering:

```sh
build/cpphdl --synth --top cpphdl_top --module SynthMath \
  --cxx c++ --output /tmp/cpphdl-math \
  synth/tests/math.cpp
```

The output directory must be new or empty. C++ parsing options such as `-I` and
`-D` follow `--`. Run `build/cpphdl --synth --help` for available options.

The flow lowers the root object's bindings and methods into the shared graph,
emits `operations.v`, then uses `synth/Mapping.cpp` for generic gate mapping.
`gates.v` is the gate-level Verilog result; `gates.json` records the mapped cells.
The driver rejects remaining non-generic cells instead of labeling unmapped
arithmetic as a finished gate netlist. Logs and cell counts in
`manifest.json` remain alongside the result.

One rising edge of the generated `clk` executes one `_work`/`_strobe` transaction.
`work_reset` is synchronous; reset and enable conditions remain in next-state
logic. Register assignments are simultaneous. No extra stages or initialization
are inserted. Top port names follow the graph frontend's flattened naming.

The initial backend supports integer arithmetic, comparisons, muxes, reductions,
shifts, statically expanded helper functions/loops, and synchronous registers.
It supports single-writer memories and named-clock asynchronous reset handlers
as described below. It rejects host callbacks, explicit event graphs and undeclared
clock methods. It does not
implement floating-point library functions. Scheduled HLS graphs and opt-in
retiming are described in [retiming](retiming.md). Generic gates are not an ASIC library mapping or a physical timing
guarantee. Clock-targeted synthesis needs a subsequent technology-aware stage.

Neither graph export nor gate mapping invokes Yosys or another HDL elaborator.
The mapper expands arithmetic into bit gates, shares identical gates, and maps
memories to flip-flops with decoded writes and multiplexed reads. This generic
backend does not infer FPGA block RAM or report FPGA LUT counts. Explicit keep
boxes preserve their operation-level implementations for subsequent technology
mapping. Gate tests use Verilator; passing simulation is not a formal proof.

## Multiple Clocks

Declare all clock inputs explicitly. The synthesis and native-graph commands
accept the same clock options. For a main clock and a slower secondary clock:

```sh
build/cpphdl --synth --top cpphdl_top --module MainAndSecondary \
  --primary_clock main_clk 120000000 --secondary_clock secondary_clk 40000000 \
  --output /tmp/main-and-secondary synth/tests/multiclock/main_and_secondary.cpp
```

The primary clock must have the highest frequency. For independent clock inputs,
without a primary/secondary relationship, repeat `--clock` instead:

```sh
build/cpphdl --synth --top cpphdl_top --module TwoMainClocks \
  --clock left_clk 100000000 --clock right_clk 60000000 \
  --output /tmp/two-main-clocks synth/tests/multiclock/two_main_clocks.cpp
```

Do not mix the two option forms. Frequencies are positive integer Hz metadata,
not generated clock dividers or timing guarantees. All clocks remain separate
top-level input signals. The testbench supplies their levels and phases; the
backend does not assume that one clock edge coincides with another.

For these graph flows, declaring a clock `name` requires root methods
`_work_name(bool reset)` and `_strobe_name()`. Optional falling-edge logic uses
the pair `_work_neg_name(bool reset)` and `_strobe_neg_name()`. A child can keep
ordinary `_work`/`_strobe` methods and be called from its parent's chosen domain.
Each register must be assigned and strobed by the same clock/edge. The converter
rejects multiple owners, mismatched strobes, calls to another domain's lifecycle
method, and cross-domain reads of `_next`. Use `cpphdl::reg` for persistent state;
ordinary mutable module fields are not supported as multiclock registers.

The intermediate Verilog contains a separate `always @(posedge name)` or
`always @(negedge name)` block for each populated domain/edge. Gate mapping
preserves those clock connections. Without reset handlers, `work_reset` is shared synchronous reset:
each process observes it only on its own active edge. Per-domain reset conditions
can also be expressed with ordinary input ports inside that domain's work method.
A stopped clock does not reset or advance merely because reset or data changed.

Native graph models expose a Boolean member for each named clock. Set **all**
clock and input levels for a timestamp, then call `step()` once. It detects edges,
computes next state from the old registers for all active domains, commits that
state, and settles outputs. Calling `step()` again without changing clock levels
does not generate another edge. `eval()` only settles outputs; it does not consume
clock edges. With no named clocks, the original one-transaction-per-`step()` API
is unchanged.

Multiclock tests verify digital behavior, not metastability or physical CDC
safety. Synchronizers are written explicitly in the source; this flow does not
insert synchronizers, preserve physical synchronizer placement constraints, or
automatically prove arbitrary bus crossings safe.

### Memory Across Clock Domains

A `memory<logic<W>, ROW_ELEMENTS, DEPTH>` can have one clock/edge that writes
and any number of readers in other domains. Put `memory.apply()` in the writing
domain's strobe method. Readers do not call `apply()`.

```cpp
void _work_write_clk(bool reset) {
    if (!reset && write_enable_in()) storage[write_addr_in()] = data_in();
}
void _strobe_write_clk() { storage.apply(); }
void _work_read_clk(bool reset) {
    if (read_enable_in()) read_reg._next = storage[read_addr_in()].to_ullong();
    if (reset) read_reg._next = 0;
}
void _strobe_read_clk() { read_reg.strobe(); }
```

Ordinary reads sample committed storage. At coincident edges, a registered reader
samples the old row before the writer updates it. Within the writing process,
`pending(address)` forwards earlier queued writes; multiple writes to one row
use the last assignment. Pending data must not cross clock domains. A stopped
writer neither writes nor applies its queue. Writes without a matching-domain
`apply()`, and writers from multiple clocks or edges, are rejected.

The intermediate Verilog uses a memory array and clocked writes; the current
generic-gate synthesis command maps it to registers and muxes, not device BRAM.
No memory initialization or clearing is invented. Tests write each row before
reading it. Native simulation diagnoses enabled out-of-range accesses; the RTL
guards writes and returns zero for out-of-range reads. Designs must use valid
addresses. The simulated old-data collision rule is not a guarantee for a
physical dual-clock RAM: avoid unsynchronized same-address collisions in hardware.

### Asynchronous Register Reset

For a declared clock `fast_clk`, add `_reset_pos_fast_clk()` to reset its
positive-edge registers, and `_reset_neg_fast_clk()` for its negative-edge
registers. Here `pos`/`neg` selects the **clock edge**, not reset polarity.
Both handlers use the shared active-high `work_reset` input.

```cpp
void _work_fast_clk(bool) { count._next = uint32_t(count) + 1; }
void _strobe_fast_clk() { count.strobe(); }
void _reset_pos_fast_clk() { count.clr(); }
```

The generated process has sensitivity `posedge fast_clk or posedge work_reset`
and gives reset priority. Assertion resets the registers even while the clock
is stopped. Held reset prevents normal work; deassertion alone does not advance
state. Negative-edge registers use `negedge fast_clk or posedge work_reset`.
Domains without a reset handler retain their synchronous behavior.

A handler must reset every register owned by its clock/edge to a constant.
Use `reg.clr()`, `reg.set(constant)`, or `reg._next = constant`; the existing
strobe commits next-state assignments. Child reset handlers must be called
explicitly from the parent. The converter rejects missing or conditional reset
values, another domain's registers, data-dependent reset values, and memory
access inside reset handlers. Reset does not clear RAM. While a domain's reset
is asserted, its normal memory writes are disabled.

For native graph simulation, call `step()` for reset changes too, even if clock
levels did not change. `eval()` only evaluates combinational outputs. In a direct
C++ testbench, invoke each applicable reset handler and strobe on assertion, and
instead of normal work on that domain's edges while reset is held. Neither flow
models analog recovery/removal timing; synchronize reset release where required.
This synthesis path currently supports constant, active-high resets on named
clock processes, not arbitrary reset expressions or multiple reset signals.

## Regression Tests

Enable `CPPHDL_BUILD_TESTS` and `CPPHDL_BUILD_SYNTH_TESTS`. Set
`CPPHDL_SYNTH_VERILATOR` to the Verilator executable if it is not on `PATH`.

```sh
cmake --build build --target cpphdl synth_graph_checks synth_mapping_checks synth_schedule_checks synth_retiming_checks synth_stream_checks
ctest --test-dir build --output-on-failure -R '^synth_'
```

`synth/tests/math.cpp` is both the RTL source and its testbench. It checks all
65,536 pairs of 8-bit operands, with unsigned arithmetic, signed multiplication,
division, remainder and comparison, arithmetic shifts, saturation, integer square
root, population count, 64-bit operations, and an enabled accumulator with
synchronous reset. Nested handwritten records exercise native field offsets,
padding, constant initialization and partial updates without generated layout metadata.
Independent arithmetic expectations check the ordinary C++ model. The native
graph and Verilated **mapped gates**, not just the intermediate RTL, are compared
with that model. `synth_graph_checks` checks explicit rejection of unsupported
effects, malformed clock/memory contracts, undriven signals, and combinational cycles.

`synth_mapping_checks` tests bit-level arithmetic expansion, narrowing, signed
division and memory address bounds. `synth_schedule_checks` checks loop clock
boundaries, initialization and response handshakes. The HLS integration tests
compare original RTL and mapped gates cycle-for-cycle, then check the retimed
results under backpressure and reset. `synth_hls_hft` runs the complete streaming
packet testbench against baseline gates, then requires full-design 315 MHz
retiming with II=1. Explicit RTL stages split the collector-to-decision boundary
outside the HLS regions. The current full-design estimate is 3.15 ns, with
12/6/30 clocks of latency in the three HLS regions. See
[the HFT results](../hls/examples/net/README.md) for the manual stages and
functional checks; estimated timing does not establish physical timing closure.

`synth/tests/pipeline.cpp` adds a two-stage parent/child design with 4,096 cycles
of stalls and resets. It checks simultaneous state updates and unchanged latency.
The two `synth/tests/multiclock/` examples additionally compare C++, native graph,
and mapped gates over 11,999 and 17,999 timestamps. They cover coincident and
noncoincident edges, both edge polarities, synchronizer latency, stopped clocks,
independent resets/enables, and hierarchy. Tests inspect mapped flip-flop clock
connections and reject malformed clock declarations and ownership violations.
`multiclock/memory.cpp` adds independent-clock RAM traffic, coincident read/write
collisions, write priority, pending forwarding and disabled/stopped ports.
`multiclock/async_reset.cpp` checks reset pulses between edges, stopped clocks,
nonzero reset values, both clock edges and child handlers.
`multiclock/memory_async_reset.cpp` combines both features and checks that RAM
survives reset. All three compare ordinary C++, native graph execution, and
Verilated gate-level output; negative cases check ownership and reset diagnostics.
`tests/graph/SignedArithmetic.cc` separately checks 225 signed 64-bit boundary
pairs against the native graph and constant folding, including the graph's
defined zero-divisor and minimum-integer overflow results.

Future pipeline tests must compare the scheduled C++ model and gate-level model
cycle by cycle. The original unscheduled C++ model remains a transaction-level
reference when allowed latency changes.

## Delay-Based Retiming

The synthesis driver supports cycle-preserving register movement and
latency-changing feed-forward pipelining and ready/commit scheduling for
register-only feedback. `--synth` also imports the actual Clocked FSM and
supports atomic memory transactions and output bundles. Pure functions in the
ordinary C++ graph flow can carry a checked `CPPHDL_ONE_CLOCK`
boundary. See [Retiming](retiming.md)
for the delay model, command-line options, module annotations, memory rules,
and regression tests. Both frontends use these shared graph passes; their
external timing contracts must be respected when integrating the result.

### Streaming Regions

`ClockedPipeline` exports an untimed method transition as a streaming region
alongside its initial physical pipeline. The shared graph records its input
and output bindings, owned registers, constant state initialization, and HLS
stage boundaries. This metadata survives serialization and gate expansion;
the synthesis pass does not need to reparse C++ or infer intent from hold muxes.

HLS distributes operations by dependency depth. Synthesis
`fit_pipeline_retiming` expands oversized operators and adds delay-based cuts
while preserving those boundaries, operand alignment and ready/valid flow.
It rebuilds the region from the logical transition, retaining II=1 instead of
using the ordinary feedback transaction scheduler. Each invocation snapshots
committed state at admission and commits its candidate state at the final
pipeline stage. Retiming delays that feedback; it need not preserve sequential
C++ decisions. Global backpressure freezes updates, bubbles do not commit,
and synchronous reset discards all in-flight calls.

The native `ClockedPipeline<T, N>` wrapper models N advancing edges. A retimed
reference must instead use the reported streaming latency. Stateful pipeline
tests compare this contract in native C++, generated RTL and retimed mapped
gates, including nonzero reset state and simultaneous admission/commit.
