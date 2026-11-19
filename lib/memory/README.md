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
initialize memory before reading it. The replacement RTL uses MLAB for banks
of at most 64 entries and M20K for deeper banks. Pass `true` as the optional
third template argument of `SynchronousRam<WORDS, DEPTH, BLOCK>` or
`MaskedSynchronousRam<WORDS, DEPTH, BLOCK>` to select M20K for a shallow bank,
including its output registers. This is useful when many shallow banks would
otherwise consume logic and local routing. The masked RAM's bounded
physical indices retain invalid-write rejection and synchronous read-first
behavior; out-of-range RTL reads remain undefined.

MatrixTransfer accepts a transfer command and drives a separate memory request
and completion protocol. Its test covers delayed and immediate responses,
backpressure, exhausted outstanding-read credits, partial tiles, both transpose
directions, sparse strides, and error draining.

The transfer controller advances affine addresses with additions and uses a
shared five-stage combinational barrel rotation to address its 32 diagonal
banks. Converted incoming elements and FIFO metadata are registered before bank
steering. This preserves one-response-per-clock throughput and adds one drain
clock per tile before output reads; reset cancels that pending bank write.
Command address products, sums and checks take four setup clocks before the
first request. The controller validates captured arguments and rejects
overflow, overlap and invalid extents without memory traffic.

Build the library tests (`CPPHDL_BUILD_LIB_TESTS=ON`), then run:

```sh
ctest --test-dir build -R '^memory_.*_(native|verilator)$' --output-on-failure
```

These tests are registered with the shared library, including when HLS and
example tests are disabled.
