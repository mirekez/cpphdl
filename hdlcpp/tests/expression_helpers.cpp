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
    cpphdl::logic<6> slice;
    uint32_t native;
    model.data_i_in = _ASSIGN(data);
    model.word_i_in = _ASSIGN(word);
    model.slice_i_in = _ASSIGN(slice);
    model.native_i_in = _ASSIGN(native);
    model._assign();
#endif
    uint64_t state = 0x9e3779b97f4a7c15ull;
    for (unsigned sample = 0; sample < 256; ++sample) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        const uint64_t value = sample == 0 ? 0 : sample == 1 ? UINT64_MAX : state;
        const unsigned index = sample % 4;
        const unsigned offset = sample % 64;
#ifdef USE_VERILATOR
        model.data_i = value;
        model.word_i = index;
        model.slice_i = offset;
        model.native_i = uint32_t(value);
        model.eval();
        const unsigned selected = model.selected_o;
        const unsigned guarded = model.guarded_o;
        const uint64_t tree = model.tree_o;
        const uint64_t prefix_and = model.prefix_and_o;
        const uint64_t suffix_and = model.suffix_and_o;
        const unsigned native_byte = model.native_o;
#else
        data = value;
        word = index;
        slice = offset;
        native = uint32_t(value);
        ++_system_clock;
        const unsigned selected = uint64_t(model.selected_o_out());
        const unsigned guarded = uint64_t(model.guarded_o_out());
        const uint64_t tree = model.tree_o_out();
        const uint64_t prefix_and = model.prefix_and_o_out();
        const uint64_t suffix_and = model.suffix_and_o_out();
        const unsigned native_byte = uint64_t(model.native_o_out());
#endif
        if (native_byte != (uint32_t(value) >> 24)) {
            std::fprintf(stderr, "native slice mismatch sample=%u\n", sample);
            return false;
        }
        for (unsigned bit = 0; bit < 65; ++bit) {
            const unsigned source = offset + bit;
            const bool expected = ((source < 64 ? ~value : value) >> (source % 64)) & 1;
#ifdef USE_VERILATOR
            const bool upward = (model.wide_o[bit / 32] >> (bit % 32)) & 1;
            const bool downward = (model.down_o[bit / 32] >> (bit % 32)) & 1;
#else
            const bool upward = model.wide_o_out().get(bit);
            const bool downward = model.down_o_out().get(bit);
#endif
            if (upward != expected || downward != expected) {
                std::fprintf(stderr, "wide slice mismatch sample=%u bit=%u up=%u down=%u expected=%u\n",
                    sample, bit, upward, downward, expected);
                return false;
            }
        }
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
            const uint64_t prefix_mask = UINT64_MAX >> (63 - span);
            const uint64_t suffix_mask = UINT64_MAX << span;
            if (((prefix_and >> span) & 1) != ((value & prefix_mask) == prefix_mask) ||
                ((suffix_and >> span) & 1) != ((value & suffix_mask) == suffix_mask)) {
                std::fprintf(stderr, "slice reduction mismatch sample=%u span=%u prefix=%llx suffix=%llx\n",
                    sample, span, (unsigned long long)prefix_and, (unsigned long long)suffix_and);
                return false;
            }
            const uint64_t expected = span == 63 ? value :
                (value & ((uint64_t(1) << (span + 1)) - 1)) | (uint64_t(1) << (span + 1));
#ifdef USE_VERILATOR
            const uint64_t observed = uint64_t(model.prefix_o[2 * span]) |
                (uint64_t(model.prefix_o[2 * span + 1]) << 32);
#else
            const uint64_t observed = uint64_t(model.prefix_o_out().bits(span * 64 + 63, span * 64));
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
