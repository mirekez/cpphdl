#pragma once
#include <cstdint>
#include <map>

#ifndef _LIBCPP_VERSION
#error "HLS container examples require libc++ headers"
#endif

// Shared std::map reference and workload for the standard-map and RbMap examples.
struct MapMethods {
    std::map<uint32_t, uint32_t> data;
    static constexpr bool singleClock(uint32_t op) { return op == 6; }
    uint64_t sum(uint32_t bias) {
        uint64_t result = bias;
        for (auto it = data.begin(); it != data.end(); ++it)
            result = result * 131 + uint64_t(it->first) * 65537 + it->second;
        return result;
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 0) {
            auto& mapped = data[index];
            mapped = value;
            return mapped;
        }
        if (operation == 1) return sum(value + index);
        if (operation == 2) {
            for (auto it = data.begin(); it != data.end(); ++it) it->second = value;
            return data.size();
        }
        if (operation == 5) { data.clear(); return data.size(); }
        if (operation == 6) return data.size();
        auto it = data.find(index);
        if (it == data.end()) return operation == 4 ? 0 : UINT64_MAX;
        if (operation == 4) { data.erase(it); return 1; }
        return it->second;
    }
#ifndef SYNTHESIS
    template<class Test> static void extraTests(Test&& test) {
        test(6, 0, 0);
        test(3, 123, 0); // A missing lookup must not insert a node.
        test(6, 0, 0);
        test(0, 3, 919);
        test(6, 0, 0);
        for (unsigned key : {3u, 0u, 7u, 4u, 1u, 6u, 2u, 5u}) {
            test(4, key, 0);
            test(3, key, 0);
            test(1, 11, 17);
        }
        test(4, 999, 0);
        for (unsigned key : {40u, 20u, 60u, 10u, 30u, 50u, 70u, 25u, 35u})
            test(0, key, key + 81);
        test(4, 40, 0); // Erase a node with two children.
        test(1, 11, 17);
        test(5, 0, 0);
        test(6, 0, 0);
        for (unsigned key = 8; key > 0; --key) test(0, key, key + 91);
        test(1, 19, 23);
        uint32_t seed = 12345;
        for (unsigned i = 0; i < 40; ++i) {
            seed = seed * 1664525u + 1013904223u;
            unsigned key = (seed >> 16) % 23;
            test(i % 3 == 0 ? 4 : 0, key, seed);
            test(3, key, 0);
            test(6, 0, 0);
            test(1, i, seed);
        }
    }
#endif
};
