#pragma once
#include <cpphdl.h>
#include "../../../lib/math/Q48Math.h"

// The HLS blackbox ABI packs the first argument into the low 64 bits.
// These are real combinational RTL implementations, not simulation callbacks.
template<unsigned INPUT_BITS = 128, unsigned OUTPUT_BITS = 64>
class LlmQ48Divide : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<INPUT_BITS>) args_in;
    _PORT(cpphdl::logic<OUTPUT_BITS>) result_out = _ASSIGN(calculate());
private:
    uint64_t calculate() {
        return uint64_t(cpphdl_examples::math::Q48Math::divide(
            int64_t(uint64_t(args_in())), int64_t(uint64_t(args_in() >> 64))));
    }
};

template<unsigned INPUT_BITS = 64, unsigned OUTPUT_BITS = 64>
class LlmQ48ExpNegative : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<INPUT_BITS>) args_in;
    _PORT(cpphdl::logic<OUTPUT_BITS>) result_out = _ASSIGN(calculate());
private:
    uint64_t calculate() {
        return uint64_t(cpphdl_examples::math::Q48Math::exp_negative(int64_t(uint64_t(args_in()))));
    }
};

template<unsigned INPUT_BITS = 64, unsigned OUTPUT_BITS = 64>
class LlmQ48InverseSqrt : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<INPUT_BITS>) args_in;
    _PORT(cpphdl::logic<OUTPUT_BITS>) result_out = _ASSIGN(calculate());
private:
    uint64_t calculate() {
        return uint64_t(cpphdl_examples::math::Q48Math::inverse_sqrt(int64_t(uint64_t(args_in()))));
    }
};

template<unsigned INPUT_BITS = 64, unsigned OUTPUT_BITS = 64>
class LlmQ48Silu : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<INPUT_BITS>) args_in;
    _PORT(cpphdl::logic<OUTPUT_BITS>) result_out = _ASSIGN(calculate());
private:
    uint64_t calculate() {
        return uint64_t(cpphdl_examples::math::Q48Math::silu(int64_t(uint64_t(args_in()))));
    }
};

// An ordinary RTL parent instantiates all four concrete blackbox signatures.
class MathRtl : public cpphdl::Module {
    LlmQ48Divide<> division;
    LlmQ48ExpNegative<> exponential;
    LlmQ48InverseSqrt<> inverse;
    LlmQ48Silu<> activation;
public:
    _PORT(uint64_t) lhs_in;
    _PORT(uint64_t) rhs_in;
    _PORT(uint64_t) divide_out = _ASSIGN(uint64_t(division.result_out()));
    _PORT(uint64_t) exp_out = _ASSIGN(uint64_t(exponential.result_out()));
    _PORT(uint64_t) inverse_out = _ASSIGN(uint64_t(inverse.result_out()));
    _PORT(uint64_t) silu_out = _ASSIGN(uint64_t(activation.result_out()));
    void _assign() {
        division.args_in = _ASSIGN((cpphdl::logic<128>(rhs_in()) << 64) | cpphdl::logic<128>(lhs_in()));
        exponential.args_in = _ASSIGN(lhs_in());
        inverse.args_in = _ASSIGN(lhs_in());
        activation.args_in = _ASSIGN(lhs_in());
    }
};
