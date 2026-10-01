# Retiming over CppHDL Synthesis

Retiming operates on the shared operation graph in `synth/`. It does not use HLS
or modify the C++ source. Both the original and retimed graph can be simulated
as native C++, and the synthesis driver maps the result to gate-level Verilog.

There are two different contracts:

| Mode | Existing clock-cycle behaviour | Register count | Purpose |
| --- | --- | --- | --- |
| `keep_behaviour_retiming` | Preserved | May change when banks split or merge | Balance existing stages |
| `fit_pipeline_retiming` | Additional latency | Increases | Pipeline feed-forward processing |

Retiming is opt-in. Without a command-line rule or a source annotation, synthesis
reports estimated timing but does not move or insert registers.

## Delay Estimates

`synth/timing.h` defines `DelayModel`; `synth/timing.cpp` evaluates it. All values
are nanoseconds. Defaults are intentionally simple:

| Component | Estimate |
| --- | --- |
| AND / OR | 0.05 |
| XOR | 0.10 |
| Multiplexer | 0.10 |
| Register clock-to-Q / setup | 0.10 / 0.05 |
| Add, subtract, ordered compare | 0.05 + 0.06 per operand bit |
| Equality / reduction | Gate-tree estimate proportional to log2(width) |
| Variable shift | Multiplexer-tree estimate proportional to log2(width) |
| Multiply | log2(width) estimated adder levels |
| Divide / remainder | width estimated adder levels |
| Memory read | 0.50 + 0.08 * ceil(log2(depth)) |

Constant shifts and bit rearrangement are wiring. Memory write address, data,
and enable paths terminate at a setup-time boundary. Registered memory reads
include both the memory delay and the destination register's setup requirement.
The analyser checks input-to-register, register-to-register, register-to-output,
and memory-write paths. It accounts for mux/select paths, not only data inputs.

Use `--delay-scale 1.5` to scale every estimate, including register overhead.
C++ users of the synthesis API can supply a custom `DelayModel`.

These are **estimates, not static timing analysis**. They do not model a target
library, placement, routing, fanout, clock skew, hold time, or reset recovery and
removal. C++ integer promotions can make estimates conservative. Frequencies
declared by clock options do not select a retiming target automatically: specify
the period explicitly. Final acceptance still requires technology mapping and
timing analysis with the actual constraints.

## Apply a Rule

To pipeline the whole design toward a 2.5 ns estimated period:

```sh
build/cpphdl --synth --top cpphdl_top --module SynthRetiming \
  --retiming fit_pipeline_retiming --clock-period-ns 2.5 \
  --output /tmp/retimed synth/tests/retiming/logic_retiming.cpp
```

To limit the rule to one C++ module instance and its descendants:

```sh
build/cpphdl --synth --top cpphdl_top --module SynthRetiming \
  --retiming keep_behaviour_retiming --clock-period-ns 2.5 \
  --retime-module cpphdl_top.stage \
  --output /tmp/retimed-stage synth/tests/retiming/logic_retiming.cpp
```

The selector is an **instance path**, not a class name. A misspelled or empty
region is an error. Source annotations apply a rule to each instance of a class:

```cpp
class [[clang::annotate("CPPHDL_RETIMING=fit_pipeline_retiming:2.5")]]
ArithmeticChain : public cpphdl::Module {
    // Ordinary ports, registers, _work and _strobe methods.
};
```

The format is `CPPHDL_RETIMING=mode:period_ns`. Protect the attribute with
`#ifdef __clang__` when another native compiler warns about unknown attributes.
An explicit CLI retiming rule overrides source rules for that invocation.
Multiple disjoint annotated instances are allowed; overlapping rules are
rejected instead of retiming the same region twice. State names and operation
scope metadata survive lowering; common-expression merging does not cross scope
boundaries.

## Keep Behaviour

This mode searches for moves across pure combinational operations:

- Forward: compute an operation before the register boundary, replacing banks
  at its operands with a bank at its result where those operand banks are unused.
- Backward: move a result bank to the operation's operands and compute the
  operation after those banks.

Each move preserves the number of clock boundaries along the affected paths.
Only moves that improve the selected region's worst estimated path are accepted.
Unrelated consumers keep the storage they still need. Native initial values are
transformed too; backward moves requiring an unsupported initial-value inverse
are not attempted. Synthesized power-up state remains unspecified unless the
design resets it.

Clock, edge, reset, and memory boundaries constrain legal moves. Forward moves
require compatible clock/edge/reset domains and transform constant asynchronous
reset values. Backward movement across asynchronous-reset registers is currently
disabled. Registers do not move through a RAM access or across clock domains.

This is a bounded greedy search, not a globally optimal retiming solver. A target
may be impossible with the existing latency, or this search may not find it.
The driver still emits the improved design and sets `target_met` to false.
It must not insert extra cycles to disguise that failure.

## Fit a Pipeline

This mode traverses the feed-forward dependency graph and inserts register banks
when another operation would exceed the available stage delay. It adds matching
delays to the other operands of a join, including mux controls. Existing
feed-forward register stages retain their original relative cycle boundaries.
Externally visible selected register outputs are balanced to the same added
latency, so a tag carried alongside data stays aligned. Balancing output copies
does not add delay to an internal register's other consumers.

The interface must accept one transaction every clock. An enable that means
"hold this register" introduces feedback and is not supported by this mode.
Represent bubbles as data accompanied by a validity field where appropriate;
the retimer does not invent ready/valid protocols or backpressure.

Inserted stages preserve a common constant synchronous reset, or the selected
domain's constant asynchronous reset. Reset control is not delayed with data.
Pipeline contents are invalid during filling after reset; discard the reported
added latency before comparing transactions. The source C++ model keeps its
original latency. Use the generated native graph to simulate the transformed
pipeline, or compare its outputs against delayed source-model expectations.

Current rejection conditions include:

- Register feedback, including accumulator and hold-enable loops.
- Multiple clock/edge/reset domains within one fit rule, or state dependencies
  crossing clocks or edges. Use separate rules for independent domains.
- Mixed synchronous-reset contracts within the selected region.
- Combinational top-level output boundaries; expose registered outputs first.
- A register whose added latency would change a memory write transaction.
- A selected register driving an unselected register. Include the downstream
  pipeline in the rule instead of changing only one side of that boundary.
- A memory address path that would need to be delayed.
- A single indivisible operation that exceeds the target by itself.

Multipliers and dividers remain atomic graph operations. Inserting registers
around a too-slow divider cannot make the divider faster; decompose it into a
multi-stage implementation first. If the transformed region still exceeds its
target, fit mode fails and no successful gate netlist is reported.

For a scoped fit rule, callers must account for the changed module latency. This
is not a protocol-preserving transformation across arbitrary module boundaries.
Expose registered module outputs at the synthesis boundary, or include their
downstream consumers in the selected region.

## Memory Rules

RAM is state, not just another combinational expression. A read delayed by one
clock might observe a newer write, even if its address is identical. Retiming
therefore leaves the read transaction at its original clock and can register
the value already read. Arithmetic and sidebands downstream can then be pipelined
together. Write address, data, enable, and clock are left unchanged.

The RAM regression writes and reads the same address on the same edge, uses
changing input data and biases, and checks the old-data read contract. A separate
latency-aware scoreboard prevents an accidentally delayed memory access from
passing as a harmless output delay. This digital collision rule is not a promise
about physical asynchronous RAM collisions.

## Preserve a Module for Technology Mapping

Annotate a combinational module with its estimated **input-to-output delay in
nanoseconds**. The synthesis retimer treats the whole module as one indivisible
box, using this delay instead of adding up its internal operation estimates:

```cpp
class [[clang::annotate("CPPHDL_KEEP_BOX=2.1")]] MultiplyBox
    : public cpphdl::Module {
public:
    _PORT(uint8_t) a_in;
    _PORT(uint8_t) b_in;
    _PORT(uint16_t) product_out = _ASSIGN(product_comb_func());

    uint16_t product_comb_func() {
        return uint16_t(a_in()) * uint16_t(b_in());
    }
};
```

This number is propagation delay, **not clock-cycle latency**. This box has no
internal registers and still computes a combinational result. The declaration
does not add a register or turn a multi-cycle operation into a single-cycle one.
Use the normal retiming rule on its enclosing module or on the whole design.

- `keep_behaviour_retiming` may move boundaries in surrounding logic but does
  not move a register through the box or modify its internal operations.
- `fit_pipeline_retiming` may insert aligned register banks at the box inputs
  and in logic after its outputs. It cannot split the box. All box inputs are
  aligned together, including control inputs; outputs share the declared delay.
- A 2.1 ns box leaves only the remaining stage budget for surrounding logic,
  after clock-to-Q, setup and any reset mux overhead. A smaller declared delay
  allows more surrounding logic in the same stage. An oversized box causes an
  explicit `indivisible keep box exceeds target period` error in fit mode.

The current contract supports **pure combinational modules**. Registers, RAM,
host effects, nested/overlapping keep boxes, and combinational paths which leave
and re-enter the same box are rejected. Registered or multi-cycle macros need a
separate sequential timing contract; do not describe those with this annotation.
Ordinary frontend lowering and constant simplification still apply. This keeps
the live operation-level implementation, not the original source text or port
spelling; boundaries are emitted as packed input/output buses.

`operations.v` includes the preserved module implementation and its instance.
During generic Yosys gate mapping, the driver temporarily hides only those
explicitly declared implementations. It restores their real bodies in `gates.v`
afterwards and also writes `keep_boxes.v`. Consequently `gates.v` is self-contained
and simulatable: surrounding logic is gate-level, while kept modules retain
operations such as `*` for subsequent DSP or ASIC technology mapping. Compile
`gates.v` alone; do not also compile `keep_boxes.v`, which would duplicate module
definitions. No unresolved blackbox is substituted for simulation.

`timing.json` lists each live kept instance, its generated module name and
declared delay. `--delay-scale` scales built-in cell estimates only; an explicitly
declared box delay stays unchanged. These numbers are user-provided timing
contracts, not timing measurements or guarantees of DSP inference.

`synth/tests/retiming/keep_box.cpp` compares C++, the retimed native graph and
Verilator over 4,096 transactions with resets and tags. It tests both retiming
modes and two declared box delays. Structural checks require the multiplication
and internal addition to remain in one unregistered module, keep parent binding
arithmetic outside it, and retain exactly one instance through generic mapping.

## Reports and Tests

The output directory contains:

- `timing.json`: per-rule before/after delay, target status, moved boundaries,
  inserted register bits, and added latency.
- `retimed_graph.cc`: the transformed shared graph, also usable by the native
  graph emitter.
- `operations.v`: operation-level Verilog after retiming.
- `gates.v` and `gates.json`: Yosys-mapped generic gates.
- Logs and `manifest.json`, including the timing report.

The register-bit insertion count describes the transformation, not the final
mapped flip-flop count; synthesis can remove constant and redundant bits.

```sh
cmake --build build --target cpphdl synth_retiming_checks
ctest --test-dir build --output-on-failure -R '^synth_.*retiming'
```

`logic_retiming.cpp` checks arithmetic, tags, nonzero reset values, exposed
intermediate registers, and module selection through annotations and CLI rules.
`ram_retiming.cpp` checks RAM ordering and downstream
arithmetic. Both modes run against native C++ and Verilated gate-level output.
Keep-mode tests additionally run an eight-cycle Yosys SAT equivalence check with
zero-initialized state and reset on the first cycle. This bounded check supplements
the runtime regressions; it is not an unbounded proof for every possible design.
`Checks.cpp` exercises delay scaling, impossible targets, invalid scopes,
feedback, memory-write barriers, clock-domain rejection, initial-value
preservation, and timing after bit-level dependency splitting.
