# C++ casts in generated SystemVerilog

Value conversions written as `static_cast<T>(x)`, `(T)x`, or `T(x)` use the
same lowering. Integer widths and signedness come from Clang's target ABI,
including aliases, character types, `long long`, and 128-bit integers. Enum
casts use the declared underlying type and a generated enum package.
CppHDL `logic<N>` and `u<N>` conversions preserve their width; symbolic
module-parameter widths remain symbolic.

Conversions preserve truncation, signed/unsigned extension, integer
promotions, and boolean nonzero tests. For example,
`uint64_t(uint32_t(x) + 1u)` performs the addition at 32 bits and wraps before
widening. A single SV 64-bit size cast would incorrectly widen the addition.

The emitter omits duplicate conversions, same-type variable casts, and a
final cast already supplied by an assignment. It omits `unsigned'(...)` when
the operand is provably unsigned in RTL, and uses typed literals instead of
casts around literals. Width barriers on intermediate arithmetic remain:
`(uint32_t(x) + 1u) % uint64_t(3)` must wrap the addition at 32 bits before
the 64-bit remainder operation. `code_buffer_casts_cpp` and
`code_buffer_casts_verilator` guard the Buffer example's generated expressions
and FIFO behavior at depths 1, 3, and 8, including reset and backpressure.
Untyped SV parameters still need conversions where their inferred width or
signedness would not preserve the declared C++ type.
Boolean assignments are not ordinary one-bit truncations: their nonzero
conversion must remain. The cast matrix also checks these assignments and
slice reads/writes with explicitly typed endpoints; slice-width folding must
use the value of `64'h1f`, not its width prefix `64`.
Bit-vector complements also retain their result width before widening:
converting `~logic<8>(0)` to 64 bits produces `0xff`, not 64 one bits.

Reference/qualification casts keep the selected object assignable instead
of converting it into an SV temporary. This includes `(Struct&)`, `const_cast`,
`reinterpret_cast` reference views, and compile-time hierarchy casts (including a
`dynamic_cast` upcast that does not require RTTI). Existing pointer views of
packed storage are structural views, not runtime C++ addresses. Casting to
`void` retains operand side effects.

This is not a runtime C++ object model. Runtime RTTI/downcasts,
pointer/integer conversions, member-pointer casts, and floating-point
conversions without a direct RTL lowering are ignored: generation continues
with the operand, preserving its side effects. This permissive fallback does
not implement their C++ semantics or guarantee valid RTL for non-hardware
operand types. Reference views retain the source signal and its layout; they
do not repack fields or adjust object offsets. Use explicit bit-vector
operations or a hardware floating-point implementation when those semantics
are needed. A cast is not a generic way to synthesize arbitrary constructors
or conversion operators.

`cast_cpp` and `cast_verilator` run the same cast matrix, with boundary,
walking-bit and deterministic random inputs. The C++ run uses ASan/UBSan;
the RTL run compares each output against the C++ model, including assignable
struct-reference round trips. `cast_fallback` checks that eight previously
rejected casts now convert successfully and retain their operands; it does
not assert RTL equivalence for runtime-only features.
The sign-extension cases also check both flows against an independent unsigned
bit-pattern oracle, exhausting all 65,536 16-bit inputs in addition to the
boundary/random vectors. They cover signed 8/16/32/64-bit values, unsigned
destinations, chained narrowing/widening, `logic<64>` destinations, conditional
integer promotions, arithmetic right shifts, and arithmetic after promotion,
using all three value-cast spellings.
`cast_parameter_widths` compares C++ and RTL
with widths 1, 9, and 33, overriding the same generated module's parameter
without regenerating it. This includes signed-byte conversion into each
parameterized width, with all 256 byte encodings checked against a bit-pattern
oracle. It also checks all-byte strobes formed with `~logic<(WIDTH + 7) / 8>(0)`:
compound size expressions must be grouped as `(size_expression)'(value)` in SV.
`code_CastTypes` additionally checks symbolic width
expressions.
