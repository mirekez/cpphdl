#include "generated/indexed_comb_proof.h"
#ifdef CPPHDL_TEST_OPTIMIZED
using ProofModel = indexed_comb_proof<8>;
#include "ProofModel_optimized_combs.h"
#endif
#include <cstdio>

long _system_clock = 0;

template<unsigned Width>
struct CheckProof : indexed_comb_proof<Width> {
    static_assert(indexed_comb_proof<Width>::__cpphdl_complete_copied_o_comb);
    static_assert(!indexed_comb_proof<Width>::__cpphdl_complete_partial_o_comb);
    static_assert(indexed_comb_proof<Width>::__cpphdl_complete_prefix_comb);
};

int main()
{
    CheckProof<4> narrow;
    CheckProof<8> model;
    cpphdl::logic<8> data;
    cpphdl::logic<3> index = 0;
    model.data_i_in = [&]() { return &data; };
    model.index_i_in = [&]() { return &index; };
    model._assign();
#ifdef CPPHDL_TEST_OPTIMIZED
    bind_optimized_ports(model);
#endif
    for (unsigned value = 0; value < 256; ++value) {
        data = value;
        ++_system_clock;
#ifdef CPPHDL_TEST_OPTIMIZED
        calc_all(model);
#endif
        unsigned prefix = 0;
        unsigned parity = 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
            parity ^= (value >> bit) & 1;
            prefix |= parity << bit;
        }
        if (uint64_t(model.copied_o_out()) != value ||
            uint64_t(model.prefix_o_out()) != prefix) return 1;
    }
    std::puts("indexed comb proof: 256 input patterns passed");
}
