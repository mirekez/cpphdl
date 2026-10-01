#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

struct BusTrace {
    uint32_t reset_n;
    std::array<uint32_t, 24> requests;
    std::array<uint32_t, 47> responses;
    std::array<uint32_t, 10> slave_outputs;
    std::array<uint32_t, 118> master_outputs;
};
static_assert(sizeof(BusTrace) == 800);

inline std::vector<BusTrace> read_bus_trace(const char* path) {
    auto* file = std::fopen(path, "rb");
    if (!file) throw std::runtime_error("cannot open bus trace");
    std::vector<BusTrace> result;
    BusTrace record{};
    std::size_t bytes = 0;
    while ((bytes = std::fread(&record, 1, sizeof(record), file)) == sizeof(record)) {
        if (record.reset_n > 1 || (record.requests.back() >> 12) ||
            (record.responses.back() >> 8) || (record.slave_outputs.back() >> 4) ||
            (record.master_outputs.back() >> 16)) {
            std::fclose(file);
            throw std::runtime_error("invalid bus trace record");
        }
        result.push_back(record);
    }
    const bool valid = bytes == 0 && !std::ferror(file) && !result.empty() && !result.front().reset_n;
    std::fclose(file);
    if (!valid) throw std::runtime_error("empty, truncated or unreset bus trace");
    return result;
}
