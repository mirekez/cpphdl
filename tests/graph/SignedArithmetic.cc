#include "cpphdl.h"
#include <cstdint>

class SignedArithmetic : public cpphdl::Module {
public:
    _PORT(int64_t) numerator_in;
    _PORT(int64_t) denominator_in;
    _PORT(int64_t) quotient_out = _ASSIGN(quotient());
    _PORT(int64_t) remainder_out = _ASSIGN(remainder());
    int64_t quotient() {
        if (!denominator_in()) return 0;
        if (numerator_in() == INT64_MIN && denominator_in() == -1) return INT64_MIN;
        return numerator_in() / denominator_in();
    }
    int64_t remainder() {
        if (!denominator_in() || (numerator_in() == INT64_MIN && denominator_in() == -1)) return 0;
        return numerator_in() % denominator_in();
    }
};
SignedArithmetic cpphdl_top;

#ifdef SIGNED_ARITHMETIC_RUN
#include "model.h"
#include "cpphdl_graph.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    int64_t numerator = 0, denominator = 0;
    const int64_t values[] = {INT64_MIN, INT64_MIN + 1, -65537, -129, -128, -7, -1,
                             0, 1, 2, 7, 127, 128, 65537, INT64_MAX};
    cpphdl_native::Model model;
    cpphdl_top.numerator_in = _ASSIGN(numerator);
    cpphdl_top.denominator_in = _ASSIGN(denominator);
    for (auto a : values) for (auto b : values) {
        numerator = a; denominator = b;
        model.numerator[0] = uint32_t(a); model.numerator[1] = uint64_t(a) >> 32;
        model.denominator[0] = uint32_t(b); model.denominator[1] = uint64_t(b) >> 32;
        model.eval();
        auto quotient = uint64_t(model.quotient[0]) | uint64_t(model.quotient[1]) << 32;
        auto remainder = uint64_t(model.remainder[0]) | uint64_t(model.remainder[1]) << 32;
        if (quotient != uint64_t(cpphdl_top.quotient_out()) || remainder != uint64_t(cpphdl_top.remainder_out()))
            return 1;
        // Check constant folding, including the explicitly defined graph edges.
        if (cpphdl::graph::Graph::calculate("sdiv", uint64_t(a), uint64_t(b), 64) != quotient ||
            cpphdl::graph::Graph::calculate("smod", uint64_t(a), uint64_t(b), 64) != remainder) return 2;
        ++_system_clock;
    }
    std::puts("signed arithmetic: 225 boundary pairs match native graph and constant folding");
}
#endif
