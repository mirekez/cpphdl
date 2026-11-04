# RTL Memory Library

These are ordinary RTL modules, independent of HLS. Include the header to reuse
a module. Define `MAINFILE` when compiling its embedded native/Verilator
testbench through `tests/ExampleMain.cpp`; see [the build instructions](../README.md).

| Header | Purpose |
| --- | --- |
| [SynchronousRam.h](SynchronousRam.h) | Independent read/write ports with a one-clock registered read |
| [MaskedSynchronousRam.h](MaskedSynchronousRam.h) | The same protocol with a write enable per 32-bit lane |
| [MatrixTransfer.h](MatrixTransfer.h) | Strided/transposed matrix transfers, FP16/FP32 conversion, and local banked RAM |

The reusable classes are in `cpphdl_examples`. RAM reads return the old contents when a
read and write address coincide. Without read enable the output holds. Reset
suppresses accesses but does not clear RAM contents or its output register;
initialize memory before reading it. The replacement RTL declares block-RAM
attributes and implements the same synchronous, read-first behavior.

MatrixTransfer accepts a transfer command and drives a separate memory request
and completion protocol. Its test covers delayed and immediate responses,
backpressure, exhausted outstanding-read credits, partial tiles, both transpose
directions, sparse strides, and error draining.

Build the library tests (`CPPHDL_BUILD_LIB_TESTS=ON`), then run:

```sh
ctest --test-dir build -R '^memory_.*_(native|verilator)$' --output-on-failure
```

These tests are registered with the shared library, including when HLS and
example tests are disabled.
