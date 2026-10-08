#include "WideWrite.cc"
#include <array>
#include <cstdio>
#ifdef WRITE_GRAPH
#include "model.h"
#endif
#ifdef WRITE_RTL
#include "VWideWrite.h"
#endif

long _system_clock = 0;
struct Driver {
    WideWrite<WRITE_LINE, WRITE_SLICE, WRITE_DATA, WRITE_ALIGNED> dut;
    logic<1> word, enable, overlap;
    logic<16> first, second;
    logic<WRITE_DATA> data, data2;
    void _assign() {
        dut.word_in = _ASSIGN(word);
        dut.enable_in = _ASSIGN(enable);
        dut.overlap_in = _ASSIGN(overlap);
        dut.first_in = _ASSIGN(first);
        dut.second_in = _ASSIGN(second);
        dut.data_in = _ASSIGN(data);
        dut.data2_in = _ASSIGN(data2);
        dut._assign();
    }
};
template<unsigned Width, class T>
unsigned rtlBit(const T& data, unsigned bit) {
    if constexpr (Width <= 64) return (uint64_t(data) >> bit) & 1;
    else return (data[bit/32] >> (bit%32)) & 1;
}
template<unsigned Width, class T, class Words>
void rtlInput(T& data, const Words& words) {
    if constexpr (Width <= 32) data = words[0];
    else if constexpr (Width <= 64) data = words[0] | (uint64_t(words[1]) << 32);
    else for (unsigned i = 0; i < (Width+31)/32; ++i) data[i] = words[i];
}
int main() {
    Driver driver;
    std::array<uint8_t, WRITE_LINE> expected{};
    std::array<uint32_t, (WRITE_DATA+31)/32> words{}, words2{};
    uint32_t random = 0x795183ab;
#ifdef WRITE_GRAPH
    cpphdl_native::Model graph;
#endif
#ifdef WRITE_RTL
    VWideWrite rtl;
#endif
    driver._assign();
    for (unsigned sample = 0; sample < 4096; ++sample) {
        const bool reset = sample % 257 == 0;
        const bool enable = sample % 7 != 0;
        const bool overlap = (sample & 2) != 0;
        const unsigned word = sample & 1;
        const unsigned first = sample % (WRITE_LINE - WRITE_SLICE + 1);
        const unsigned second = sample % 4 == 0 ? first : (sample * 37 + 7) % (WRITE_LINE - WRITE_SLICE + 1);
        const unsigned low = WRITE_ALIGNED ? word*64 : first;
        const unsigned low2 = WRITE_ALIGNED ? (1-word)*64 : second;
        for (unsigned i = 0; i < words.size(); ++i) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            words[i] = sample % 17 == 0 ? ~0u : sample % 19 == 0 ? 0 : random;
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            words2[i] = random;
        }
        if constexpr (WRITE_DATA%32) {
            words.back() &= (1u << (WRITE_DATA%32))-1;
            words2.back() &= (1u << (WRITE_DATA%32))-1;
        }
        for (unsigned bit = 0; bit < WRITE_DATA; ++bit) {
            driver.data.set(bit, (words[bit/32] >> (bit%32)) & 1);
            driver.data2.set(bit, (words2[bit/32] >> (bit%32)) & 1);
        }
        if (enable) {
            for (unsigned bit = 0; bit < WRITE_SLICE; ++bit)
                expected[low+bit] = bit < WRITE_DATA ? (words[bit/32] >> (bit%32)) & 1 : 0;
            if (overlap) for (unsigned bit = 0; bit < WRITE_SLICE; ++bit)
                expected[low2+bit] = bit < WRITE_DATA ? (words2[bit/32] >> (bit%32)) & 1 : 0;
        }
        if (reset) expected.fill(0);
        driver.word = word; driver.enable = enable; driver.overlap = overlap;
        driver.first = first; driver.second = second;
        ++_system_clock;
#ifdef WRITE_RTL
        rtl.clk = 0; rtl.reset = reset;
        rtl.word_in = word; rtl.enable_in = enable; rtl.overlap_in = overlap;
        rtl.first_in = first; rtl.second_in = second;
        rtlInput<WRITE_DATA>(rtl.data_in, words);
        rtlInput<WRITE_DATA>(rtl.data2_in, words2);
        rtl.eval();
#endif
        driver.dut._work(reset);
#ifdef WRITE_RTL
        rtl.clk = 1; rtl.eval();
#endif
        driver.dut._strobe();
        ++_system_clock;
#ifdef WRITE_GRAPH
        graph.word[0] = word; graph.enable[0] = enable; graph.overlap[0] = overlap;
        graph.first[0] = first; graph.second[0] = second; graph.work_reset[0] = reset;
        graph.data = words; graph.data2 = words2;
        graph.step();
#endif
#ifdef WRITE_RTL
        rtl.clk = 0; rtl.eval();
#endif
        const auto result = driver.dut.result_out();
        const auto observed = driver.dut.observed_out();
        for (unsigned bit = 0; bit < WRITE_LINE; ++bit) {
            const unsigned selected = bit < WRITE_SLICE && !reset ? expected[low+bit] : 0;
            bool good = unsigned(result.get(bit)) == expected[bit];
            if (bit < WRITE_SLICE) good &= unsigned(observed.get(bit)) == selected;
#ifdef WRITE_GRAPH
            good &= ((graph.result[bit/32] >> (bit%32)) & 1) == expected[bit];
            if (bit < WRITE_SLICE) good &= ((graph.observed[bit/32] >> (bit%32)) & 1) == selected;
#endif
#ifdef WRITE_RTL
            good &= rtlBit<WRITE_LINE>(rtl.result_out, bit) == expected[bit];
            if (bit < WRITE_SLICE) good &= rtlBit<WRITE_SLICE>(rtl.observed_out, bit) == selected;
#endif
            if (!good) {
                std::fprintf(stderr, "wide write mismatch: line=%u slice=%u data=%u aligned=%u sample=%u bit=%u\n",
                    WRITE_LINE, WRITE_SLICE, WRITE_DATA, WRITE_ALIGNED, sample, bit);
                return 1;
            }
        }
    }
    std::printf("wide writes: line=%u slice=%u data=%u aligned=%u, 4096 oracle samples passed\n",
        WRITE_LINE, WRITE_SLICE, WRITE_DATA, WRITE_ALIGNED);
}
