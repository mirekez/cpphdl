#pragma once
#include <analog.hpp>
#include <array>

namespace analog::one_g {
inline constexpr double symbol_rate = 125e6;
inline constexpr double ui = 1.0 / symbol_rate;
inline constexpr double supply = 3.3;
inline constexpr double logic_supply = 1.2;
inline constexpr double common_mode = 1.65;
inline constexpr double logic_midpoint = .6;
inline constexpr double symbol_step = .25;
inline constexpr std::size_t pairs = 4;
using quartet = std::array<int, pairs>;

inline void bind(IC& component, const electrical& vdd, const electrical& vss) {
    component.Vdd = vdd;
    component.Vss = vss;
    component.elaborate();
}
} // namespace analog::one_g
