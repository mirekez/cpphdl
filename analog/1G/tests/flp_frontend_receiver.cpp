#include "../flp_receive.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

long _system_clock = -1;

// Replay each solver's actual detector waveform at the reference-clock input.
// Electrical pulse templates retain their measured edges; only the long quiet
// intervals between FLP pulses are expanded to the production protocol timing.
int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    try {
        std::ifstream input(argv[1]);
        std::string line;
        std::getline(input, line);
        std::vector<double> times, levels;
        auto number = [](const std::string& text) {
            char* end = nullptr;
            const double value = std::strtod(text.c_str(), &end);
            // Physical quiet nodes can contain subnormal values. Underflow to
            // zero is valid; incomplete text and nonfinite values are not.
            if (end == text.c_str() || *end || !std::isfinite(value))
                throw std::runtime_error("invalid detector number");
            return value;
        };
        while (std::getline(input, line)) {
            std::replace(line.begin(), line.end(), ',', ' ');
            std::istringstream fields(line);
            std::string time_text, level_text;
            if (!(fields >> time_text >> level_text))
                throw std::runtime_error("invalid detector waveform");
            const double time = number(time_text), level = number(level_text);
            if (!times.empty() && time * 1e9 <= times.back())
                throw std::runtime_error("unordered detector waveform");
            times.push_back(time * 1e9);
            levels.push_back(level);
        }
        if (times.size() < 1000 || times.front() > 1 || times.back() < 999)
            throw std::runtime_error("missing detector waveform coverage");
        const bool expected_pulses = std::stoi(argv[2]) != 0;
        auto sample = [&](double start, double offset) {
            if (offset < 0 || offset >= 144)
                return 0.;
            const double time = start + offset;
            auto upper = std::lower_bound(times.begin(), times.end(), time);
            const unsigned i = unsigned(upper - times.begin());
            if (i == 0 || i >= times.size())
                throw std::runtime_error("template sample outside trajectory");
            return levels[i - 1] + (levels[i] - levels[i - 1])
                * (time - times[i - 1]) / (times[i] - times[i - 1]);
        };
        unsigned pages = 0, observations = 0;
        for (unsigned phase = 0; phase < 8; ++phase) {
            for (unsigned page : {0x0000u, 0xFFFFu, 0xA601u, 0x592Eu}) {
                FLPReceiver receiver;
                bool pulse = false;
                receiver.pulse_in = _ASSIGN(pulse);
                receiver.enable_in = _ASSIGN(true);
                receiver._assign();
                ++_system_clock;
                receiver._work(true);
                receiver._strobe();
                unsigned received = 0;
                pulse = false;
                for (unsigned clock = 0; clock < 250040; ++clock) {
                    const int elapsed = int(clock * 8) - 32 - int(phase);
                    double voltage = 0;
                    if (elapsed >= 0) {
                        const unsigned slot = unsigned(elapsed) / 125000;
                        const unsigned offset = unsigned(elapsed) % 125000;
                        if (slot <= 16) {
                            voltage = sample(160, offset);
                            if (slot < 16 && ((page >> slot) & 1))
                                voltage = std::max(voltage, sample(320, int(offset) - 62500));
                            if (slot < 16)
                                voltage = std::max(voltage, sample(480, int(offset) - 31250));
                        }
                    }
                    // The sensing boundary retains its previous state in the
                    // undefined voltage region, as the electrical co-sim does.
                    if (voltage <= .99)
                        pulse = false;
                    else if (voltage >= 2.31)
                        pulse = true;
                    ++_system_clock;
                    receiver._work(false);
                    receiver._strobe();
                    ++observations;
                    if (receiver.fault_out())
                        throw std::runtime_error("physical pulse waveform caused a protocol fault");
                    if (receiver.valid_out()) {
                        ++received;
                        if (!expected_pulses || unsigned(receiver.page_out()) != page)
                            throw std::runtime_error("physical waveform decoded an incorrect FLP page");
                    }
                }
                if (received != unsigned(expected_pulses))
                    throw std::runtime_error("physical waveform lost or duplicated a page");
                pages += received;
            }
        }
        std::cout << "PASS " << observations << " reference clocks, " << pages
                  << " pages, eight sampling phases and physical short-pulse rejection\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
