#include "../examples/feedback_checks.hpp"
#include <sstream>

using namespace analog;

static std::vector<sample> read_trace(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("missing trace: " + path.string());
    std::string line;
    std::getline(file, line); // Header; each tested bench fixes the observation order explicitly.
    std::vector<sample> trace;
    while (std::getline(file, line)) {
        std::istringstream row(line);
        std::string field;
        std::getline(row, field, ',');
        sample value{std::stod(field), {}, {}};
        while (std::getline(row, field, ','))
            value.values.push_back(std::stod(field));
        trace.push_back(std::move(value));
    }
    if (trace.size() < 100)
        throw std::runtime_error("incomplete trace: " + path.string());
    return trace;
}

int main(int argc, char** argv) {
    if (argc != 4)
        return 2;
    try {
        std::string example = argv[1];
        auto coarse = read_trace(argv[2]), fine = read_trace(argv[3]);
        if (fine.size() < 1.8 * coarse.size())
            throw std::runtime_error("refined trace must contain substantially more samples");
        bool pass = true;
        auto compare = [&](const char* name, double a, double b, double tolerance) {
            bool close = std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance;
            std::cout << name << ": coarse=" << a << ", fine=" << b << ", tolerance=" << tolerance << '\n';
            pass &= close;
        };
        auto statistics = [](const auto& trace, std::size_t column, double start, double end) {
            auto result = feedback_checks::window(trace, column, start, end);
            if (!result)
                throw std::runtime_error("missing or nonfinite measurement window");
            return *result;
        };
        if (example == "ldo") {
            for (double start : {.0007, .0017, .003}) {
                auto a = statistics(coarse, 1, start, start + .0002);
                auto b = statistics(fine, 1, start, start + .0002);
                compare("settled output V", a.mean, b.mean, .001);
            }
        } else if (example == "pll") {
            auto measure = [&](const auto& trace) {
                auto reference = feedback_checks::rising_edges(trace, 0, .6, .01, .015);
                auto output = feedback_checks::rising_edges(trace, 1, .6, .01, .015);
                auto f = feedback_checks::frequency(output);
                if (!f || reference.size() < 20)
                    throw std::runtime_error("missing oscillator/reference edges");
                double phase = 0;
                for (double edge : output) {
                    auto nearest = std::min_element(reference.begin(), reference.end(), [&](double a, double b) {
                        return std::abs(a - edge) < std::abs(b - edge);
                    });
                    phase += (edge - *nearest) * 12000;
                }
                return std::pair{*f, phase / output.size()};
            };
            auto a = measure(coarse), b = measure(fine);
            compare("frequency Hz", a.first, b.first, 12); // 0.1% of the reference.
            compare("mean phase cycles", a.second, b.second, .01);
        } else if (example == "agc") {
            for (double start : {.06, .14, .22}) {
                auto a = statistics(coarse, 1, start, start + .015);
                auto b = statistics(fine, 1, start, start + .015);
                compare("positive peak V", a.high, b.high, .002);
                compare("mean detector V", statistics(coarse, 3, start, start + .015).mean,
                        statistics(fine, 3, start, start + .015).mean, .003);
                compare("mean control V", statistics(coarse, 2, start, start + .015).mean,
                        statistics(fine, 2, start, start + .015).mean, .05);
            }
        } else {
            throw std::runtime_error("unknown example");
        }
        return pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
