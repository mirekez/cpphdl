# HLS Above CppHDL

## Design Boundary

HLS is an experimental superstructure over CppHDL, not a change to the RTL
contract. Keep HLS-specific source, scheduling, allocation policy, and tests
in `hls/`. Reuse the existing Clang AST and Sema specialization machinery so
improvements to normal CppHDL parsing remain available to HLS.

**Translate actual `std::` container methods. Do not implement substitute
containers or select replacement algorithms by container name.** For example,
synthesizing `std::map` must follow its tree operations, not replace them with
a linear key/value table.

The LLVM transaction-kernel experiment has been removed. HLS no longer uses
Clang CodeGen, `kernel-source.ll`, `kernel-lowered.ll`, or an external C++
compiler to synthesize a method. Native test executables and Verilator's
generated C++ still need a C++ compiler, as usual.

## Clocked Class Instantiation

An ordinary class can be a clocked member of an RTL module:

```cpp
#include "hls/Clocked.h"
#include <vector>

struct Samples {
    std::vector<uint32_t> values;

    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 0) {
            values.push_back(value);
            return values.size();
        }
        uint64_t sum = 0;
        for (uint32_t i = 0; i < values.size(); ++i) sum += values[i];
        return sum;
    }
};

class Top : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<Samples> worker;
    // Expose and connect worker's command/response ports in _assign().
    void _work(bool reset) { worker._work(reset); }
    void _strobe() { worker._strobe(); }
};
```

`Clocked<T>` carries the `CPPHDL_HLS_CLOCKED` annotation. It is the explicit
opt-in to AST scheduling; `T` itself does not need clock methods, registers,
ports, or a Module base. See the fully connected parent modules in
[ClockedArray.cpp](tests/std/ClockedArray.cpp),
[ClockedVector.cpp](tests/std/ClockedVector.cpp), and
[ClockedMap.cpp](tests/std/ClockedMap.cpp).

The initial entry contract is deliberately small:
`uint64_t command(uint32_t operation, uint32_t index, uint32_t value)`.
The wrapper exposes these arguments, command valid/ready, result,
response valid/ready, and a fault code. It accepts one outstanding command.
Arguments are saved on acceptance. The result remains stable until consumed.
Reset cancels the current operation and reconstructs the object before ready
is asserted again.

```sh
build/cpphdl --hls --generated-dir build/clocked-vector \
    hls/tests/std/ClockedVector.cpp -- -Iinclude -stdlib=libc++ -fno-exceptions
cmake --build build --target hls_tests
ctest --test-dir build -R '^hls_clocked_' --output-on-failure
```

Standard-container examples and their analysis helpers live in `hls/tests/std/`.
The Verilator tests generate RTL under
`build/hls/tests/std/hls_clocked_<name>-rtl/generated/`, where `<name>` is
`Array`, `Vector`, or `Map`. General clocked HLS tests (bindings, recursion,
rejection, and code reuse), the shared clocked test harness, and storage-helper
tests live in `hls/tests/`.

## Source-Based Scheduling

`AstClocked.cpp` follows concrete methods reachable from `T::command()` and
the object's initialization. Missing template bodies are instantiated with
Sema. Calls, fields, offsets, casts, constructors, and control flow come from
those declarations, including declarations in the installed standard library.
There is no separate C++ compilation or LLVM instruction stream.

Each statically elaborated call has source-named bindings, identified by its
method, call site, and bounded recursion depth. Scalar parameters, locals, and
results become directly assigned logic values. A backwards dataflow analysis
adds registers only for values needed after a loop's clock boundary.
Independent scalar values become individual typed signals, not slices of a
single large packed bit vector. Only values live across clocks are grouped in
the saved-state record.
`this` binds to the selected source object; reference parameters and local
references bind to their targets instead of storing another pointer copy.
When evaluating later arguments could change a selected pointer, its value is
captured before those arguments, preserving C++ evaluation order.

An actual address use, such as `&local` passed to a pointer-taking function,
requires addressable storage. Aggregate objects and container allocations also
retain their source layout and addresses. These are source data, not a CPU
stack. There are no runtime call pushes, pops, return addresses, or stack
pointer. The conversion-time scope records only map AST declarations to their
statically elaborated values. The generated `phase_reg` selects the next loop
phase; it does not execute instructions.

The preprocessor builds a graph of source statements and emits named, reusable
SV functions and an FSM continuation register:

- Straight-line operations, branches, and nested calls run in the same clock.
- Entering a loop saves its continuation. Loop iterations are clock steps;
  after-loop code can run with the final condition check.
- A loop inside a called method suspends the caller too. Its locals and
  references remain available when execution resumes.
- Nested loops have separate continuations. `break` and `continue` route to
  the appropriate exit or next iteration, including local cleanup.
- A statically single-pass `do { ... } while (false)` is not a clock boundary.
  This matters for standard-library assertion macros.

The generated functions form an acyclic call sequence within each clock; they do
not recursively call one another. Comments identify instantiated methods and
their source locations. The normal converter still handles the containing
RTL module, its child instance, and port connections.

Generic Clang builtins need explicit lowering: reference casts such as
`std::forward`, checked integer arithmetic, address-of, allocation, and byte
copy/move/set operations. These are not implementations of container methods.
Library assertions and allocation failures produce a fault response.

### Real `std::map` Methods

The map example stores `std::map<uint32_t, uint32_t>` directly. Its command
method uses `operator[]`, `find`, iterators, `erase`, `clear`, and `size`.
The generated hardware follows libc++'s red-black tree, including node
allocation, parent/left/right links, color changes, and rotations. It does
not substitute a table scan or another container implementation.

HLS examples use Clang with **libc++**, whose tree algorithm bodies are in
headers. No bundled library implementation or `tree.cc` include is needed.
Native reference tests and Verilator's C++ testbenches also use libc++.
The converter itself and non-HLS targets keep their existing standard library.
The current regression environment uses libc++ 21.1.3. Other library versions
may introduce additional AST constructs and need their own validation.

Install Clang, libc++ development headers, and libc++abi. CMake checks that
the HLS compiler can compile, link, and run a vector/map program. Select another
Clang installation with `-DHLS_CXX=/path/to/clang++`; a nonstandard library
directory can be supplied with `-DHLS_LIBCXX_LIBRARY_DIR=/path/to/lib`.
The tests pass that compiler's header search paths explicitly to CppHDL so
the converter's build-time libstdc++ headers cannot enter the HLS source AST.
Projects not building HLS examples can set `-DCPPHDL_BUILD_HLS_TESTS=OFF`
without disabling the ordinary CppHDL tests.

```sh
build/cpphdl --hls --generated-dir build/clocked-map \
    hls/tests/std/ClockedMap.cpp -- -Iinclude -stdlib=libc++ -fno-exceptions
cmake --build build --target hls_clocked_Map
ctest --test-dir build -R '^hls_clocked_Map_' --output-on-failure
```

Look in `cpphdl_hls_ClockedMapMethods_R8.sv` for functions whose names retain
`__tree_min`, `__tree_next_iter`, `__tree_balance_after_insert`,
`__tree_remove`, `__tree_left_rotate`, and `__tree_right_rotate`.
Node accesses retain `__left_`, `__right_`, `__parent_`, and `__is_black_`.
Generated functions use `hls_<qualified_method>__shared_N`. A call without clock
suspension becomes one reusable static function, including its branches and
nested same-clock calls. A helper's branch and field read stay together;
it does not have separate scheduler functions for copying its return value.
These whole-method functions have no `active`, `phase`, or
continuation arguments. Identity returns such as `return this` bind directly.

The `Combinational.cpp` pass joins branch arms at their common continuation,
propagates same-width scalar copies while their sources remain unchanged, and
removes unused copies. It preserves saved values when their source changes,
branches disagree, or a nested call can modify reference arguments. It also
merges adjacent single-predecessor scheduler blocks without crossing a clock
boundary. Methods containing loops still have scheduled continuations.
Equivalent method bodies and scheduled blocks share code, with source object
addresses and scalar values supplied as arguments. Only scheduled continuations
also receive successor states. Call-site and recursion suffixes remain on the
caller's storage, not on copies of the function body. Ordinary calls do not add
a clock; loop boundaries do.
Parameters and locals retain their source names in address constants and
individual combinational signals. Unused temporary declarations are omitted;
they are not collected into an artificial giant struct. Field accesses use
named offset constants containing `__tree_node_base` and `__left_`, rather than unexplained
numeric offsets. These names are used by executable RTL, not only comments.
Storage remains a byte-addressed register array so references and pointers can
alias correctly; the named address constants do not allocate extra registers.
Loads and stores call shared width-specific functions, for example
`hls_storage_write_64(storage, address, value, fault)`. Byte assembly, byte
writes, and bounds checks are emitted once per width instead of at every
access. Fixed byte-lane loops inside these functions execute combinationally;
they do not introduce clocks. Write arguments snapshot the address and value
before changing storage, including when source and destination overlap.

The sharing pass only parameterizes symbols registered by the scheduler. It
compares operations, literal values, argument widths, writable/input modes,
symbol alias relationships, branch conditions, and clock boundaries. Different
template specializations share code only when their lowered operations match.
Fields and constants retain their layout-specific meanings. Each distinct
temporary and writable state, including backing storage, are passed using
`inout`; read-only arguments use `input`. Generated functions have static
lifetime, with no `automatic` declarations or `ref` arguments. They contain
no recursive SV calls and execute without suspension. Values that cross a
clock boundary live in named module registers, not persistent function locals.
Independent calls keep independent saved values, including calls that
are suspended inside loops. This is emitted-code reuse, not a new clocked
resource-sharing schedule or a guarantee of reduced synthesized area.

Header-defined container algorithms are the supported baseline. The tested
`std::vector` operations need only the standard headers plus the scheduler's
allocation and memory-operation lowering. The libc++ `std::map` example also
uses only installed headers. Bounded aligned allocation and C++17 returned-object
construction preserve the library's temporary node-owner semantics.

Tree clearing recursively visits subtrees. `Clocked<MapMethods, 8>` explicitly
permits up to eight simultaneously active calls of the same concrete function.
The scheduler unfolds these calls into separately named depth-specific values and nonrecursive
SV blocks. A call beyond the bound produces `fault_out == 5`, not a successful
partial result. The fault persists until reset. The bound must include the
terminal/base-case invocation. It is not a bound on the number of map elements.
`Clocked<T>` defaults to rejecting recursion; explicit bounds may be 1 through
16. Branching recursion can still produce excessively large generated code.
Different bounds produce distinct module names, such as `_R2` and `_R4`, so
both can appear in the same parent. The native transaction reference does not
enforce this hardware bound.

Clocked RTL tests use Verilator's `-fno-inline-funcs` option to retain shared
function bodies during model compilation. The functions pass their state
explicitly. This does not change
the clock schedule or turn off correctness checks. A passing simulation still does not
establish useful synthesis area or timing for this experimental backend.

## Memory Contract

Two choices must ultimately be explicit:

1. Where container state lives: registers, `memory<>`, or external SRAM.
2. The memory port's capacity, width, latency, number of ports, and protocol.

Dynamically allocated container objects, such as
`std::vector<uint32_t>* values = new std::vector<uint32_t>;`, are not
supported as an HLS usage contract. Container examples use direct members;
their tests do not include dynamically allocated container-object variants.
The working clocked Vector and Map tests do exercise allocation of elements
and nodes inside those direct members.

The future intended default is **local/member-created objects use registers;
dynamically allocated storage uses SRAM/ports**. A local `std::vector`
still dynamically allocates its elements, so object placement alone cannot
select the policy for all reachable storage. The container object and its
allocated nodes/elements need separate policies.

**The current AST prototype is register-backed only.** It maps both objects
and virtual heap addresses into a byte-indexed register array, preserving the
Clang object-layout offsets. This validates pointer
and scheduling semantics, not SRAM implementation or achievable timing.
Only addressable source objects occupy the byte array. Their addresses are
fixed at conversion, without reusing storage on function return. Constant
objects are limited to 4 KiB; unused constant capacity is not allocated in RTL.
The prototype heap is 4096 bytes, with a minimum 16-byte alignment and support
for explicit power-of-two alignment up to 4096 bytes. It is monotonic:
delete is a no-op, storage is reclaimed on
reset. Bounds failures are reported; allocation reuse is not yet implemented.
Programs must bound total allocations between resets, not just live size.

`Allocator.h` retains the earlier bounded allocator policy for future AST
integration; the clocked vector example uses ordinary `std::allocator`.
`Memory.h` and `Sram.cpp` retain the independently tested `HlsMemoryIf`
protocol. Their tests do **not** prove that this AST scheduler supports SRAM.
The next memory backend should suspend on a real memory wait and use this
whole interface. A later arbiter can let several containers share an arena.
Do not silently substitute per-access clocking for the register backend.

## Tests And Limits

Each working container has its own C++ class and native/Verilator tests.
The Verilator flow converts the source, checks for instantiated library
methods and loop continuations, and builds the connected parent module.
It also verifies that no LLVM `.ll` files were generated.

- Array: indexing, fill, two loop-containing calls in an expression,
  saved arguments/references, nested for/while/do loops, local destructors,
  delegating constructors, nonzero-offset downcasts, constant objects (including
  constructors that distinguish constant evaluation from runtime),
  out-of-line definitions with renamed parameters, and void return expressions.
- Vector: push/growth/relocation, indexing, fill, erase, clear, and reuse of
  existing capacity.
- Map: ordered traversal checksums, ascending/descending/shuffled insertion,
  duplicate updates, missing lookups, size, leaf/two-child deletion, recursive
  clear, reinsertion, and deterministic mixed operations.
- Containers: changed input pins during execution, one-clock straight-line calls,
  loop suspension, response backpressure, and reset cancellation/restart.
- Bounded recursion: depth-specific values across loops, boundary-depth success,
  overflow fault/backpressure, and reset recovery.
- Reuse: repeated calls with different arguments, aliased references, saved
  results across loop-containing calls, 8-bit/32-bit template instances,
  16-bit/64-bit locals, and 128-bit aggregate copies.
  Structural checks require one `mix` body and two width-specific `widen` bodies.
- Bindings: explicit `this`, methods returning `*this`, two independent objects,
  changing the receiver pointer while evaluating arguments, reference aliasing,
  address-taken scalar updates, object methods that suspend in loops, inherited
  methods at nonzero base offsets, aggregate base/member initialization, and
  const receiver methods. Additional cases cover anonymous members and reference
  initialization, C++17 returned-object elision, named and alternate return paths,
  destructor timing, and aligned allocation followed by placement construction.
- Library selection: native compile/link/run with libc++, and converter header
  selection through both `-stdlib=libc++` and explicit `-nostdinc++` paths.
  A compile-time guard rejects mixed libc++/libstdc++ headers.
- Direct values: unaddressed scalar locals must not have byte-memory addresses;
  only values live across clock boundaries have register assignments. Generated
  code must not contain saved `this` slots or unresolved source access handles.
- Storage helpers: generated read/write functions are tested directly for
  8/16/32/64/128-bit accesses, little-endian byte order, unaligned addresses,
  overlapping copies, address snapshotting, bounds, and fault preservation.
  Invalid writes must leave every byte unchanged. Structural checks forbid
  raw byte accesses in scheduled method bodies.
- Shared-block unit tests: equivalence under symbol renaming, successor-state
  parameters, and rejection of unsafe merges across differing widths, modes,
  aliases, literals, branch conditions, and clock boundaries. Identifier
  substrings, strings, and comments must not be rewritten as parameters.
- Same-clock methods: the map-style `_S_left`/`return this` pattern, early returns,
  nested calls, source mutation after a copy, and reference mutation. Structural
  checks require whole methods without scheduler arguments and no standalone
  identity-return function. Graph tests preserve branch joins and clock boundaries.
- Rejection tests: unavailable function bodies, recursion without a bound, static locals,
  volatile access, floating-point expressions, indirect calls, missing
  `--hls`, and the removed kernel option.

**The native wrapper is a transaction reference, not a cycle-accurate HLS
simulator.** It invokes the original C++ command atomically. Tests compare
transaction results against RTL and check RTL timing separately. Generating
the same scheduled execution for native C++ is still needed for cycle-accurate
integration with other modules.

This is not general STL synthesis yet. Notable limits include SRAM scheduling,
allocator reuse, arbitrary entry signatures, concurrent commands, named/CDC
clocks, exceptions, virtual/indirect calls, nontrivial temporary cleanup,
bit-field object layouts, and switch/range-for lowering.
Global constant objects containing mutable fields are rejected. Non-null
pointer/reference values in their initializers are also rejected until their
address relocations can be represented correctly.
Unsupported constructs must produce diagnostics, not placeholder RTL.
Loops are not automatically proven terminating and have no hardware watchdog.
Keeping a long acyclic call chain in one clock can create a long critical path.
The flat register representation is also expensive for synthesis optimization.
Passing simulation is not an area or timing result. Earlier byte-only lowering
was expensive in Yosys; synthesis cost has not been established for the direct
value lowering. Aggregate-field promotion, simpler straight-line functions,
and further storage partitioning remain future improvements.

The earlier direct-container discovery tests for vector, list, map, multimap,
and unordered_map remain **analysis-only, expected-rejection tests**.
`StdContainers.cpp` records reachable methods and layouts in
`hls-analysis.json`; it does not replace their algorithms. These tests also use
libc++ headers, but their unwrapped container members do not opt into the
`Clocked<T>` scheduler and are still rejected for RTL emission.

The separate scalar recursion pass in `HLS.cpp` remains available for ordinary
CppHDL methods: bounded, numbered functions with an explicit terminal method.
It is independent of the clocked-object scheduler's bounded source-call expansion;
neither mechanism emits recursive SystemVerilog or an unbounded runtime stack.
