#include "WideSlice.cc"
#include <cstdio>
#ifdef WIDE_GRAPH
#include "model.h"
#endif
#ifdef WIDE_RTL
#include "VWideSlice.h"
#endif

long _system_clock = 0;
struct WideSliceDriver {
    WideSlice<WIDE_LINE, WIDE_SELECT, WIDE_FIXED> dut;
    logic<WIDE_LINE> line;
    logic<16> first;
    void _assign() {
        dut.line_in = _ASSIGN(line);
        dut.first_in = _ASSIGN(first);
        dut._assign();
    }
};

template<unsigned Width, class T>
unsigned rtlBit(const T& data, unsigned bit) {
    if constexpr (Width <= 64) return (uint64_t(data) >> bit) & 1;
    else return (data[bit/32] >> (bit%32)) & 1;
}
template<unsigned Width, class T>
void rtlInput(T& data, const uint32_t* words) {
    if constexpr (Width <= 32) data = words[0];
    else if constexpr (Width <= 64) data = words[0] | (uint64_t(words[1]) << 32);
    else for (unsigned i = 0; i < (Width+31)/32; ++i) data[i] = words[i];
}

int main() {
    WideSliceDriver driver;
    uint32_t words[(WIDE_LINE+31)/32];
    uint32_t random = 0x975132ab;
    unsigned samples = 0;
    driver._assign();
#ifdef WIDE_GRAPH
    cpphdl_native::Model model;
#endif
#ifdef WIDE_RTL
    VWideSlice rtl;
#endif
    for (unsigned pattern = 0; pattern < 64; ++pattern) {
        for (unsigned word = 0; word < (WIDE_LINE+31)/32; ++word) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            words[word] = pattern == 0 ? 0 : pattern == 1 ? ~0u :
                pattern == 2 ? (word >= 2 ? ~0u : 0) : pattern == 3 ? 0xaaaaaaaa : random;
        }
        if constexpr (WIDE_LINE%32) words[WIDE_LINE/32] &= (1u << (WIDE_LINE%32))-1;
        for (unsigned bit = 0; bit < WIDE_LINE; ++bit)
            driver.line.set(bit, (words[bit/32] >> (bit%32)) & 1);
#ifdef WIDE_GRAPH
        for (unsigned word = 0; word < (WIDE_LINE+31)/32; ++word) model.line[word] = words[word];
#endif
#ifdef WIDE_RTL
        rtlInput<WIDE_LINE>(rtl.line_in, words);
#endif
        for (unsigned offset = 0; offset <= WIDE_LINE-WIDE_SELECT; ++offset) {
            const unsigned first = WIDE_FIXED ? 32 : offset;
            driver.first = offset;
            ++_system_clock;
#ifdef WIDE_GRAPH
            model.first[0] = offset;
            model.eval();
#endif
#ifdef WIDE_RTL
            rtl.first_in = offset;
            rtl.eval();
#endif
            const auto data = driver.dut.data_out();
            const auto const_data = driver.dut.const_data_out();
            const auto padded = driver.dut.padded_out();
            for (unsigned bit = 0; bit < WIDE_LINE; ++bit) {
                const unsigned expected = bit < WIDE_SELECT ?
                    (words[(first+bit)/32] >> ((first+bit)%32)) & 1 : 0;
                bool good = unsigned(padded.get(bit)) == expected;
                if (bit < WIDE_SELECT)
                    good &= unsigned(data.get(bit)) == expected && unsigned(const_data.get(bit)) == expected;
#ifdef WIDE_GRAPH
                good &= ((model.padded[bit/32] >> (bit%32)) & 1) == expected;
                if (bit < WIDE_SELECT)
                    good &= ((model.data[bit/32] >> (bit%32)) & 1) == expected &&
                        ((model.const_data[bit/32] >> (bit%32)) & 1) == expected;
#endif
#ifdef WIDE_RTL
                good &= rtlBit<WIDE_LINE>(rtl.padded_out, bit) == expected;
                if (bit < WIDE_SELECT)
                    good &= rtlBit<WIDE_SELECT>(rtl.data_out, bit) == expected &&
                        rtlBit<WIDE_SELECT>(rtl.const_data_out, bit) == expected;
#endif
                if (!good) {
                    std::fprintf(stderr, "slice mismatch: line=%u select=%u fixed=%u pattern=%u first=%u bit=%u\n",
                        WIDE_LINE, WIDE_SELECT, WIDE_FIXED, pattern, first, bit);
                    return 1;
                }
            }
            ++samples;
        }
    }
    std::printf("wide slice: %u -> %u bits, fixed=%u, %u oracle samples passed\n",
        WIDE_LINE, WIDE_SELECT, WIDE_FIXED, samples);
}
