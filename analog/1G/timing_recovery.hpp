#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace analog::one_g {
// Sampled Gardner timing loop. Times are in nominal UIs; no expected data enters it.
// The caller interpolates its actual receive waveform at next +/- period/2 and next.
class gardner_timing {
public:
    double next = .3, period = 1;
    double proportional = .01, integral = 1e-6;
    unsigned active = 0;

    void advance(double early, double middle, double late) {
        if (!std::isfinite(early) || !std::isfinite(middle) || !std::isfinite(late))
            throw std::invalid_argument("nonfinite timing observation");
        double error = std::clamp((early - late) * middle, -4.0, 4.0);
        if (std::abs(early - late) > .05)
            ++active;
        period = std::clamp(period - integral * error, .998, 1.002);
        next += period - proportional * error;
    }
};
} // namespace analog::one_g
