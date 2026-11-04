# RTL Math Examples

These are ordinary C++HDL RTL blocks. They do not require the HLS layer.
Reusable implementations and their executable C++/Verilator tests live here:

| Header (model and tests) | Purpose |
| --- | --- |
| [FpSqrt.h](FpSqrt.h) | Combinational floating-point square root |
| [FpTanh.h](FpTanh.h) | Combinational fixed-point approximation of floating-point tanh |
| [Fp32Exp.h](Fp32Exp.h) | Table-based approximate FP32 exponential |
| [Maximum.h](Maximum.h) | First-maximum reduction with original positions |
| [Transposer.h](Transposer.h) | Memory-backed matrix transposition |

FpAdd, FpConverter, Fp32Mac, Fp32Bits and Fp32Add are shared with HLS and now
live in [lib/math](../../lib/README.md), together with their inline tests and
generated RTL. Their class names and CTest names are unchanged.

Include the appropriate header to reuse a block without its testbench.
`W` is the total floating-point width and `EW` is the exponent width.
The RTL blocks must not depend on HLS. The exponential is an
approximation, not a correctly rounded implementation. Maximum uses the
original position to preserve first-maximum semantics across reduction trees.

The stateless helpers live in `cpphdl_examples::math`. Their `*Methods` classes expose
methods the RTL converter can instantiate; free functions remain available to
ordinary C++ and HLS callers. Defining `MAINFILE` enables the header's test
wrapper and `main_from_header()` entry point. Both flows use the same embedded
testbench; see [the build instructions](../README.md).

Generated RTL for these examples is in `generated/`. Shared-library snapshots
are in `lib/math/generated/`. Regenerate each design into a separate output
directory when combining examples in a larger project.

Build the examples, then run `ctest --test-dir build -R '^math_.*_(native|verilator)$'
--output-on-failure`. HLS is not needed.
