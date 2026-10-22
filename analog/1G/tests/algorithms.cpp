#include "../adaptation.hpp"
#include "../timing_recovery.hpp"
#include "../mapping.hpp"
#include <iostream>
#include <set>
#include <vector>

using namespace analog::one_g;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
unsigned state = 0x723ab1;
int random_symbol() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return int(state % 5) - 2;
}
void mapping_test() {
    std::set<quartet> seen;
    for (unsigned point = 0; point < 64; ++point)
        for (unsigned subset = 0; subset < 8; ++subset) {
            auto value = symbol_mapper::map(point, subset);
            int sum = 0;
            for (int x : value) {
                require(x >= -2 && x <= 2, "invalid PAM5 mapping level");
                sum += x;
            }
            require((sum & 1) == int(subset & 1), "even/odd subset parity mismatch");
            require(seen.insert(value).second, "ambiguous normal-data mapping");
            require(symbol_mapper::inverse(value) == point * 8 + subset, "inverse mapping mismatch");
            for (unsigned other = 0; other < point; ++other) {
                auto previous = symbol_mapper::map(other, subset);
                int distance = 0;
                for (unsigned lane = 0; lane < 4; ++lane)
                    distance += (value[lane] - previous[lane]) * (value[lane] - previous[lane]);
                require(distance >= 4, "subset does not preserve minimum squared distance");
            }
        }
    require(symbol_mapper::map(0, 0) == quartet{0, 0, 0, 0}, "IEEE zero mapping vector mismatch");
    require(symbol_mapper::map(0, 7) == quartet{0, 1, 0, 0}, "IEEE odd-subset vector mismatch");
    require(!symbol_mapper::inverse(quartet{2, 2, 2, 2}), "control delimiter accepted as normal data");
}
void adaptation_test() {
    adaptive_receiver receiver;
    sampled_adc adc;
    std::array<double, 4> local{}, previous{};
    double previous_remote = 0, mse = 0, raw = 0;
    unsigned errors = 0;
    std::array<double, adaptive_receiver::count> frozen{};
    for (unsigned i = 0; i < 2048; ++i) {
        double remote = random_symbol();
        for (auto& value : local)
            value = random_symbol();
        double received = .75 * remote + .25 * previous_remote + .5 * local[0] + .18 * previous[1] - .1 * local[2];
        std::optional<double> desired;
        if (i < 1024)
            desired = remote;
        double result = receiver.sample(adc.sample(received), local, desired);
        if (i == 1023)
            frozen = receiver.weights;
        if (i >= 1024)
            require(frozen == receiver.weights, "NLMS learned from holdout answers");
        if (i >= 1040) {
            errors += adaptive_receiver::decide(result) != int(remote);
            mse += (result - remote) * (result - remote);
            raw += (received - remote) * (received - remote);
        }
        previous_remote = remote;
        previous = local;
    }
    std::cout << "NLMS holdout: errors=" << errors << " MSE=" << mse / 1008 << " raw_MSE=" << raw / 1008
              << " clips=" << adc.clips << '\n';
    require(errors == 0 && mse < raw * .01 && adc.clips == 0, "adaptive echo/NEXT/FFE holdout failed");
}
void timing_test(double source_period) {
    std::vector<double> symbols(7000);
    for (auto& value : symbols)
        value = random_symbol();
    auto receive = [&](double time) {
        double index = time / source_period - .65;
        unsigned before = unsigned(std::floor(index));
        double fraction = index - before;
        return symbols.at(before) * (1 - fraction) + symbols.at(before + 1) * fraction;
    };
    gardner_timing clock;
    clock.next = 20.3;
    double error = 0;
    for (unsigned i = 0; i < 5000; ++i) {
        double time = clock.next;
        clock.advance(receive(time - clock.period / 2), receive(time), receive(time + clock.period / 2));
        if (i >= 4500) {
            double cycles = time / source_period - .65;
            error += std::abs(cycles - std::round(cycles));
        }
    }
    require(error / 500 < .1 && std::abs(clock.period / source_period - 1) < .0004 && clock.active > 1000,
            "Gardner loop did not track phase/rate without expected symbols");
    double period = clock.period;
    for (unsigned i = 0; i < 100; ++i)
        clock.advance(0, 0, 0);
    require(clock.period == period, "no-data timing holdover changed frequency");
}
} // namespace
int main() {
    try {
        mapping_test();
        adaptation_test();
        timing_test(1.0001);
        timing_test(.9999);
        std::cout << "512 constellation mappings, frozen-holdout NLMS, ADC and +/-100 ppm Gardner timing passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
