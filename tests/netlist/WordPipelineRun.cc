#include "model.h"
#include <array>
#include <cstdint>
#include <cstdio>

int main()
{
    cxxrtl_design::p_WordPipeline model;
    std::array<bool, 65> history{};
    uint64_t random = 0x96562b6f9717a073ull;
    for (unsigned cycle = 0; cycle < 20000; ++cycle) {
        for (auto& word : model.p_payload.data) {
            random ^= random << 13;
            random ^= random >> 7;
            random ^= random << 17;
            word = uint32_t(random);
        }
        model.p_payload.data[16] &= 255;
        const unsigned select = cycle % 8;
        const bool reset = cycle % 127 == 0;
        const bool enable = cycle % 3 != 0;
        model.p_clk.set<bool>(false);
        model.p_reset.set<bool>(reset);
        model.p_enable.set<bool>(enable);
        model.p_select.set<uint32_t>(select);
        model.step();
        std::array<bool, 65> selected{};
        for (unsigned index = 0; index < 65; ++index) {
            const unsigned offset = select * 65 + index;
            selected[index] = (model.p_payload.data[offset / 32] >> (offset % 32)) & 1;
        }
        for (unsigned index = 0; index < 65; ++index) {
            const bool expected_shift = selected[index + select < 65 ? index + select : 64];
            if (model.p_selected.bit(index) != selected[index] ||
                model.p_history.curr.bit(index) != history[index] ||
                model.p_pair.bit(index) != selected[index] ||
                model.p_pair.bit(index + 65) != history[index] ||
                model.p_shifted.bit(index) != expected_shift) {
                std::fprintf(stderr, "word pipeline mismatch cycle=%u bit=%u\n", cycle, index);
                return 1;
            }
        }
        model.p_clk.set<bool>(true);
        model.step();
        model.step();
        for (unsigned index = 0; index < 65; ++index) {
            if (reset) history[index] = false;
            else if (enable) history[index] ^= selected[index];
            if (model.p_history.curr.bit(index) != history[index]) return 2;
        }
    }
    std::puts("word pipeline: 20000 cycles match the independent bit oracle");
}
