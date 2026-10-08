#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>

inline void checkClockValue(const char* field, uint64_t actual, uint64_t expected, unsigned time) {
    if (actual != expected) {
        std::fprintf(stderr, "%s at t=%u: actual=%llu expected=%llu\n", field, time,
                     (unsigned long long)actual, (unsigned long long)expected);
        std::exit(1);
    }
}
