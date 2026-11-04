# Reusable HLS math

`Float.h` is an opt-in policy header for ordinary FP32 C++ add, subtract,
negate and multiply. It registers `HLS_OVERRIDE_BITS` implementations with the
HLS engine; include it before converting the owning method. `decode`/`encode`
transport IEEE representation bits without numerical conversion. `mac(c,a,b)`
is `c + a*b`, with separate multiply/add rounding. Compile CPU comparisons with
`-ffp-contract=off`. Hardware parallelism comes from the owning OpenMP loops.

[Fp32Bits.h](../../lib/math/Fp32Bits.h) contains the pure integer implementations used by these policies.
They implement round-to-nearest-even, signed zero, overflow to infinity,
canonical arithmetic NaNs, and flush exponent-zero FP32 operands/results to
signed zero. `fp16_to_fp32` preserves finite FP16 values exactly, including
subnormals; NaN representation transport preserves the shifted payload.
Native ordinary float uses the host's floating environment; it need not flush
subnormals. Bit-level comparisons should explicitly apply the hardware FTZ
policy. Tolerance-based inference comparisons must declare summation order.

`Reduction.h` provides `reduce_rows`, with native OpenMP `+` reduction and an
HLS balanced FP32 combine tree. OpenMP allows association to differ on CPU.
`reduce_rows_tree` instead implements a specific recursive pairwise tree shared
by CPU/HLS, carries odd elements and applies the final +0 identity. The host
recursive cache path uses this explicit tree only when exporting striped C;
intermediate cache transfers preserve all partials. `ReductionMethods` is a
stateless streaming packet adapter around the deterministic tree.

The following ordinary RTL blocks belong to the shared `lib/math/` library,
not the HLS layer. Their headers can be used without HLS. The HLS MAC adapter includes
`../../lib/math/FpAdd.h` to reuse the registered adder:

| Header | Existing block |
| --- | --- |
| [FpAdd.h](../../lib/math/FpAdd.h) | Registered significand sum; combinational alignment/normalization |
| [FpConverter.h](../../lib/math/FpConverter.h) | FP format types and optional registered conversion |
| [Fp32Bits.h](../../lib/math/Fp32Bits.h) | Integer implementations for FP32 arithmetic and exact FP16 conversion |
| [Fp32Mac.h](../../lib/math/Fp32Mac.h) | Enabled two-stage FP32 multiply/add RTL |
| [Fp32Add.h](../../lib/math/Fp32Add.h) | Narrow FP32 adder methods |

FpSqrt, FpTanh, Fp32Exp and Maximum remain ordinary
[RTL examples](../../examples/math/README.md); these HLS policies do not include them.

The parameterized FP blocks and Fp32Mac retain their global class names. Pure
arithmetic helpers use `cpphdl_examples::math`; HLS policies import them where needed.
FpConverter retains its format
aliases and C++23 formatting support. These blocks have
explicit RTL protocols; importing a header does not imply an automatic
zero-latency sqrt/tanh operator replacement. No exp/division policy is silently
installed. FpAdd's historical underflow check occurs before rounding; the
bit-level adder rounds and then flushes, so the underflow boundary is a known
contract difference. Implementations and inline tests live together in
`lib/math/`; see [library build instructions](../../lib/README.md).

The project compatibility runner checks all four original native examples.
FP32 tests compare 200,000 add/multiply references and all 65,536 FP16 inputs.
The OpenMP Float regression verifies the policies through native/RTL/retimed
flows, including +/* reductions, helpers, scratch and neighbor snapshots.

`Nonlinear.h` additionally installs FP32 division and signed/unsigned 32-bit
integer conversion policies. Its restoring division and square root follow
RNE/FTZ. `exponential` and `square_root` have named HLS overrides; native calls
use standard host math. The integer `fp32_exp` uses range reduction and a
degree-seven polynomial with a tested relative error bound of 2e-5 on finite
normal outputs. It is not correctly rounded exp. `fp32_silu` uses the stable
negative-exponent form. Including `Float.h` alone does not install these extra
operators. Actual math latency depends on the selected HLS scheduler and
retiming report, and must be integrated through ready/valid protocols.
