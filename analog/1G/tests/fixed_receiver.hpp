#pragma once
#include "../receive_filter.hpp"
#include "../adaptation.hpp"

// Test adapter for the actual hardware class. The C++RTL work/strobe methods
// execute every sample; this does not reproduce the hardware's arithmetic.
class fixed_receiver {
public:
    static constexpr unsigned count = 24;
    std::array<double, count> weights{};

private:
    ReceiveAdaptiveFilter hardware;
    unsigned sample_code = 128, local_codes = 1170, desired_code = 2;
    bool training = false, initialized = false;

public:
    double sample(double input, const std::array<double, 4>& local, std::optional<double> desired = {}) {
        if (!initialized) {
            hardware.sample_in = _ASSIGN(sample_code);
            hardware.local_in = _ASSIGN(local_codes);
            hardware.desired_in = _ASSIGN(desired_code);
            hardware.train_in = _ASSIGN(training);
            hardware.advance_in = _ASSIGN(true);
            hardware._assign();
            ++_system_clock;
            hardware._work(true);
            hardware._strobe();
            initialized = true;
        }
        sample_code = unsigned(std::clamp(std::floor(input * 32 + .5) + 128, 0., 255.));
        local_codes = 0;
        for (unsigned lane = 0; lane < 4; ++lane)
            local_codes |= unsigned(std::clamp(std::round(local[lane]), -2., 2.) + 2) << (lane * 3);
        training = bool(desired);
        // An invalid target in holdout proves that the port is ignored then.
        desired_code = desired ? unsigned(std::round(*desired) + 2) : 7;
        ++_system_clock;
        hardware._work(false);
        hardware._strobe();
        if (hardware.fault_out() || !hardware.valid_out())
            throw std::runtime_error("hardware receive filter rejected a sampled symbol");
        for (unsigned i = 0; i < count; ++i)
            weights[i] = (int(unsigned(hardware.coefficient_out[i]()) ^ 131072) - 131072) / 16384.;
        return (int(unsigned(hardware.sample_out())) - 128) / 32.;
    }

    static int decide(double value) {
        return analog::one_g::adaptive_receiver::decide(value);
    }
};

class fixed_adc {
public:
    unsigned clips = 0;
    double sample(double value) {
        if (!std::isfinite(value))
            throw std::runtime_error("nonfinite hardware ADC observation");
        clips += value < -4 || value > 127. / 32;
        return (std::clamp(std::floor(value * 32 + .5) + 128, 0., 255.) - 128) / 32;
    }
};
