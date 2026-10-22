#pragma once
#include <analog.hpp>
#include <array>
#include <numbers>

namespace analog::ten_g {
inline constexpr double line_rate = 10.3125e9;
inline constexpr double ui = 1.0 / line_rate;
inline constexpr double supply = 1.2;
inline constexpr double common_mode = .6;
inline constexpr std::size_t parallel_width = 4;
} // namespace analog::ten_g
