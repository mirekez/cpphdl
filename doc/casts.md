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

Reference/qualification casts keep the selected object assignable instead
of converting it into an SV temporary. This includes `const_cast`, identity
`reinterpret_cast`, and compile-time hierarchy casts (including a
`dynamic_cast` upcast that does not require RTTI). Existing pointer views of
packed storage are structural views, not runtime C++ addresses. Casting to
`void` retains operand side effects.

This is not a runtime C++ object model. Runtime RTTI/downcasts,
pointer/integer conversions, type-punning reference casts, member-pointer
casts, and floating-point conversions are diagnosed as unsupported with a
nonzero converter exit. Use explicit bit-vector operations or a hardware
floating-point implementation for those operations. A cast is not a generic
way to synthesize arbitrary constructors or conversion operators.

`cast_cpp` and `cast_verilator` run the same cast matrix, with boundary,
walking-bit and deterministic random inputs. The C++ run uses ASan/UBSan;
the RTL run compares each output against the C++ model. `cast_reject`
checks that unsupported examples compile as C++ but fail RTL conversion
with the intended diagnostic. `cast_parameter_widths` compares C++ and RTL
with widths 1, 9, and 33, overriding the same generated module's parameter
without regenerating it. `code_CastTypes` additionally checks symbolic width
expressions.
