#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace analog::one_g {
// Sampled DSP reference, not an electrical device or a transistor implementation.
class sampled_adc {
public:
    unsigned bits = 8;
    double range = 4; // +/- range, in normalized PAM levels.
    unsigned clips = 0;

    double sample(double value) {
        if (!std::isfinite(value) || !std::isfinite(range) || range <= 0 || bits < 2 || bits > 24)
            throw std::invalid_argument("invalid sampled ADC input/configuration");
        if (std::abs(value) > range)
            ++clips;
        double codes = double((1u << bits) - 1);
        return std::round((std::clamp(value, -range, range) + range) * codes / (2 * range)) * (2 * range) / codes -
               range;
    }
};

// Joint four-tap receive FFE and five taps from each of four local transmit streams.
// Desired symbols enter only during training. No expected data is available in holdout.
class adaptive_receiver {
public:
    static constexpr unsigned receive_taps = 4, reference_taps = 5, streams = 4;
    static constexpr unsigned count = receive_taps + streams * reference_taps;
    // A truncated inverse of the known 3/4 + 1/4 z^-1 transmit pulse seeds the FFE.
    std::array<double, count> weights{4.0 / 3, -4.0 / 9, 4.0 / 27, -4.0 / 81};
    double rate = .65;

private:
    std::array<double, receive_taps> receive_{};
    std::array<std::array<double, reference_taps>, streams> reference_{};

public:
    double sample(double input, const std::array<double, streams>& local, std::optional<double> desired = {}) {
        if (!std::isfinite(input) || !std::isfinite(rate) || rate <= 0 || rate >= 2)
            throw std::invalid_argument("invalid NLMS input/rate");
        for (unsigned tap = receive_taps - 1; tap; --tap)
            receive_[tap] = receive_[tap - 1];
        receive_[0] = input;
        std::array<double, count> feature{};
        std::copy(receive_.begin(), receive_.end(), feature.begin());
        for (unsigned stream = 0; stream < streams; ++stream) {
            if (!std::isfinite(local[stream]))
                throw std::invalid_argument("nonfinite transmit reference");
            for (unsigned tap = reference_taps - 1; tap; --tap)
                reference_[stream][tap] = reference_[stream][tap - 1];
            reference_[stream][0] = local[stream];
            for (unsigned tap = 0; tap < reference_taps; ++tap)
                feature[receive_taps + stream * reference_taps + tap] = -reference_[stream][tap];
        }
        double output = 0, energy = 1e-3;
        for (unsigned i = 0; i < count; ++i) {
            output += weights[i] * feature[i];
            energy += feature[i] * feature[i];
        }
        if (desired) {
            if (!std::isfinite(*desired))
                throw std::invalid_argument("nonfinite training symbol");
            for (unsigned i = 0; i < count; ++i)
                weights[i] += rate * (*desired - output) * feature[i] / energy;
        }
        return output;
    }
    static int decide(double value) {
        if (!std::isfinite(value))
            throw std::invalid_argument("nonfinite PAM decision");
        return int(std::clamp(std::round(value), -2.0, 2.0));
    }
};
} // namespace analog::one_g
