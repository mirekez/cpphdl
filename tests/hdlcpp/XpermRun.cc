#include "generated/Xperm.h"
#include <cstdio>
#ifdef XPERM_VERILATOR
#include "VXperm.h"
#endif
#ifdef XPERM_GRAPH
#include "model.h"
#endif
#ifndef TEST_XLEN
#define TEST_XLEN 32
#endif
long _system_clock = 0;

struct Driver : cpphdl::Module {
    Xperm<TEST_XLEN> dut;
    cpphdl::logic<TEST_XLEN> a, b;
    bool choose;
    int8_t signed_a, signed_b;
    void _assign() {
        dut.operand_a_in = _ASSIGN(a);
        dut.operand_b_in = _ASSIGN(b);
        dut.choose_in = _ASSIGN(choose);
        dut.signed_a_in = _ASSIGN(signed_a);
        dut.signed_b_in = _ASSIGN(signed_b);
        dut._assign();
    }
};

int main() {
    Driver driver;
    driver._assign();
#ifdef XPERM_VERILATOR
    VXperm rtl;
#endif
#ifdef XPERM_GRAPH
    cpphdl_native::Model graph;
    auto readWord = [](const auto& words) {
        uint64_t value = words[0];
        if (words.size() > 1) value |= uint64_t(words[1]) << 32;
        return value;
    };
#endif
    uint64_t random = 0x123456789abcdef0ull;
    for (unsigned sample = 0; sample < 4096; ++sample) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        const uint64_t a = sample == 0 ? 0 : sample == 1 ? UINT64_MAX : random;
        uint64_t b = 0;
        // Exhaust all 256 byte selectors, then mix valid and invalid selectors.
        for (unsigned lane = 0; lane < TEST_XLEN / 8; ++lane) {
            unsigned selector = sample < 256 ? sample :
                unsigned((random >> (lane * 8)) & 255) % (TEST_XLEN / 8 + 2);
            b |= uint64_t(selector) << (lane * 8);
        }
        driver.a = a; driver.b = b; driver.choose = sample & 1;
        driver.signed_a = int8_t(sample); driver.signed_b = int8_t(~sample);
        ++_system_clock;
#ifdef XPERM_GRAPH
        graph.operand_a[0] = a; graph.operand_b[0] = b;
        if constexpr (TEST_XLEN == 64) {
            graph.operand_a[1] = a >> 32; graph.operand_b[1] = b >> 32;
        }
        graph.choose[0] = driver.choose;
        graph.signed_a[0] = uint8_t(driver.signed_a);
        graph.signed_b[0] = uint8_t(driver.signed_b);
        graph.eval();
#endif
#ifdef XPERM_VERILATOR
        rtl.operand_a = a; rtl.operand_b = b; rtl.choose = driver.choose;
        rtl.signed_a = uint8_t(driver.signed_a); rtl.signed_b = uint8_t(driver.signed_b);
        rtl.eval();
#endif
        uint64_t expected = 0, reversed = 0, nested = 0;
        for (unsigned lane = 0; lane < TEST_XLEN / 8; ++lane) {
            const unsigned selector = (b >> (lane * 8)) & 255;
            const bool valid = selector < TEST_XLEN / 8;
            const uint64_t byte = valid ? (a >> (selector * 8)) & 255 : 0;
            const unsigned word = valid ? byte : 0xd00d;
            expected |= byte << (lane * 8);
            reversed |= (valid ? byte : 0xa5) << (lane * 8);
            nested |= (driver.choose ? (valid ? byte : 0x7e) : 0xc3) << (lane * 8);
            if (uint64_t(driver.dut.wide_o_out().bits(lane*16+15, lane*16)) != word) {
                std::fprintf(stderr, "C++ wide mismatch XLEN=%d sample=%u lane=%u\n", TEST_XLEN, sample, lane);
                return 1;
            }
#ifdef XPERM_GRAPH
            if (((graph.wide_o[lane / 2] >> ((lane % 2) * 16)) & 65535) != word) return 5;
#endif
#ifdef XPERM_VERILATOR
#if TEST_XLEN == 32
            if (((rtl.wide_o >> (lane*16)) & 65535) != word) return 2;
#else
            if (((rtl.wide_o[lane/2] >> ((lane%2)*16)) & 65535) != word) return 2;
#endif
#endif
        }
        const uint32_t whole = driver.choose ? a & 255 : 255;
        const uint16_t signed_result = int16_t(driver.choose ? driver.signed_a : driver.signed_b);
        uint64_t constant_result = 0x1122334455667788ull ^ 0xfedcba9876543210ull ^
            (driver.choose ? 0xa55a123456789abcull : 0x0123456789abcdefull);
        if constexpr (TEST_XLEN == 32) constant_result &= 0xffffffffull;
        if (uint64_t(driver.dut.result_o_out()) != expected ||
            uint64_t(driver.dut.reverse_o_out()) != reversed ||
            uint64_t(driver.dut.nested_o_out()) != nested ||
            uint32_t(driver.dut.whole_o_out()) != whole ||
            uint16_t(driver.dut.signed_o_out()) != signed_result ||
            uint64_t(driver.dut.constant_o_out()) != constant_result) {
            std::fprintf(stderr, "C++ mismatch XLEN=%d sample=%u got=%llx/%llx/%llx/%x/%x expected=%llx/%llx/%llx/%x/%x\n",
                TEST_XLEN, sample, (unsigned long long)uint64_t(driver.dut.result_o_out()),
                (unsigned long long)uint64_t(driver.dut.reverse_o_out()),
                (unsigned long long)uint64_t(driver.dut.nested_o_out()),
                uint32_t(driver.dut.whole_o_out()), uint16_t(driver.dut.signed_o_out()),
                (unsigned long long)expected, (unsigned long long)reversed,
                (unsigned long long)nested, whole, signed_result);
            return 3;
        }
#ifdef XPERM_VERILATOR
        if (rtl.result_o != expected || rtl.reverse_o != reversed || rtl.nested_o != nested ||
            rtl.whole_o != whole || rtl.signed_o != signed_result || rtl.constant_o != constant_result) {
            std::fprintf(stderr, "RTL mismatch XLEN=%d sample=%u\n", TEST_XLEN, sample);
            return 4;
        }
#endif
#ifdef XPERM_GRAPH
        if (readWord(graph.result_o) != expected || readWord(graph.reverse_o) != reversed ||
            readWord(graph.nested_o) != nested || graph.whole_o[0] != whole ||
            graph.signed_o[0] != signed_result || readWord(graph.constant_o) != constant_result) {
            std::fprintf(stderr, "graph mismatch XLEN=%d sample=%u\n", TEST_XLEN, sample);
            return 6;
        }
#endif
    }
    std::printf("Xperm XLEN=%d: 4096 samples passed\n", TEST_XLEN);
}
