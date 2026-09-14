# HLS Using Standard C++ Containers

## Non-Negotiable Boundary

**HLS must synthesize the methods of actual `std::` containers. Do not
implement replacement containers in `hls/`.** A linear key/value table is
not an implementation of the library's map methods, and a kind-number
dispatch selecting such a table is not STL synthesis.

The previous `HlsContainer` and `HlsSelection` experiment has been removed.
Its passing reference tests demonstrated a limited command protocol, not
translation of STL algorithms. Those results are not evidence for this design.

All HLS-specific compiler code, allocation policies, scheduling, and tests
belong in `hls/`. CppHDL remains the RTL layer: modules, ports, interfaces,
registers, clock methods, strobes, and memory commit. HLS must reuse the
existing Clang parser and Sema specialization machinery rather than fork the
C++ parser or change normal CppHDL semantics. The experimental transaction
backend uses Clang CodeGen and LLVM's control-flow IR, with its scheduler and
SV emitter isolated in `hls/Kernel.cpp`.

## Current Implementation

The five separate test modules now contain real standard containers:
[Vector.cpp](tests/Vector.cpp), [List.cpp](tests/List.cpp),
[Map.cpp](tests/Map.cpp), [Multimap.cpp](tests/Multimap.cpp), and
[UnorderedMap.cpp](tests/UnorderedMap.cpp). Their `_work()` methods call
`push_back`, `find`, `emplace`, `erase`, `clear`, and `size` on those
objects. These calls are not confined to the software reference model.
The same class also exposes the command/response ports and strobes its
response registers.

Each test builds with an ordinary member object and with a member pointer
initialized using `new`. A capacity annotation records the intended bound:

```cpp
class MapTop : public cpphdl::Module
{
    // Ports precede private state in the complete example.
private:
    [[clang::annotate("CPPHDL_HLS_CAPACITY=8")]]
    std::map<uint32_t, uint32_t> values;
};
```

The test header guards the Clang annotation so native GCC C++17 builds remain
possible. The attribute records a synthesis contract; it does not change
the behavior or allocator of `std::map` in a native executable.

`StdContainers.cpp` runs before ordinary RTL collection when `--hls` is
enabled. It discovers direct standard-container members of concrete Module
classes, records the object policy and capacity, follows called functions,
and asks Sema for reachable template definitions where needed. It records
actual instantiated method bodies, call edges, source locations, record
fields, pointer fields, and base classes. It does not synthesize a replacement
algorithm based on the container's name.

For the installed libstdc++, map analysis exposes `_M_left`, `_M_right`,
`_M_parent`, and `_M_color`, and follows insertion and erase into the
RB-tree helpers. Some helpers, including `_Rb_tree_insert_and_rebalance`
and `_Rb_tree_rebalance_for_erase`, are declared in headers but defined
in the separately compiled standard library. Missing definitions are listed
in the report with their declaration locations.

**The generic container-discovery flow remains analysis-only.** Conversion exits with
an error before emitting SV and writes `hls-analysis.json` in the selected
generated directory. That file is an analysis report, not synthesizable IR.
It includes `status: unsupported_rtl` and explicit blockers. No tree, list,
hash table, or vector RTL is claimed by those analysis tests. The separate
bounded-vector flow below now generates and tests actual vector RTL.

## Working Bounded-Vector Backend

[BoundedVector.cpp](tests/BoundedVector.cpp) contains a Module-derived class
with an actual `std::vector<uint32_t, cpphdl::hls::Allocator<uint32_t, 512>>`.
Its methods call the library's `push_back`, `erase`, `clear`, indexing, and
`size`. [Allocator.h](Allocator.h) supplies only bounded allocation: a
first-fit pool with 16-byte blocks, deallocation/reuse, alignment checks, and
allocator rebinding which preserves arena identity. It does not implement
any vector operations. There are no modifications to namespace `std`.

```sh
build/cpphdl --hls --hls-kernel bounded_vector \
    --generated-dir build/hls-vector hls/tests/BoundedVector.cpp -- -Iinclude
```

The compiler uses the real instantiated method bodies, including vector's
reallocation and element relocation. It inlines reachable definitions and
expands compiler `memcpy`, `memmove`, and `memset` intrinsics into loops.
The scheduler groups acyclic paths into one clock step in `ScheduledKernel.sv`.
Dependent arithmetic, comparisons, branches, and inlined helper calls execute
together. Loop back-edges save a continuation for the next clock; external
SRAM accesses suspend until the memory protocol completes. Instructions do
not each consume a clock.
Only intermediate values needed after a loop boundary or SRAM wait are
retained in registers; values used entirely within the step stay combinational.
No container-kind dispatch, host callback, recursive SV function, or runtime
instruction ROM implements the container algorithm. `kernel-source.ll` and
`kernel-lowered.ll` retain the inputs to each stage for inspection.

The explicit entry ABI for this first backend is:

```cpp
extern "C" const uint64_t cpphdl_hls_state_bytes = sizeof(MyState);
extern "C" const uint64_t cpphdl_hls_state_align = alignof(MyState);
extern "C" uint64_t entry(MyState* self, uint32_t operation,
                          uint32_t index, uint32_t value);
```

Operation `UINT32_MAX` is reserved for placement construction of the state.
Reset starts that construction and keeps command-ready low until it finishes.
The controller latches all three arguments when accepting a command. It
holds its 64-bit result and response-valid until response-ready is sampled.
Only one transaction can execute at a time.

The generated module has two storage choices using the **same compiled
algorithm**:

- `SRAM=0`: the root object, pool, and fixed local allocations occupy a wide
  register. Loads and stores execute with the surrounding calculation in the
  same step. A load sees preceding stores in that step, including overlapping
  byte ranges. Writes commit at the clock edge.
- `SRAM=1`: the same byte-addressed layout uses the existing `HlsMemoryIf`
  64-bit word transport. Loads assemble bytes; partial stores use
  read-modify-write transactions. Unaligned and cross-word accesses are
  supported, with one request outstanding and an acknowledgement for writes.

Pointers become offsets in a bounded local address space, never host addresses.
Null is zero, the root starts at byte 16, and fixed locals follow the root.
Stored pointers, aliases, pointer differences, and one-past-end values retain
their meaning. Every dereference checks the complete access against the local
memory bounds before narrowing the hardware index to 32 bits. State alignment
must be a power of two no greater than 16 bytes. This is an
arena-range check, **not** per-allocation use-after-free or lifetime analysis.
Clang's current little-endian, 64-bit pointer layout is required. LLVM pointer
values retain that representation; actual RAM indexes are 32-bit.

Fault codes are 1 for allocation/length exhaustion, 2 for allocator validation,
3 for a memory-range violation, and 4 for an invalid control condition.
A fault holds the response and blocks further commands until reset. Completed
stores are not rolled back. Reset must reach both controller and responder
to cancel pending transactions; constructor writes reinitialize ownership,
not every payload byte. Resetting one controller on a future shared memory
bus will require draining or identifying its outstanding responses.

The external SRAM adapter remains serial and byte-oriented. Its waits cannot
be merged into combinational arithmetic: subsequent calculations need the
returned data or write acknowledgement. The register backend has no such
waits. Neither backend adds clock boundaries for an ordinary `if` or function
call. Grouping more computation reduces cycle count but can lengthen the
combinational critical path; it does not establish a higher clock frequency.

Native tests execute the original STL methods; RTL tests compare
transaction results against those native methods and another vector using
the ordinary allocator. There is not yet a cycle-accurate scheduled native
backend. Verilator passes both storage choices. When Yosys is available,
the RTL tests also run process lowering and structural checks. No
technology-mapped timing or area claim is made.

### Clocked Instances

The intended Module integration is an explicit **clocked instance** of a
member class. The class supplies ordinary C++ methods; the instance owns the
stored object, arguments, intermediate values, and continuation state needed
to execute those methods across clocks. This distinction belongs to the
instance so the same class can also be used as an ordinary C++ object.

A method call starts on command acceptance. Straight-line code runs together;
loop back-edges advance to another clock, and SRAM accesses wait for their
responses. Other module logic must not read a partially completed operation
as its final result. The instance publishes completion through its result
handshake and accepts at most one outstanding call. Its clock and reset come
from the owning RTL module.

This describes the integration contract, **not an implemented C++ wrapper or
attribute**. The current `--hls-kernel` ABI tests this execution model in a
standalone generated module. Automatic lowering of a member's method calls
and a cycle-accurate C++ work/strobe implementation remain to be added. Normal
CppHDL members and `_work()` semantics are unchanged.

### Shared SRAM Next

Keep `HlsMemoryIf` as the transport boundary. A later multiplexer will allow
several containers to use one SRAM arena. Address-space allocation, response
ownership, reset cancellation, and atomic allocation/read-modify-write
sequences must be specified before several controllers share allocator
metadata. Merely multiplexing requests does not make those sequences atomic.
Neither the interface multiplexer nor a shared allocator service is included
in this first backend.

### Explicit Limits

This mode currently requires one source file, an explicit transaction entry,
and an explicit bounded allocator. It does not automatically turn arbitrary `_work()` calls
into a scheduled command interface or select storage from stack/heap ownership.
The ordinary discovery flow still diagnoses those unsupported cases. The
generated controller uses `clk` and synchronous `reset`; named-clock and
JSON-output options are rejected in this mode.

Scalar integer operations up to 64 bits, fixed locals, branches, parallel PHI
copies, pointer arithmetic, and memory intrinsics are supported. Floating
point, indirect calls, dynamic stack allocations, atomics, volatile accesses,
unresolved external method definitions, and recursive call graphs are rejected.
General global-object initialization is not supported. There is no loop
watchdog or proof of termination; kernel authors must bound loops. The tested
vector operations are bounded by the pool capacity.

Map/list/hash-container RTL and STL recursive destruction are still future
work. The existing scalar recursion pass below is not applied to this new
scheduled LLVM path. Missing library implementations must be supplied from
matching library sources, never replaced with a different algorithm.

The discovery frontend currently inventories direct container members and direct
calls in their owning module's methods. It is not a complete object-alias or
reachability analysis. Calls hidden behind arbitrary user helpers, indirect
calls, function pointers, ownership through smart pointers, and containers
nested inside unrelated wrapper classes need further analysis. Reported
indirect calls and the method-count limit must not be interpreted as a
complete graph. None of those gaps are accepted for RTL emission.

## Storage Contract

The intended automatic storage policy distinguishes two questions:

1. **Where is the container object stored?** An ordinary member defaults to
   register-backed storage. An owned, dynamically allocated object defaults
   to an SRAM/raw-port backend.
2. **How does its allocator obtain storage?** Internal STL allocations inherit
   that object's backend; they must not independently select SRAM merely
   because a member `std::vector` allocates its buffer internally.

The current analyzer recognizes ownership only for a direct pointer member
with an in-class `new` initializer. Other pointer roots receive an
`unproven_object_ownership` diagnostic. This is deliberately narrower than
general C++ ownership inference.

A usable lowering contract must specify:

- Maximum live elements and total storage bytes, including nodes and metadata.
- Object construction, destruction, allocation failure, and reset behavior.
- Alignment, address units, data width, access width, and port count.
- Request acceptance, response latency, backpressure, and write acknowledgement.
- Whether multiple operations can access the same object concurrently.
- Maximum recursion and loop bounds, and observable failure on exhaustion.

A capacity of eight elements is not necessarily space for eight allocations:
vector growth can temporarily retain old and new buffers, and unordered maps
need buckets as well as nodes. Map nodes contain links and color metadata in
addition to the key/value pair. Allocator rebinding must retain the original
object's storage identity when the library requests a node type instead of
the public value type.

A custom bounded allocator is a possible attachment point because standard
containers already support allocator template parameters. An allocator or
a generic memory controller may live in `hls/`; container algorithms may
not. Supporting a custom allocator must not become a requirement to rewrite
insertion, search, balancing, hashing, or iteration.

## Method and Object Lowering

The broader implementation path is the following. Discovery/reporting exists
for all five containers. The explicit vector entry implements a first bounded
pointer, allocator, and scheduling path; automatic Module integration remains
to be done.

### 1. Collect the Actual Call Graph

Start from calls made by the RTL module on a particular container object.
Resolve its concrete element type, comparator, hasher, allocator, and method
specializations using Clang, including free functions and iterator methods.
Follow only reachable definitions rather than instantiating every member
of every library template.

Function identity must preserve overloads and specialization arguments.
Object identity must remain separate: two instances of the same container
may share a generated function body but must not share storage or allocator
state accidentally.

Library definitions absent from headers must be supplied from the matching
standard-library sources, or through an explicitly verified primitive
implementation. Linking a native `.so` does not provide synthesizable
function bodies. HLS must diagnose a missing algorithm body rather than
replace it with another algorithm. Definitions can already be supplied in the
same translation unit through the existing compiler arguments:

```sh
build/cpphdl --hls --generated-dir build/hls-map-analysis \
    hls/tests/Map.cpp -- -Iinclude -include /path/to/matching/tree.cc
```

An experiment with GCC 15.2.0's unmodified libstdc++ `tree.cc` successfully
collected both rotations, insertion rebalancing, and erase fixup. The source
is not copied into `hls/`, and no alternative tree algorithm is provided.
Its version and ABI must match the headers used to parse the design.
Cross-translation-unit linking of implementation bodies remains future work.

### 2. Lower Objects and Pointers, Not Container Names

Use the concrete C++ type layout to describe fields, base subobjects, nodes,
and embedded sentinels. Convert pointers to bounded references identifying
an allocation and an offset. Null, one-past-end pointers, iterator references,
parent links, and links to a header embedded in the root object must remain
distinct.

Native host addresses must never be copied into SV. Pointer comparisons,
casts, arithmetic, aliases, and object lifetimes need explicit lowering.
A reference returned by an iterator must still refer to the same stored
element when a later method writes through it.

Register-backed and SRAM-backed implementations use this same object graph.
The backend chooses where generic loads and stores go; it does not replace
the tree with a table or the hash buckets with a scan.

### 3. Attach Generic Allocation and Memory Primitives

Recognize allocator operations, construction/destruction, and compiler
memory intrinsics through explicit contracts. Lower bounded allocation,
free, load, store, and copy into reusable primitives.

For register storage, writes become next-state updates. For external memory,
operations use an interface derived from `cpphdl::Interface`, connected
as a whole with `assignIf`. Preserve the existing eight-signal transport:

| SV port at the requesting module | Direction | Meaning |
| --- | --- | --- |
| `memory_out__valid_out` | Output | Request valid |
| `memory_out__write_out` | Output | Write request |
| `memory_out__addr_out` | Output | Word address |
| `memory_out__data_out` | Output | Write data |
| `memory_out__ready_in` | Input | Request ready |
| `memory_out__valid_in` | Input | Response valid |
| `memory_out__data_in` | Input | Read/acknowledgement data |
| `memory_out__ready_out` | Output | Response ready |

[Memory.h](Memory.h) retains this interface and the generic `HlsSram`
responder. It contains no container implementation. Its transport is
currently one outstanding 64-bit word transaction with a 32-bit word index;
every accepted write also receives a completion. Requests and responses
remain stable under backpressure. The new transaction scheduler supplies
byte assembly and read-modify-write adaptation for partial-width accesses;
arbitrary STL layouts do not fit in one word.

SRAM testing remains separate from container analysis. A passing memory
primitive test does not mean that a container's loads and stores have been
lowered to that primitive.

### 4. Schedule Calls Against the RTL Contract

Memory-dependent functions cannot simply become combinational SV functions.
The scheduler needs a control-flow graph, dependencies, saved local values,
and continuation state while a request waits for its response.

Latch command arguments on acceptance. Publish results only when the
scheduled operation completes and hold them while the consumer stalls.
Register updates and memory commits must follow the CppHDL work/strobe
contract across the generated hierarchy.

The native source kernels currently execute an STL operation atomically in
one work call; their private container state has one owner and only registered
responses are observable. They are behavioral references, not cycle-accurate
models of a future SRAM schedule. A scheduled native backend must use the
same state transitions as generated RTL. Until then, compare accepted
transactions and results, not an assumed identical latency.

Reset must cancel outstanding work and reset allocator ownership consistently.
Calling `clear()` on a linked container can traverse and free nodes; a
backend cannot replace that with a size-counter reset while retaining stale
allocations or completions.

### 5. Eliminate Recursion With an Explicit Bound

The existing opt-in recursion pass remains useful for scalar RTL helpers.
It clones methods into numbered functions, preserves the public entry,
and rejects missing or incompatible terminal handlers. Its default bound is
ten, with an accepted range of one through 64.

For example:

```cpp
[[clang::annotate("CPPHDL_HLS_MAX_RECURSION=4")]]
uint64_t sum(uint32_t n, uint32_t budget);
```

The existing pass requires a matching nonrecursive `sum_limit` method.
Native code must implement the same bound and exhaustion behavior. It does
not automatically make unbounded C++ recursion safe.

Unmodified standard-library methods do not supply these handlers. Their
recursion therefore needs a different lowering policy: prove an applicable
bound from the allocation contract, or generate a checked bounded call stack.
Silent truncation would corrupt operations such as recursive destruction.
A failed operation also needs defined handling for stores already performed.

Combinational, bounded call graphs can use numbered nonrecursive functions.
Calls which wait for memory must use scheduled continuation state and bounded
frames. Neither approach requires recursive or `automatic` SV functions.
This STL-specific recursion/scheduling integration remains unimplemented.

## Tests and Commands

```sh
cmake -S . -B build -DCPPHDL_BUILD_TESTS=ON
cmake --build build --target hls_tests -j4
ctest --test-dir build -R '^hls_' --output-on-failure -j4
```

Test names distinguish what is actually validated:

- `hls_bounded_vector_native` executes the bounded STL vector and checks it
  against a vector using the ordinary allocator.
- `hls_bounded_vector_rtl_0` and `_rtl_1` generate actual method-body RTL and
  run Verilator with register and SRAM storage. They cover reallocation,
  relocation, overlapping erase, reuse, invalid indexes, exhaustion, response
  backpressure, delayed memory, reset recovery, and aborting work on reset.
- `hls_kernel_memory_native` and its two RTL variants check unaligned fields,
  overlapping moves in both directions, saved pointers, loop-carried PHIs,
  signed values, allocator rebinding/reuse/double-free detection, and (in RTL)
  invalid-address faults.
- `hls_grouped_schedule_native` and its two RTL variants exercise dependent
  arithmetic, branches and joins, overlapping stores, loop-carried values,
  continue/break, nested loops, and reset during a suspended loop. RTL latency
  assertions require one cycle for acyclic computation and one cycle per
  iteration for the simple recurrence. Vector register tests also require
  one-cycle reads, size queries, and clear operations.
- Seven `hls_kernel_reject_*` tests reject unresolved calls, indirect calls,
  recursion, floating point, dynamic stack allocation, atomic accesses, and
  unsupported state alignment. `hls_kernel_cli` checks incompatible options.
  They also check that a failed conversion removes stale generated RTL.
- `hls_std_Map_object_native` and `hls_std_Map_heap_native` run real STL
  objects in the C++ module and check the command/response behavior.
- Corresponding `_unsupported_rtl` tests require a conversion error, inspect
  the method/layout report, and reject any placeholder SV files. They are
  expected-rejection tests, **not Verilator passes**.
- `hls_std_missing_capacity` checks the capacity diagnostic.
- `hls_sram` and `hls_sram_verilator` test the generic SRAM responder with
  both native and RTL simulation, including backpressure and reset retention.
- Recursion and static-helper tests retain their native and Verilator flows.
  Six additional tests exercise recursion diagnostics.

Set `CPPHDL_HLS_STDLIB_TREE_SOURCE` to a matching libstdc++ `tree.cc` when
configuring to add optional Map and Multimap source-attachment regressions.
They require real rotation and rebalance bodies in the report. These remain
analysis checks, not working container RTL flows.

To inspect the real map methods:

```sh
build/cpphdl --hls --generated-dir build/hls-map-analysis \
    hls/tests/Map.cpp -- -Iinclude -DHLS_HEAP=1
```

This command currently returns failure and creates `hls-analysis.json`,
not a map SV module. Inspect its `methods`, `records`, and `issues` arrays.
The analysis uses structured JSON, preserving source references and method
bodies so subsequent compiler work can be checked against the real library.

`std::inplace_vector` is recognized by the discovery logic but has no local
test because the installed standard library does not provide it. AXI4,
multi-port scheduling, general alias analysis, and cycle-accurate scheduled
native container implementations are not supported. No synthesis area or timing
result is claimed.

## Acceptance Criteria for Container RTL

Do not declare a container supported until its actual method bodies reach
generated RTL, every reachable algorithm dependency is resolved, and both
native scheduled simulation and Verilator pass the same transaction tests.

Tests must additionally exercise container-specific behavior: vector
reallocation, list links and iterator stability, map rotations and deletion
fixup, multimap duplicate ordering, and unordered-map collisions and rehashing.
Check structural invariants and storage bounds, not just final lookup values.
A command-level comparison alone cannot establish those properties.
