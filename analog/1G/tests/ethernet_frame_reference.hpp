#pragma once
#include <cstdint>
#include <vector>

// Test-side CRC over an octet vector, independent of hardware scheduling.
static std::uint32_t crc(const std::vector<unsigned>& data, unsigned start) {
    std::uint32_t value = 0xffffffffu;
    for (unsigned i = start; i < data.size(); ++i) {
        value ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = value & 1 ? (value >> 1) ^ 0xedb88320u : value >> 1;
    }
    return value ^ 0xffffffffu;
}

static void append_fcs(std::vector<unsigned>& data) {
    auto value = crc(data, 8);
    for (unsigned i = 0; i < 4; ++i) data.push_back((value >> (i * 8)) & 255);
}

static std::vector<unsigned> frame(unsigned quanta, std::uint64_t destination = 0x0180c2000001ull) {
    std::vector<unsigned> data(68, 0);
    for (unsigned i = 0; i < 7; ++i) data[i] = 0x55;
    data[7] = 0xd5;
    for (unsigned i = 0; i < 6; ++i) {
        data[8 + i] = (destination >> ((5 - i) * 8)) & 255;
        data[14 + i] = (0x02123456789aull >> ((5 - i) * 8)) & 255;
    }
    data[20] = 0x88; data[21] = 8; data[23] = 1;
    data[24] = quanta >> 8; data[25] = quanta & 255;
    append_fcs(data);
    return data;
}

