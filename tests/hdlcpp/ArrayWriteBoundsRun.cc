#include "generated/ArrayWriteBounds.h"
#include <cstdio>
#ifdef BOUNDS_GRAPH
#include "model.h"
#endif
#ifdef BOUNDS_RTL
#include "VArrayWriteBounds.h"
#endif

long _system_clock = 0;

int main() {
    constexpr uint64_t signalMask = (1ull << (5 * TEST_COUNT)) - 1;
    ArrayWriteBounds<TEST_COUNT, TEST_LOWER, TEST_ZERO> dut;
    cpphdl::logic<8> transaction;
    cpphdl::logic<64> index;
    cpphdl::logic<1> enabled, value, reset;
    cpphdl::logic<5 * TEST_COUNT> raw;
    dut.trans_id_i_in = _ASSIGN(transaction);
    dut.index_i_in = _ASSIGN(index);
    dut.wt_valid_i_in = _ASSIGN(enabled);
    dut.value_i_in = _ASSIGN(value);
    dut.rst_ni_in = _ASSIGN(reset);
    dut.raw_i_in = _ASSIGN(raw);
    dut._assign();
#ifdef BOUNDS_GRAPH
    cpphdl_native::Model model;
    auto fromWords = [](const auto& words) {
        uint64_t result = words[0];
        if (words.size() > 1) result |= uint64_t(words[1]) << 32;
        return result & signalMask;
    };
#endif
#ifdef BOUNDS_RTL
    VArrayWriteBounds rtl;
#endif
    uint64_t random = 0xdeadbeef1234;
    uint64_t registered = 0;
    unsigned samples = 0;
    for (unsigned sample = 0; sample < 256; ++sample) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        const uint64_t input = sample ? random & ((1ull << (5 * TEST_COUNT)) - 1) : 23;
        for (unsigned port = 0; port < TEST_COUNT; ++port) {
            const uint64_t indices[] = {uint64_t(TEST_LOWER + port), uint64_t(TEST_LOWER) - 1,
                TEST_LOWER + TEST_COUNT, (1ull << 32) + TEST_LOWER + port, ~0ull};
            for (auto destination : indices) {
                for (unsigned valid = 0; valid < 2; ++valid) {
                    const unsigned selected = TEST_ZERO ? 0 : port;
                    uint64_t expected = input;
                    if (valid && ((input >> (5 * selected)) & 23) == 23) {
                        expected |= 8ull << (5 * selected);
                        if (selected) expected |= 8ull << (5 * (selected - 1));
                    }
                    const bool running = samples++ % 97 != 0;
                    const unsigned assigned = (sample >> 2) & 1;
                    auto write = [&](uint64_t previous) {
                        if (!valid || destination < TEST_LOWER || destination >= TEST_LOWER + TEST_COUNT)
                            return previous;
                        const uint64_t mask = 8ull << (5 * (destination - TEST_LOWER));
                        return (previous & ~mask) | (assigned ? mask : 0);
                    };
                    const auto direct = write(input);
                    uint64_t wrapped = input;
                    const uint32_t wrappedIndex = uint32_t(destination) + uint32_t(1);
                    if (valid && wrappedIndex >= TEST_LOWER && wrappedIndex < TEST_LOWER + TEST_COUNT) {
                        const uint64_t mask = 8ull << (5 * (wrappedIndex - TEST_LOWER));
                        wrapped = (input & ~mask) | (assigned ? mask : 0);
                    }
                    uint64_t casted = input;
                    if (valid && destination >= TEST_LOWER && destination < TEST_LOWER + TEST_COUNT) {
                        const unsigned shift = 5 * (destination - TEST_LOWER);
                        casted = (input & ~(31ull << shift)) |
                            (((assigned ? input : ~input) & 31) << shift);
                    }
                    registered = running ? write(registered) : 0;
                    raw = input; transaction = port; index = destination;
                    enabled = valid; value = assigned; reset = running;
                    ++_system_clock;
                    dut._work(!running);
                    dut._strobe();
                    ++_system_clock;
                    bool good = (uint64_t(dut.result_o_out()) & signalMask) == expected &&
                        (uint64_t(dut.wrap_o_out()) & signalMask) == wrapped &&
                        (uint64_t(dut.cast_o_out()) & signalMask) == casted &&
                        (uint64_t(dut.direct_o_out()) & signalMask) == direct &&
                        (uint64_t(dut.registered_o_out()) & signalMask) == registered;
#ifdef BOUNDS_GRAPH
                    model.raw_i[0] = input;
                    if (model.raw_i.size() > 1) model.raw_i[1] = input >> 32;
                    model.trans_id_i[0] = port;
                    model.index_i[0] = destination; model.index_i[1] = destination >> 32;
                    model.wt_valid_i[0] = valid; model.value_i[0] = assigned;
                    model.rst_ni[0] = running; model.work_reset[0] = !running;
                    model.step();
                    good &= fromWords(model.result_o) == expected && fromWords(model.direct_o) == direct &&
                        fromWords(model.wrap_o) == wrapped &&
                        fromWords(model.cast_o) == casted &&
                        fromWords(model.registered_o) == registered;
#endif
#ifdef BOUNDS_RTL
                    rtl.raw_i = input; rtl.trans_id_i = port; rtl.index_i = destination;
                    rtl.wt_valid_i = valid; rtl.value_i = assigned; rtl.rst_ni = running;
                    rtl.clk_i = 0; rtl.eval(); rtl.clk_i = 1; rtl.eval();
                    good &= (uint64_t(rtl.result_o) & signalMask) == expected &&
                        (uint64_t(rtl.wrap_o) & signalMask) == wrapped &&
                        (uint64_t(rtl.cast_o) & signalMask) == casted &&
                        (uint64_t(rtl.direct_o) & signalMask) == direct &&
                        (uint64_t(rtl.registered_o) & signalMask) == registered;
#endif
                    if (!good) {
                        std::fprintf(stderr, "array write mismatch: sample=%u id=%u index=%llu valid=%u\n",
                            sample, port, (unsigned long long)destination, valid);
                        std::fprintf(stderr, "C++ result=%llx/%llx direct=%llx/%llx registered=%llx/%llx\n",
                            (unsigned long long)uint64_t(dut.result_o_out()), (unsigned long long)expected,
                            (unsigned long long)uint64_t(dut.direct_o_out()), (unsigned long long)direct,
                            (unsigned long long)uint64_t(dut.registered_o_out()), (unsigned long long)registered);
#ifdef BOUNDS_RTL
                        std::fprintf(stderr, "RTL result=%llx direct=%llx registered=%llx\n",
                            (unsigned long long)rtl.result_o, (unsigned long long)rtl.direct_o,
                            (unsigned long long)rtl.registered_o);
#endif
                        return 1;
                    }
                }
            }
        }
    }
    std::printf("%u bounded writes pass: count=%u lower=%u constant_id=%u\n",
        samples, TEST_COUNT, TEST_LOWER, TEST_ZERO);
}
