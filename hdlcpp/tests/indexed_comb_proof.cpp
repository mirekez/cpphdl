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
    static_assert(indexed_comb_proof<Width>::__cpphdl_complete_tree_comb);
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
        unsigned tree = value & 1;
        for (unsigned level = 0; level < 3; ++level) {
            for (unsigned node = 0; node < (1u << level); ++node) {
                const unsigned parent = (tree >> ((1u << level) - 1 + node)) & 1;
                const unsigned child = (1u << (level + 1)) - 1 + 2 * node;
                tree |= (parent ^ ((value >> (level + 1)) & 1)) << child;
                tree |= (parent ^ ((value >> (level + 2)) & 1)) << (child + 1);
            }
        }
        const auto actual_copy = uint64_t(model.copied_o_out());
        const auto actual_prefix = uint64_t(model.prefix_o_out());
        const auto actual_tree = uint64_t(model.tree_o_out());
        if (actual_copy != value || actual_prefix != prefix || actual_tree != tree) {
            std::fprintf(stderr,
                "indexed comb proof: input=%u copied=%llu (expected %u), "
                "prefix=%llu (expected %u), tree=%llu (expected %u)\n",
                value, static_cast<unsigned long long>(actual_copy), value,
                static_cast<unsigned long long>(actual_prefix), prefix,
                static_cast<unsigned long long>(actual_tree), tree);
            return 1;
        }
    }
    std::puts("indexed comb proof: 256 input patterns passed");
}
