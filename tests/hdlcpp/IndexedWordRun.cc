#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#ifndef INDEX_WIDTH
#define INDEX_WIDTH 128
#endif
#ifdef INDEX_GRAPH
#include "model.h"
#elif defined(INDEX_RTL)
#include "VIndexedWord.h"
#else
#include "generated/IndexedWord.h"
long _system_clock = 0;
#endif

int main(int argc, char**) {
#ifdef INDEX_GRAPH
    cpphdl_native::Model model;
#elif defined(INDEX_RTL)
    VIndexedWord model;
#else
    IndexedWord<INDEX_WIDTH> model;
    cpphdl::logic<INDEX_WIDTH> data;
    cpphdl::logic<64> index;
    cpphdl::logic<1> enable, bitValue;
    model.data_in = _ASSIGN(data);
    model.index_in = _ASSIGN(index);
    model.enable_in = _ASSIGN(enable);
    model.bit_value_in = _ASSIGN(bitValue);
#endif
    constexpr unsigned words = (INDEX_WIDTH + 31) / 32;
    uint64_t random = 0x8e7c39a1589b4521ull, checksum = 0;
    unsigned samples = argc > 1 ? 2000000 : 4096;
    auto started = std::chrono::steady_clock::now();
    for (unsigned sample = 0; sample < samples; ++sample) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        std::array<uint32_t, words> source{};
        for (unsigned word = 0; word < words; ++word)
            source[word] = uint32_t(random >> ((word % 2) * 32)) ^ (word * 0x9e3779b9u);
        if constexpr (INDEX_WIDTH % 32) source.back() &= (1u << (INDEX_WIDTH % 32)) - 1;
        uint64_t selectedIndex = sample % 31 == 0 ? UINT64_MAX : (random >> 32) % (INDEX_WIDTH + 17);
        bool enabled = sample % 3 != 0, replacement = random & 1;
        auto expected = source;
        bool selected = selectedIndex < INDEX_WIDTH && (source[selectedIndex / 32] >> (selectedIndex % 32)) & 1;
        if (enabled && selectedIndex < INDEX_WIDTH) {
            uint32_t mask = 1u << (selectedIndex % 32);
            expected[selectedIndex / 32] = (expected[selectedIndex / 32] & ~mask) | (replacement ? mask : 0);
        }
        bool changedBit = selectedIndex < INDEX_WIDTH && (expected[selectedIndex / 32] >> (selectedIndex % 32)) & 1;
#ifdef INDEX_GRAPH
        model.data = source;
        model.index = {uint32_t(selectedIndex), uint32_t(selectedIndex >> 32)};
        model.enable[0] = enabled;
        model.bit_value[0] = replacement;
        model.eval();
        assert(model.changed == expected && model.selected[0] == selected && model.selected_changed[0] == changedBit);
        checksum = checksum * 131 + model.changed[0] + model.changed.back() + model.selected[0];
#elif defined(INDEX_RTL)
#if INDEX_WIDTH <= 64
        model.data = source[0];
        if constexpr (INDEX_WIDTH > 32) model.data |= uint64_t(source[1]) << 32;
#else
        for (unsigned word = 0; word < words; ++word) model.data[word] = source[word];
#endif
        model.index = selectedIndex; model.enable = enabled; model.bit_value = replacement;
        model.eval();
#if INDEX_WIDTH <= 64
        assert(uint32_t(model.changed) == expected[0]);
        if constexpr (INDEX_WIDTH > 32) assert(uint32_t(uint64_t(model.changed) >> 32) == expected[1]);
#else
        for (unsigned word = 0; word < words; ++word) assert(model.changed[word] == expected[word]);
#endif
        assert(model.selected == selected && model.selected_changed == changedBit);
        checksum = checksum * 131 + expected[0] + expected.back() + model.selected;
#else
        for (unsigned word = 0; word < words; ++word)
            for (unsigned byte = 0; byte < 4 && word * 4 + byte < sizeof(data.bytes); ++byte)
                data.bytes[word * 4 + byte] = source[word] >> (byte * 8);
        index = selectedIndex; enable = enabled; bitValue = replacement;
        ++_system_clock;
        auto actual = model.changed_out();
        for (unsigned bit = 0; bit < INDEX_WIDTH; ++bit)
            assert(bool(actual[bit]) == bool((expected[bit / 32] >> (bit % 32)) & 1));
        assert(bool(model.selected_out()) == selected && bool(model.selected_changed_out()) == changedBit);
        checksum = checksum * 131 + expected[0] + expected.back() + selected;
#endif
    }
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("indexed word width=%u samples=%u checksum=%llu seconds=%.9f\n", INDEX_WIDTH,
                samples, static_cast<unsigned long long>(checksum), seconds);
}
