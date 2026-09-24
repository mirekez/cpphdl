#include "SliceWidths.cc"
#include <cstdio>
#ifdef SLICE_WIDTHS_RTL
#include "VSliceWidths.h"
#endif
#ifndef SLICE_WIDTH
#define SLICE_WIDTH 32
#endif
long _system_clock = 0;

struct Driver {
    SliceWidths<SLICE_WIDTH> dut;
    logic<512> data, data2;
    logic<4> lane;
    void _assign() {
        dut.data_in = _ASSIGN(data);
        dut.data2_in = _ASSIGN(data2);
        dut.lane_in = _ASSIGN(lane);
        dut._assign();
    }
};

int main() {
    Driver driver;
    uint32_t words[16], words2[16];
    uint32_t random = 0x93817abc;
    driver._assign();
#ifdef SLICE_WIDTHS_RTL
    VSliceWidths rtl;
#endif
    for (unsigned sample = 0; sample < 512; ++sample) {
        for (unsigned word = 0; word < 16; ++word) {
            random = random*1664525u+1013904223u;
            words[word] = sample == 0 ? 0 : sample == 1 ? ~0u : random;
            random = random*1664525u+1013904223u;
            words2[word] = sample == 0 ? ~0u : sample == 1 ? 0 : random;
            driver.data.bits(word*32+31, word*32) = words[word];
            driver.data2.bits(word*32+31, word*32) = words2[word];
#ifdef SLICE_WIDTHS_RTL
            rtl.data_in[word] = words[word]; rtl.data2_in[word] = words2[word];
#endif
        }
        for (unsigned lane = 0; lane < 16; ++lane) {
            driver.lane = lane;
            ++_system_clock;
            auto& dut = driver.dut;
            uint32_t parameter = 0;
            for (unsigned bit = 0; bit < SLICE_WIDTH; ++bit) {
                const unsigned index = lane*SLICE_WIDTH+bit;
                parameter |= ((words[index/32] >> (index%32)) & 1u) << bit;
            }
            const uint32_t expected[] = {words[lane], words2[lane], words[lane],
                words[lane] >> 16, words2[lane], parameter};
            const uint32_t native[] = {dut.first_out().raw, dut.second_out().raw,
                uint32_t(dut.next_out()), uint32_t(dut.offset_out()),
                uint32_t(dut.shifted_out()), uint32_t(dut.parameter_out())};
#ifdef SLICE_WIDTHS_RTL
            rtl.lane_in = lane;
            rtl.eval();
            const uint32_t other[] = {rtl.first_out, rtl.second_out, rtl.next_out,
                rtl.offset_out, rtl.shifted_out, rtl.parameter_out};
#else
            const auto& other = native;
#endif
            for (unsigned port = 0; port < 6; ++port) {
                if (native[port] != expected[port] || other[port] != expected[port]) {
                    std::fprintf(stderr, "slice width mismatch: width=%u sample=%u lane=%u port=%u\n",
                        SLICE_WIDTH, sample, lane, port);
                    return 1;
                }
            }
            for (unsigned word = 0; word < 16; ++word) {
                const uint32_t expected_word = word == lane ? words2[word] : words[word];
                if (uint32_t(dut.edited_out().bits(word*32+31, word*32)) != expected_word) return 2;
#ifdef SLICE_WIDTHS_RTL
                if (rtl.edited_out[word] != expected_word) return 3;
#endif
            }
        }
    }
    std::printf("slice widths: WIDTH=%u, 8192 read/write cases passed\n", SLICE_WIDTH);
}
