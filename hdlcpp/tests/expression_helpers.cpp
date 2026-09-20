#include <cstdint>
#include <cstdio>
#ifdef USE_VERILATOR
#include "Vexpression_helpers.h"
#else
#include "generated/expression_helpers.h"
long _system_clock = 0;
#endif

template<unsigned Mode>
bool check()
{
#ifdef USE_VERILATOR
    Vexpression_helpers model;
#else
    expression_helpers<Mode> model;
    cpphdl::logic<64> data;
    cpphdl::logic<2> word;
    model.data_i_in = _ASSIGN(data);
    model.word_i_in = _ASSIGN(word);
    model._assign();
#endif
    uint64_t state = 0x9e3779b97f4a7c15ull;
    for (unsigned sample = 0; sample < 256; ++sample) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        const uint64_t value = sample == 0 ? 0 : sample == 1 ? UINT64_MAX : state;
        const unsigned index = sample % 4;
#ifdef USE_VERILATOR
        model.data_i = value;
        model.word_i = index;
        model.eval();
        const unsigned selected = model.selected_o;
        const unsigned guarded = model.guarded_o;
        const uint64_t tree = model.tree_o;
#else
        data = value;
        word = index;
        ++_system_clock;
        const unsigned selected = uint64_t(model.selected_o_out());
        const unsigned guarded = uint64_t(model.guarded_o_out());
        const uint64_t tree = model.tree_o_out();
#endif
        if (selected != (value & (3u << (index * 2))) ||
            guarded != ((Mode == 1 ? value : ~value) & 255)) {
            std::fprintf(stderr, "mode=%u sample=%u selected=%u guarded=%u\n", Mode, sample, selected, guarded);
            return false;
        }
        for (unsigned level = 0; level < 8; ++level) {
            const unsigned expected = level == 0 ? value & 255 :
                (value & ((1u << (8 - level)) - 1)) | (1u << (8 - level));
            if (((tree >> (level * 8)) & 255) != expected) {
                std::fprintf(stderr, "tree mismatch sample=%u level=%u\n", sample, level);
                return false;
            }
        }
        for (unsigned span = 0; span < 64; ++span) {
            const uint64_t expected = span == 63 ? value :
                (value & ((uint64_t(1) << (span + 1)) - 1)) | (uint64_t(1) << (span + 1));
#ifdef USE_VERILATOR
            const uint64_t observed = uint64_t(model.prefix_o[2 * span]) |
                (uint64_t(model.prefix_o[2 * span + 1]) << 32);
#else
            const uint64_t observed = cpphdl::sv_bits_runtime(model.prefix_o_out(), span * 64 + 63, span * 64);
#endif
            if (observed != expected) {
                std::fprintf(stderr, "mode=%u sample=%u span=%u got=%llx expected=%llx\n", Mode, sample, span,
                    static_cast<unsigned long long>(observed), static_cast<unsigned long long>(expected));
                return false;
            }
        }
    }
    return true;
}

int main()
{
#ifdef USE_VERILATOR
    if (!check<TEST_MODE>()) return 1;
#else
    using Model = expression_helpers<1>;
    if (uint64_t(Model::__hdlcpp_concat_2({0x12, 8, 0x345, 0})) != 0x12 ||
        uint64_t(Model::__hdlcpp_concat_2({0x12, 8, UINT64_MAX, 64})) != UINT64_MAX ||
        uint64_t(Model::__hdlcpp_concat_2({0x12, 8, 0x345, 65})) != 0x345) return 1;
    if (!check<1>() || !check<2>()) return 1;
#endif
    std::puts("expression helpers: 256 patterns per mode, 64 concatenation widths passed");
}
