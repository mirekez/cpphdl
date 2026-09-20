#pragma once
#include <cstdint>

struct RegfileTrace {
    uint64_t write_data;
    uint64_t expected;
    uint16_t read_addresses;
    uint16_t write_addresses;
    uint8_t enables;
    uint8_t reset_n;
    uint16_t reserved;
};
static_assert(sizeof(RegfileTrace) == 24);
