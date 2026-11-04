# Shared RTL Library

These headers implement RTL and arithmetic reused by both ordinary C++HDL
designs and HLS adapters. They do not depend on `hls/` or on `examples/` when
included as a library. Include them without defining `MAINFILE`.

| Header | Purpose | HLS consumer |
| --- | --- | --- |
| [math/FpConverter.h](math/FpConverter.h) | FP formats and conversion; signed int32 conversion | Narrow-format arithmetic pipelines |
| [math/FpAdd.h](math/FpAdd.h) | FP addition with one enabled, registered sum stage | FP32 MAC RTL replacement |
| [math/Fp32Mac.h](math/Fp32Mac.h) | Enabled two-stage FP32 multiply/add, separately rounded | FP32 MAC pipeline adapter |
| [math/Fp32Bits.h](math/Fp32Bits.h) | Integer FP32 arithmetic and exact FP16 widening | `hls/math/Float.h` policies |
| [math/Fp32Add.h](math/Fp32Add.h) | Narrow-datapath FP32 adder methods | Narrow arithmetic pipelines |

The library also includes reusable buffering and memory modules:

| Header | Purpose |
| --- | --- |
| [basic/Buffer.h](basic/Buffer.h) | Parameterized buffering stage |
| [basic/Fifo.h](basic/Fifo.h) | FIFO with selectable registered/show-ahead reads |
| [basic/Memory.h](basic/Memory.h) | Parameterized memory with write masks |
| [memory/SynchronousRam.h](memory/SynchronousRam.h) | Independent read/write ports and registered reads |
| [memory/MaskedSynchronousRam.h](memory/MaskedSynchronousRam.h) | Synchronous RAM with per-lane write enables |
| [memory/MatrixTransfer.h](memory/MatrixTransfer.h) | Matrix transfers, format conversion and banked RAM |

See [the memory contracts](memory/README.md) for latency and read/write behavior.

Existing class names and the helper namespace `cpphdl_examples::math` are
unchanged. `FpMathTest.h` and `Fp32Reference.h` are test helpers, not RTL.
Math blocks without HLS consumers remain in [examples/math](../examples/math/README.md).

## Conversion Options

`FpConverter<Source, Destination, Lanes, Registered, PreserveSubnormals,
PreserveNanPayload>` defaults both preservation options to false. Enable
both for exact FP16 widening. Rounding is nearest-even; `int32_t` is also
accepted as the source type:

```cpp
FpConverter<FP16E5, FP32E8, 1, false, true, true> half_to_single;
FpConverter<int32_t, FP32E8, 1, true> integer_to_single;
```

The same conversion implementation is available inside C++/HLS methods:

```cpp
FP16E5 half;
FP32E8 single;
half.raw = bits;
half.convert(single, true, true);
auto widened = FP16E5::convert_raw<FP32E8>(bits, true, true);
auto converted = FP32E8::from_int_raw(signed_value);
```

Narrowing with preservation enabled rounds into subnormals and never turns a
truncated NaN payload into infinity. The FP32 bit-level arithmetic uses
round-to-nearest-even and flushes FP32 subnormal operands/results to signed
zero. Arithmetic NaNs are canonicalized; FP16 widening preserves the payload.

## Native and RTL Tests

As in `examples/`, each implementation header contains its complete testbench.
Defining `MAINFILE` enables `main_from_header(int, char**)`;
[`tests/ExampleMain.cpp`](../tests/ExampleMain.cpp) supplies the ordinary `main()`.
Dependencies suppress `MAINFILE` while being included, so their testbenches
are not enabled accidentally. The same tests run in C++ and against generated
SystemVerilog through Verilator.

CMake option `CPPHDL_BUILD_LIB_TESTS` defaults to `ON` and is independent of
`CPPHDL_BUILD_EXAMPLES_TESTS`. These options control self-tests and RTL
generation, not availability of the headers. Existing target and CTest names
are preserved:

```sh
cmake -S . -B build -G "Unix Makefiles" -DCPPHDL_BUILD_LIB_TESTS=ON
cmake --build build --target math_FpConverter math_FpAdd math_Fp32Mac \
    math_Fp32Bits math_Fp32Add math_FpConverter_options \
    basic_Buffer basic_Fifo basic_Memory memory_SynchronousRam \
    memory_MaskedSynchronousRam memory_MatrixTransfer
ctest --test-dir build -L lib --output-on-failure
```

The external Makefile provides the same flows as the examples:

```sh
make -C lib EXAMPLE=math/Fp32Bits.h native rtl
make -C lib EXAMPLE=basic/Buffer.h native rtl
make -C lib EXAMPLE=memory/MaskedSynchronousRam.h native rtl
make -C lib EXAMPLE=math/FpAdd.h generate
```

Override `CXX`, `CPPHDL`, `VERILATOR` and `PYTHON` as needed. Test executables
and temporary RTL builds go into `build/`. The converter-options tests use
`FP_CONVERTER_OPTIONS_TEST` in addition to `MAINFILE` and are registered in both
flows. Coverage includes all FP16 encodings, randomized arithmetic, int32
rounding, special values and registered reset/enable behavior. The standalone
header regression also compiles the library without the examples or HLS tree.

Math snapshots are in [math/generated](math/generated/). The MAC has a
separate `Fp32Mac/` directory because its adder uses `clk`, while standalone
FpAdd uses `sum_clock`. `FpConverterOptions/` contains the conversion-policy
test top. Regenerate different tops into separate output directories.
Buffering and memory snapshots live in [basic/generated](basic/generated/)
and [memory/generated](memory/generated/).
