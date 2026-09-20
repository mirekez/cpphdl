#include "model.h"
#include <array>
#include <cstdint>
#include <cstdio>

static uint64_t randomState = 0x156847abcdull;
static uint32_t randomWord() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 7;
    randomState ^= randomState << 17;
    return uint32_t(randomState);
}
static uint32_t transform(uint32_t value) {
    return ((value << 24) | (value >> 8)) ^ 0x5a00u;
}

int main() {
    cpphdl_native::Model model;
    std::array<uint32_t, 5> bank{};
    uint32_t counter = 0;
    bool previousClock = false, previousReset = true;
    for (unsigned sample = 0; sample < 20000; ++sample) {
        const bool clock = randomWord() & 1;
        const bool reset = sample > 0 && (randomWord() % 11) != 0;
        const bool enable = randomWord() & 1;
        const unsigned index = randomWord() % 8;
        const unsigned mode = randomWord() % 4;
        const unsigned shift = randomWord() % 128;
        std::array<uint32_t, 5> inputs;
        for (auto& word : inputs) word = randomWord();
        model.clk[0] = clock;
        model.reset_n[0] = reset;
        model.enable[0] = enable;
        model.index[0] = index;
        model.mode[0] = mode;
        model.shift[0] = shift;
        model.inputs = inputs;
        if ((!previousClock && clock) || (previousReset && !reset)) {
            if (!reset) {
                counter = 0xfffffff0u;
                for (unsigned lane = 0; lane < 5; ++lane) bank[lane] = 0x89abcdefu + lane;
            } else if (enable) {
                ++counter;
                if (index < 5)
                    bank[index] = (transform(inputs[index]) & 0xffff0000u) |
                                  ((inputs[index] & 255u) << 8) | (bank[index] >> 24);
            }
        }
        previousClock = clock;
        previousReset = reset;
        const uint32_t selected = index < 5 ? inputs[index] : 0;
        uint32_t mixed = mode == 0 ? transform(selected) : mode == 1 ? selected ^ 0x01234567u :
                         mode == 2 ? (enable ? selected + 9u : selected - 11u) : 0;
        for (unsigned offset = 0; offset < 4; ++offset)
            mixed = (mixed & ~(7u << (offset * 8))) | (((selected >> (offset * 3)) & 7u) << (offset * 8));
        int32_t amount = int32_t((selected >> 16) & 511u);
        if (amount & 256) amount -= 512;
        const uint32_t shifted = uint32_t(amount >> (shift < 32 ? shift : 31)) & 511u;
        const uint32_t extended = (selected & 15u) | ((selected & 8u) ? 4080u : 0u);
        uint32_t reversed = 0;
        for (unsigned bit = 0; bit < 4; ++bit) reversed |= ((selected >> bit) & 1u) << (3-bit);
        const uint32_t unpacked = index >= 2 && index <= 6 ? inputs[index-2] & 255u : 0;
        uint32_t equal = 0;
        for (unsigned lane = 0; lane < 5; ++lane) equal |= uint32_t(bank[lane] == inputs[lane]) << lane;
        model.step();
        if (model.bank_out != bank || model.count_out[0] != counter || model.selected_out[0] != selected ||
            model.mixed_out[0] != mixed || model.shifted[0] != shifted || model.equal_flags[0] != equal ||
            model.extended[0] != extended || model.reversed[0] != reversed || model.unpacked_select[0] != unpacked) {
            std::fprintf(stderr, "native graph mismatch sample=%u selected=%08x/%08x mixed=%08x/%08x shift=%x/%x count=%x/%x\n",
                         sample,model.selected_out[0],selected,model.mixed_out[0],mixed,model.shifted[0],shifted,model.count_out[0],counter);
            return 1;
        }
    }
    std::puts("native graph: 20000 randomized event/reset/field/NBA samples passed");
}
