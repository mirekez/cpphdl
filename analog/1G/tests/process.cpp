#include "../adaptation.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#ifdef CPPHDL_FIXED_RECEIVER
#include "fixed_receiver.hpp"
long _system_clock = -1;
using receive_model = fixed_receiver;
using adc_model = fixed_adc;
#else
using receive_model = analog::one_g::adaptive_receiver;
using adc_model = analog::one_g::sampled_adc;
#endif

using namespace analog::one_g;
int main(int argc, char** argv) {
    if (argc != 5 && argc != 6)
        return 2;
    try {
        std::ifstream input(argv[1]);
        if (!input)
            throw std::runtime_error("missing sampled receiver input");
        unsigned training = unsigned(std::stoi(argv[3]));
        bool adapt = std::stoi(argv[4]) != 0;
        std::string line;
        std::getline(input, line);
        unsigned channels = unsigned(std::count(line.begin(), line.end(), ',')) / 6;
        if (channels != 2 && channels != 8)
            throw std::runtime_error("sampled DSP requires two or eight receive channels");
        std::vector<receive_model> receiver(channels);
        std::vector<adc_model> adc(channels);
        std::vector<double> squared(channels), raw_squared(channels);
        std::vector<unsigned> errors(channels), raw_errors(channels), samples(channels);
        std::vector<std::array<double, receive_model::count>> frozen(channels);
        std::ofstream soft;
        if (argc == 6) {
            soft.open(argv[5]);
            if (!soft)
                throw std::runtime_error("cannot open equalized ADC output");
            soft << std::setprecision(17) << "symbol";
            for (unsigned channel = 0; channel < channels; ++channel)
                soft << ",soft" << channel;
            soft << '\n';
        }
        unsigned row = 0;
        while (std::getline(input, line)) {
            std::replace(line.begin(), line.end(), ',', ' ');
            std::istringstream values(line);
            unsigned index;
            if (!(values >> index) || index != row)
                throw std::runtime_error("missing/nonsequential sampled symbol");
            if (soft.is_open())
                soft << index;
            for (unsigned channel = 0; channel < channels; ++channel) {
                double rx, desired;
                std::array<double, 4> local;
                if (!(values >> rx >> local[0] >> local[1] >> local[2] >> local[3] >> desired))
                    throw std::runtime_error("truncated sampled receiver row");
                // Only this branch passes expected data to adaptation.
                std::optional<double> target;
                if (adapt && row < training)
                    target = desired;
                double result = receiver[channel].sample(adc[channel].sample(rx), local, target);
                if (soft.is_open())
                    soft << ',' << result;
                if (row + 1 == training)
                    frozen[channel] = receiver[channel].weights;
                if (row >= training && receiver[channel].weights != frozen[channel])
                    throw std::runtime_error("holdout unexpectedly changed trained coefficients");
                if (row >= training + 8) {
                    ++samples[channel];
                    errors[channel] += adaptive_receiver::decide(result) != int(desired);
                    raw_errors[channel] += adaptive_receiver::decide(rx) != int(desired);
                    squared[channel] += (result - desired) * (result - desired);
                    raw_squared[channel] += (rx - desired) * (rx - desired);
                }
            }
            double extra;
            if (values >> extra)
                throw std::runtime_error("unexpected sampled receiver columns");
            if (soft.is_open())
                soft << '\n';
            ++row;
        }
        if (row <= training + 24)
            throw std::runtime_error("insufficient independent holdout data");
        std::ofstream output(argv[2]);
        output << std::setprecision(17) << "{\"channels\":[";
        for (unsigned channel = 0; channel < channels; ++channel) {
            output << (channel ? "," : "") << "{\"symbol_errors\":" << errors[channel]
                   << ",\"raw_errors\":" << raw_errors[channel] << ",\"samples\":" << samples[channel]
                   << ",\"mse\":" << squared[channel] / samples[channel]
                   << ",\"raw_mse\":" << raw_squared[channel] / samples[channel]
                   << ",\"adc_clips\":" << adc[channel].clips << ",\"weights\":[";
            for (unsigned i = 0; i < receiver[channel].count; ++i)
                output << (i ? "," : "") << receiver[channel].weights[i];
            output << "]}";
        }
        output << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
