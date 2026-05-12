# CppHDL Best Practices

CppHDL is a C++ RTL framework for writing synthesizable hardware models while keeping the code executable as normal C++. The main advantages are:

* You write RTL in a familiar general-purpose language with templates, types, functions, and normal build tools.
* The same source can run as a fast native C++ model and can also be converted to SystemVerilog.
* Testbenches can be ordinary C++ programs, so random tests, reference models, file I/O, and debugging are simple.
* Large parameterized designs can use C++ templates instead of large preprocessor-heavy SystemVerilog code.
* CppHDL encourages small modules, explicit registers, and clear combinational/sequential separation.

## Mapping of SystemVerilog Expressions to C++

CppHDL code should be written as a direct C++ mapping of synthesizable SystemVerilog RTL. Continuous assignments and module port connections go into the `_assign()` section. This section runs only once, before the work cycle starts, and binds assignments that are used later during simulation and SystemVerilog generation. The `_ASSIGNxxx()` macros are only allowed in `_assign()`.

SystemVerilog:

```systemverilog
assign out = a + b;
child.valid_i = valid;
child.data_i = data[i];
child.result_i[i] = result[i] ^ mask;
```

CppHDL:

```cpp
_PORT(u<32>) out = _ASSIGN(a_in() + b_in());

void _assign()
{
    child.valid_in = _ASSIGN(valid_reg);
    for (int i = 0; i < LANES; i++) {
        child.data_in[i] = _ASSIGN_I(data_reg[i]);
        child.result_in[i] = _ASSIGN_I(result_reg[i] ^ mask_reg);
    }
}
```

Use `_ASSIGN(expr)` for expressions. Use `_ASSIGN_REG(reg_or_signal)` for direct storage bindings such as registers, logic values, memories, or ports whose final object reference is enough. Use `_ASSIGN_COMB(comb_func())` when assigning the result of a CppHDL combinational function. Even though `_ASSIGN_COMB()` captures the returned object by reference, the `comb_func()` call itself is still executed on demand when the port value is read. For loop-indexed assignments use `_ASSIGN_I`, `_ASSIGN_REG_I`, `_ASSIGN_COMB_I`, or the indexed forms such as `_ASSIGN_INDEXED((i,j,k), expr)` and `_ASSIGN_REG_INDEXED((i,j,k), object[i][j][k])`.

All SystemVerilog `always_ff` blocks for one module map into one CppHDL `_work(bool reset)` method. `_work()` computes next register values. It may contain the logic that would be split across several `always_ff` blocks in SystemVerilog.

SystemVerilog:

```systemverilog
always_ff @(posedge clk) begin
    if (reset) count <= '0;
    else if (enable) count <= count + 1;
end

always_ff @(posedge clk) begin
    if (reset) valid <= 1'b0;
    else valid <= enable;
end
```

CppHDL:

```cpp
reg<u<8>> count_reg;
reg<bool> valid_reg;

void _work(bool reset)
{
    if (reset) {
        count_reg.clr();
        valid_reg.clr();
        return;
    }

    if (enable_in()) {
        count_reg._next = count_reg + u<8>(1);
    }
    valid_reg._next = enable_in();
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

CppHDL commits registers and memories in the mandatory `_strobe()` method. `_strobe()` is executed recursively for each module at the end of each clock evaluation. Register `.strobe()` calls and memory `.apply()` calls are only allowed in `_strobe()`, not in `_assign()`, `_work()`, or comb functions.

```cpp
void _strobe()
{
    count_reg.strobe();
    valid_reg.strobe();
    memory.apply();
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
        count.clr();
        return;
    }
    if (enable_in()) {
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
for (i = 0; i < N; ++i) {
    out[i].valid_in = _ASSIGN_I(sel == i ? input.valid_in() : 0);
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

Keep combinational functions side-effect-free except for assigning their own cached result variable.

## Sequential Logic

Write next-state logic in `_work(reset)` and commit it in `_strobe()`.

```cpp
void _work(bool reset)
{
    if (reset) {
        valid_reg.clr();
        return;
    }

    valid_reg._next = next_valid;
    data_reg._next = next_data;
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
    state._next = child.data_out();
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
        valid_reg.clr();
        return;
    }
    if (!valid_reg || ready_in()) {
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
    _PORT(logic<DATA_WIDTH>) write_data_in;
    reg<u<clog2(DEPTH)>> write_ptr;
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

`assignIf()` performs the bidirectional assignment order needed when one side drives `valid/data` and the other side drives `ready`.

## Arrays, Logic, and Bit Ranges

Use `logic<N>` for arbitrary-width bit vectors and `u<N>` for unsigned integers up to 64 bits. Use `.bits(hi, lo)` and `operator[]` for bit slicing.

```cpp
logic<64> word;

word.bits(31, 0) = low_word;
word.bits(63, 32) = high_word;
word[0] = parity_bit;
```

Use `array<T,N>` for packed arrays and `memory<T,WIDTH,DEPTH>` for memories.

## Keep Synthesizable Code Simple

CppHDL can run any C++ in native simulation, but converted RTL should use a synthesizable subset:

* Keep variables declared before complex statements when possible.
* Avoid dynamic allocation in RTL models.
* Avoid STL containers in synthesizable state.
* Prefer fixed-size CppHDL types: `u<>`, `logic<>`, `array<>`, `memory<>`, and structs.
* Put file I/O, randomization, and reference models in inline tests, not in RTL modules.

## Inline Tests

Examples and tests usually keep the first test in the same `.cpp` file as the RTL model. This is the CppHDL inline test practice: the file contains synthesizable modules first, then a `// CppHDL INLINE TEST` section with ordinary C++ test code.

An inline test normally does four things:

* Instantiates the CppHDL model.
* Drives input ports with C++ values.
* Runs the RTL cycle hooks: `_assign()`, `_work(reset)`, and `_strobe()`.
* Checks outputs and internal behavior against expected values or a C++ reference model.

Minimal shape:

```cpp
// CppHDL INLINE TEST ///////////////////////////////////////////////////

long sys_clock = -1;

void tick(MyModule& dut, bool reset)
{
    dut._assign();
    dut._work(reset);
    ++sys_clock;
    dut._strobe();
}

void TestMyModule()
{
    MyModule dut;

    tick(dut, true);
    tick(dut, false);

    TEST_ASSERT(dut.done_out() == false);
}
```

Keep randomization, file I/O, scoreboards, and reference models in the inline test, not in synthesizable RTL classes. This keeps the RTL clean while still allowing strong native C++ tests.

## `sys_clock`

Every CppHDL inline test must define:

```cpp
long sys_clock = -1;
```

`sys_clock` is the global simulation clock used by CppHDL to cache and refresh combinational values. It must be incremented once per simulated clock edge before or around `_strobe()`, as shown in the examples:

```cpp
dut._assign();
dut._work(reset);
++sys_clock;
dut._strobe();
```

Without `sys_clock`, inline tests either fail to link or do not update cached combinational expressions correctly. This is why all examples define it in the test section, even when the RTL model itself has no explicit clock port.

## Build and Run Tests

The `examples/` and `tests/` folders are built by CMake. Each `.cpp` file becomes one executable target, and the post-build command runs the `cpphdl` tool to generate SystemVerilog.

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
./build/tests/interface_ValidReady --noveril
```

Run the full test, including the Verilator part when the inline test supports it:

```bash
./build/tests/interface_ValidReady
```

For a new test, place it under `tests/<group>/<Name>.cpp`. The target name is derived from the relative path, so `tests/interface/ValidReady.cpp` becomes `interface_ValidReady`.

## Conclusion

CppHDL runs RTL directly as native C++. No translator is used for native simulation, so compile/debug/test cycles are fast and use ordinary C++ tooling. For large projects, native CppHDL simulation can run about 10 times faster than Verilator and about 100 times faster than traditional SystemVerilog simulators, while the same RTL source can still be converted to SystemVerilog when needed.
