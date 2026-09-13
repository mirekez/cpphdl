# CppHDL Best Practices

CppHDL is a C++ RTL framework for writing synthesizable hardware models while keeping the code executable as normal C++. The main advantages are:

* You write RTL in a familiar general-purpose language with templates, types, functions, and normal build tools.
* The same source can run as a fast native C++ model and can also be converted to SystemVerilog.
* Testbenches can be ordinary C++ programs, so random tests, reference models, file I/O, and debugging are simple.
* Large parameterized designs can use C++ templates instead of large preprocessor-heavy SystemVerilog code.
* CppHDL encourages small modules, explicit registers, and clear combinational/sequential separation.

## Mapping of SystemVerilog Expressions to C++

CppHDL code should be written as a direct C++ mapping of synthesizable SystemVerilog RTL. Continuous assignments and module port connections belong in port member initializers or the `_assign()` section. This section runs only once, before the work cycle starts, and binds assignments that are used later during simulation and SystemVerilog generation. The `_ASSIGNxxx()` macros belong only in these static connection contexts, never in work, strobe, or combinational methods.

SystemVerilog (inside the parent module):

```systemverilog
wire [31:0] child__data_in;
assign child__data_in = data_reg;
Child child (.clk(clk), .reset(reset), .data_in(child__data_in));
```

CppHDL (matching parent members and binding):

```cpp
Child child;
reg<logic<32>> data_reg;

void _assign()
{
    child.data_in = _ASSIGN_REG(data_reg);
    child._assign();
}
```

Use `_ASSIGN(expr)` for expressions. Use `_ASSIGN_REG(reg_or_signal)` for direct storage bindings such as registers, logic values, memories, or ports whose final object reference is enough. Use `_ASSIGN_COMB(comb_func())` when assigning the result of a CppHDL combinational function. Both reference-binding macros take the address of an lvalue: do not pass a temporary, cast result, or by-value function call. `_ASSIGN_COMB()` invokes the comb chain on the first port read in a new `_system_clock` epoch; later reads reuse the cached result reference. For loop-indexed assignments use `_ASSIGN_I`, `_ASSIGN_REG_I`, `_ASSIGN_COMB_I`, or the indexed forms such as `_ASSIGN_INDEXED((i,j,k), expr)` and `_ASSIGN_REG_INDEXED((i,j,k), object[i][j][k])`.

For the default positive-edge clock, sequential logic maps into `_work(bool reset)`. Negative-edge logic uses `_work_neg(bool reset)` with `_strobe_neg()`; multi-clock designs use named work/strobe pairs (see the CDC chapter in `spec.md`). `_work()` computes next register values. It may contain the logic that would be split across several `always_ff` blocks in SystemVerilog.

SystemVerilog:

```systemverilog
always_ff @(posedge clk) begin
    if (reset) count_reg <= '0;
    else if (enable_in) count_reg <= count_reg + 1;
end

always_ff @(posedge clk) begin
    if (reset) valid_reg <= 1'b0;
    else valid_reg <= enable_in;
end
```

CppHDL:

```cpp
reg<u<8>> count_reg;
reg<u1> valid_reg;

void _work(bool reset)
{
    if (reset) {
        count_reg._next = 0;
        valid_reg._next = 0;
    }
    else {
        if (enable_in()) {
            count_reg._next = count_reg + u<8>(1);
        }
        valid_reg._next = enable_in();
    }
}
```

SystemVerilog `always_comb` blocks map to CppHDL combinational functions, usually named `*_comb_func()`. They calculate temporary combinational values from current inputs and current register values. The usual style is to store the result in a member variable and return it by reference, as shown in the root examples.

SystemVerilog:

```systemverilog
always_comb begin
    hit = valid && tag == req_tag;
    read_data = hit ? line[word] : '0;
end
```

CppHDL:

```cpp
bool hit_comb;
bool& hit_comb_func()
{
    hit_comb = valid_reg && tag_reg == req_tag_in();
    return hit_comb;
}

u<32> read_data_comb;
u<32>& read_data_comb_func()
{
    read_data_comb = hit_comb_func() ? line_reg[word_in()] : u<32>(0);
    return read_data_comb;
}

_PORT(u<32>) read_data_out = _ASSIGN_COMB(read_data_comb_func());
```

CppHDL commits registers and memories in `_strobe()` (or the matching named/negative-edge strobe method). The testbench calls the top-level strobe after work evaluation; each parent explicitly calls its children's strobe methods. Register `.strobe()` calls and memory `.apply()` calls belong only in strobe methods, not in `_assign()`, work, or comb functions.

```cpp
void _strobe()
{
    count_reg.strobe();
    valid_reg.strobe();
    data_mem.apply();
}
```

## Think Like RTL

CppHDL is C++, but the model should still be written as RTL. Keep the same mental model as SystemVerilog:

* Ports describe module inputs and outputs.
* `reg<T>` is state.
* `_work(reset)` computes next state.
* `_strobe()` commits next state to current state.
* Combinational functions compute outputs from current inputs/state.

SystemVerilog:

```systemverilog
always_ff @(posedge clk) begin
    if (reset) begin
        count <= '0;
    end else if (enable) begin
        count <= count + 1;
    end
end

assign done = (count == LIMIT);
```

CppHDL:

```cpp
reg<u<8>> count;
_PORT(bool) enable_in;
_PORT(bool) done_out = _ASSIGN(count == LIMIT);

void _work(bool reset)
{
    if (reset) {
        count._next = 0;
    }
    else if (enable_in()) {
        count._next = count + u<8>(1);
    }
}

void _strobe()
{
    count.strobe();
}
```

## Ports

Use `_PORT(type)` for module and interface ports. A port is called like a function to read its value:

```cpp
_PORT(bool) valid_in;
_PORT(logic<32>) data_in;

if (valid_in()) {
    word._next = data_in();
}
```

Output ports are usually initialized with `_ASSIGN_REG(...)`, `_ASSIGN_COMB(...)`, or `_ASSIGN(...)`:

```cpp
reg<u1> ready_reg;
_PORT(bool) ready_out = _ASSIGN_REG(ready_reg);
_PORT(bool) empty_out = _ASSIGN(count == 0);
```

## `_ASSIGN_REG`, `_ASSIGN`, and Indexed Expressions

`_ASSIGN_REG(x)` connects a port to a persistent variable. It is the best choice for registers and member variables.

```cpp
reg<logic<32>> data_reg;
_PORT(logic<32>) data_out = _ASSIGN_REG(data_reg);
```

`_ASSIGN(expr)` connects a port to an expression. Use it for simple computed outputs:

```cpp
_PORT(bool) fire_out = _ASSIGN(valid_in() && ready_in());
```

`_ASSIGN_I(expr)` and `_ASSIGN_REG_I(expr)` are used in loops where the loop index must be captured for each generated connection:

```cpp
void _assign()
{
    uint32_t i;
    for (i = 0; i < N; ++i) {
        lanes[i].data_in = _ASSIGN_I(data_reg[i]);
        lanes[i]._assign();
    }
}
```

There are also `_J` and `_IJ` forms for nested loops.

## Combinational Logic

For simple outputs, `_ASSIGN(...)` is enough. For larger combinational logic, write a member function that stores the result in a member variable and returns it by reference.

```cpp
logic<32> result_comb;

logic<32>& result_comb_func()
{
    result_comb = a_in() + b_in();
    if (sub_in()) {
        result_comb = a_in() - b_in();
    }
    return result_comb;
}

_PORT(logic<32>) result_out = _ASSIGN_COMB(result_comb_func());
```

Keep combinational functions side-effect-free except for assigning their own result variable. Call dependencies through their `*_comb_func()` methods; do not read another comb's result storage directly or rely on a separate preparation call.

Ordinary comb methods execute on every direct call. Use `_LAZY_COMB` to cache a result per `_system_clock` epoch:

```cpp
_LAZY_COMB(sum_comb, logic<32>)
    sum_comb = a_in() + b_in();
    return sum_comb;
}
```

This declares both `sum_comb` storage and `sum_comb_func()`. Assign the complete result on every evaluation path to avoid stale values and inferred latches.

## Sequential Logic

Write next-state logic in `_work(reset)` and commit it in `_strobe()`.

```cpp
void _work(bool reset)
{
    if (reset) {
        valid_reg._next = 0;
        data_reg._next = 0;
    }
    else {
        valid_reg._next = next_valid;
        data_reg._next = next_data;
    }
}

void _strobe()
{
    valid_reg.strobe();
    data_reg.strobe();
}
```

Call nested module hooks explicitly:

```cpp
void _work(bool reset)
{
    child._work(reset);
    if (reset) {
        state._next = 0;
    }
    else {
        state._next = child.data_out();
    }
}

void _strobe()
{
    child._strobe();
    state.strobe();
}
```

## CppHDL and SystemVerilog Look Similar

A valid-ready transfer in SystemVerilog:

```systemverilog
assign out_valid = valid_reg;
assign out_data  = data_reg;

always_ff @(posedge clk) begin
    if (reset) begin
        valid_reg <= 1'b0;
    end else if (!valid_reg || out_ready) begin
        valid_reg <= have_data;
        data_reg  <= next_data;
    end
end
```

The same idea in CppHDL:

```cpp
_PORT(bool) valid_out = _ASSIGN_REG(valid_reg);
_PORT(logic<32>) data_out = _ASSIGN_REG(data_reg);
_PORT(bool) ready_in;

void _work(bool reset)
{
    if (reset) {
        valid_reg._next = 0;
    }
    else if (!valid_reg || ready_in()) {
        valid_reg._next = have_data;
        data_reg._next = next_data;
    }
}
```

## Use C++ Templates for Parameters

Prefer templates for structural parameters such as data width, address width, number of ports, or FIFO depth.

```cpp
template<size_t DATA_WIDTH, size_t DEPTH>
class Fifo : public Module
{
    static_assert(DEPTH > 0, "DEPTH must be positive");
public:
    _PORT(logic<DATA_WIDTH>) write_data_in;
private:
    reg<u<(DEPTH <= 1 ? 1 : clog2(DEPTH))>> write_ptr;
    // Work and strobe methods omitted from this declaration excerpt.
};
```

This keeps parameterized RTL type-safe and avoids many preprocessor tricks.

## Interfaces

Use `Interface` for bundles containing signals of different directions. You do not need separate driver and responder interface types.

```cpp
template<size_t DATAWIDTH>
struct ValidReadyIf : public Interface
{
    _PORT(bool) valid_in;
    _PORT(bool) ready_out;
    _PORT(logic<DATAWIDTH>) data_in;
};
```

A source and sink can both use `ValidReadyIf`. A parent module or test wrapper can connect them with `assignIf()`:

```cpp
VRDriver<32> driver;
VRResponder<32> responder;

void _assign()
{
    assignIf(driver, responder, driver.source_out, responder.sink_in);
}
```

`assignIf()` performs the bidirectional binding order needed when one side drives `valid/data` and the other drives `ready`; it invokes the endpoint modules' `_assign()` methods during setup. Connect an interface as a bundle instead of manually copying its individual ports. Endpoint modules still bind the signals they themselves drive.

Make connections in the immediate common parent. Do not reach through a child into a grandchild or assign another module's internal state. Forward an interface through a proxy with `assignIf(*this, child, proxy_in, child.sink_in)`; see `tests/interface/AssignIfHierarchyProxy.cpp`.

## Arrays, Logic, and Bit Ranges

Use `logic<N>` for arbitrary-width bit vectors and `u<N>` for unsigned integers up to 64 bits. Use `.bits(hi, lo)` and `operator[]` for bit slicing.

```cpp
logic<64> word;

word.bits(31, 0) = low_word;
word.bits(63, 32) = high_word;
word[0] = parity_bit;
```

Use `array<COUNT, TYPE, PACKED = false>` (count first). The default uses unpacked C++ element storage; `true` uses packed storage. Use `memory<TYPE, ROW_SIZE, DEPTH>` for deferred-write memories, where `ROW_SIZE` counts elements per row, not bits.

```cpp
array<16, u8> bytes;
array<16, u8, true> packed_bytes;
array2D<4, 8, u16> matrix;
memory<u8, 4, 256> words; // 256 rows of four bytes.
```

See `spec.md` for the distinction between C++ storage and generated SV packing. Synthesizable indexes and loop variables should use `uint32_t` or a narrower type, not 64-bit `size_t`.

## Keep Synthesizable Code Simple

CppHDL can run any C++ in native simulation, but converted RTL should use a synthesizable subset:

* Variables can be declared inside methods/functions, but put all declarations at the very beginning of the method/function.
* Avoid dynamic allocation in RTL models.
* Avoid STL containers in synthesizable state.
* Prefer fixed-size CppHDL types: `u<>`, `logic<>`, `array<>`, `memory<>`, and structs.
* Put file I/O, randomization, and reference models in inline tests, not in RTL modules.
* Do not assume default-constructed signals are zero. Assign local values before reading them and explicitly reset required register state.
* Prefer `if (reset) { ... } else { ... }` to early returns from work methods, and always propagate reset to child work methods.

## Inline Tests

Examples and tests usually keep the first test in the same `.cpp` file as the RTL model. This is the CppHDL inline test practice: the file contains synthesizable modules first, then a `// CppHDL INLINE TEST` section with ordinary C++ test code.

An inline test normally does four things:

* Instantiates the CppHDL model.
* Drives input ports with C++ values.
* Runs the RTL cycle hooks: `_assign()`, `_work(reset)`, and `_strobe()`.
* Checks outputs and internal behavior against expected values or a C++ reference model.

Complete single-clock native testbench shape (assuming `MyModule` has `enable_in` and `done_out` ports):

```cpp
// CppHDL INLINE TEST ///////////////////////////////////////////////////
#if !defined(SYNTHESIS)
#include <cassert>

long _system_clock = -1; // Define once per native simulation executable.

class MyModuleTest : public Module
{
public:
    MyModule dut;
    bool enable = false;

    void _assign()
    {
        dut.enable_in = _ASSIGN(enable);
        dut._assign();
    }

    void tick(bool reset)
    {
        ++_system_clock;
        dut._work(reset);
        dut._strobe();
        ++_system_clock;
    }
};

int main()
{
    MyModuleTest test;
    test._assign(); // Once, outside the cycle loop.
    test.tick(true);
    assert(!test.dut.done_out());
    test.enable = true;
    test.tick(false);
}
#endif
```

Keep randomization, file I/O, scoreboards, and reference models in the inline test, not in synthesizable RTL classes. `SYNTHESIS` guards hide the testbench from conversion. Testbenches using `std::print` require a standard library with print support; C++17 consumers can use `printf` or streams instead. CppHDL does not provide a replacement `std::print`.

## `_system_clock`

Native ports and lazy combs use the global `long _system_clock` declared by `cpphdl_port.h`. Define it once in the testbench, not once per module. The name is `_system_clock`, not `sys_clock`.

This is a cache-invalidation epoch, not a hardware clock or elapsed-time counter. Starting from `-1`, increment it before the first evaluation. Increment it after input changes and after committing state before reading outputs again. The tick above starts a fresh epoch before work and another after strobe; use a separate counter for simulated clock cycles.

For simultaneous clock edges, evaluate all active work methods against pre-edge state before calling any strobe methods. Do not rebind ports or call `_assign()` in the cycle loop. See the CDC and asynchronous-reset chapters in `spec.md` for named-clock scheduling.

## Build and Run Tests

The `examples/` and `tests/` folders are built by CMake. Most `.cpp` files become executable targets with post-build SystemVerilog generation; optimizer and other special regressions use dedicated CMake rules.

Configure once:

```bash
cmake -S . -B build
```

Build one test:

```bash
cmake --build build --target interface_ValidReady
```

Run the native CppHDL test:

```bash
(cd build/tests && ./interface_ValidReady --noveril)
```

Run the full test, including the Verilator part when the inline test supports it:

```bash
(cd build/tests && ./interface_ValidReady)
```

Run from `build/tests` because the inline harness locates `generated/` relative to its working directory. Alternatively, use `ctest --test-dir build -R '^interface_ValidReady$' --output-on-failure` for the registered test.

For a new test, place it under `tests/<group>/<Name>.cpp`. The target name is derived from the relative path, so `tests/interface/ValidReady.cpp` becomes `interface_ValidReady`.

## Conclusion

CppHDL runs RTL directly as native C++. No translator is used for native simulation, so compile/debug/test cycles are fast and use ordinary C++ tooling. Performance depends on the design, compiler, optimizer mode, and testbench; benchmark the actual workload. The same RTL source can still be converted to SystemVerilog for verification and implementation.
