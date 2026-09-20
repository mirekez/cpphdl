#pragma once

#include <array>
#include <cstddef>

namespace cpphdl {

// A compile-time definite-write proof, never simulation state. Reads are
// checked in source dependency order, so a fully written but cyclic net fails.
template<std::size_t Width>
struct comb_write_proof {
    std::array<bool, Width> written{};
    bool valid = true;
    std::size_t steps = 0;

    constexpr bool step() { return ++steps <= 16384; }
    constexpr void read(std::size_t index) {
        if (index >= Width || !written[index]) valid = false;
    }
    constexpr void write(std::size_t index) {
        if (index >= Width) valid = false;
        else written[index] = true;
    }
    constexpr bool complete() const {
        if (!valid) return false;
        for (bool bit : written) if (!bit) return false;
        return true;
    }
};

}
