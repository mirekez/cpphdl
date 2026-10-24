#pragma once
#include <array>
#include <climits>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

// Read the independent IEEE table fixtures; the oracle never calls a DUT
// mapping method or uses the receiver's Cartesian partition optimization.
struct ConstellationReference {
    std::array<std::array<std::array<int, 4>, 64>, 8> symbols{};
    explicit ConstellationReference(const std::string& directory) {
        std::ifstream input(directory + "/pcs_data.csv");
        if (!input)
            throw std::runtime_error("missing constellation fixtures");
        std::string line;
        std::getline(input, line);
        unsigned count = 0;
        while (std::getline(input, line)) {
            for (auto& character : line)
                if (character == ',')
                    character = ' ';
            unsigned point, subset;
            std::istringstream row(line);
            if (!(row >> point >> subset) || point >= 64 || subset >= 8)
                throw std::runtime_error("invalid constellation fixture");
            for (auto& level : symbols[subset][point])
                if (!(row >> level) || level < -2 || level > 2)
                    throw std::runtime_error("invalid constellation level");
            ++count;
        }
        if (count != 512)
            throw std::runtime_error("incomplete constellation fixtures");
    }
    void nearest(const std::array<int, 4>& sample, unsigned signs, std::array<unsigned, 8>& metrics,
                 std::array<unsigned, 8>& points) const {
        for (unsigned subset = 0; subset < 8; ++subset) {
            metrics[subset] = UINT_MAX;
            for (unsigned point = 0; point < 64; ++point) {
                unsigned sum = 0;
                for (unsigned lane = 0; lane < 4; ++lane) {
                    int observed = sample[lane] - 128;
                    if (signs & (1 << lane))
                        observed = -observed;
                    int difference = observed - 32 * symbols[subset][point][lane];
                    sum += unsigned(difference * difference);
                }
                if (sum < metrics[subset]) {
                    metrics[subset] = sum;
                    points[subset] = point;
                }
            }
        }
    }
};

inline unsigned next_random(unsigned& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
