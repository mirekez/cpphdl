#include "../adc.hpp"
#include "../adc_sar_control.hpp"
#include <VerilogA/experiment.hpp>
#include <fstream>
#include <iostream>

long _system_clock = -1;
using namespace analog;
constexpr double period = 4e-9, supply = 3.3;
constexpr unsigned clocks = 64;

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5)
        return 2;
    try {
        const std::filesystem::path destination(argv[1]), feedback_path(argv[3]);
        std::array<bool, clocks> feedback{};
        if (feedback_path != "none") {
            std::ifstream file(feedback_path);
            for (unsigned i = 0, value = 0; i < clocks; ++i) {
                if (!(file >> value) || value > 1)
                    throw std::runtime_error("missing/invalid analog comparator feedback");
                feedback[i] = value;
            }
            unsigned extra;
            if (file >> extra)
                throw std::runtime_error("extra comparator feedback");
        }
        std::filesystem::create_directories(destination);
        ADCSARController controller;
        bool start = false, comparison = false;
        controller.start_in = _ASSIGN(start);
        controller.comparison_in = _ASSIGN(comparison);
        controller._assign();
        ++_system_clock;
        controller._work(true);
        controller._strobe();
        using wave = std::vector<std::pair<double, double>>;
        wave track{{0, 0}}, evaluate{{0, 0}};
        std::array<wave, 8> bits;
        for (auto& bit : bits)
            bit.emplace_back(0, supply);
        auto change = [](wave& points, double at, double value) {
            if (points.back().second != value) {
                points.emplace_back(at, points.back().second);
                points.emplace_back(at + .1e-9, value);
            }
        };
        std::ofstream controls(destination / "control.csv");
        controls << "clock,trial,data,valid,acquire,convert,busy\n";
        for (unsigned edge = 0; edge < clocks; ++edge) {
            start = edge % 16 == 0;
            comparison = feedback[edge];
            ++_system_clock;
            controller._work(false);
            controller._strobe();
            const double at = edge * period + .05e-9;
            change(track, at, controller.acquire_out() ? supply : 0);
            for (unsigned bit = 0; bit < 8; ++bit)
                change(bits[bit], at, (unsigned(controller.trial_out()) >> bit) & 1 ? 0 : supply);
            if (controller.convert_out()) {
                evaluate.emplace_back(edge * period + 2e-9, 0);
                evaluate.emplace_back(edge * period + 2.1e-9, supply);
                evaluate.emplace_back(edge * period + 3.7e-9, supply);
                evaluate.emplace_back(edge * period + 3.8e-9, 0);
            }
            controls << edge << ',' << unsigned(controller.trial_out()) << ',' << unsigned(controller.data_out())
                     << ',' << bool(controller.valid_out()) << ',' << bool(controller.acquire_out())
                     << ',' << bool(controller.convert_out()) << ',' << bool(controller.busy_out()) << '\n';
        }
        controls.close();
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(period / 65536);
        electrical vdd, vss, source;
        sim.reference(vss);
        vdd = sim_voltage_source(vss, voltage(supply));
        wave input{{0, 1.65}};
        const std::string profile = argc == 5 ? argv[4] : "pam";
        if (profile != "pam" && profile != "center")
            throw std::runtime_error("unknown ADC stimulus profile");
        const std::array<double, 4> levels = profile == "pam" ? std::array<double, 4>{1.15, 1.4, 1.9, 2.15}
                                                             : std::array<double, 4>{1.65, 1.649, 1.651, 1.65};
        for (unsigned sample = 0; sample < levels.size(); ++sample) {
            change(input, sample * 16 * period + 6e-9, levels[sample]);
            // Deliberately disturb the source while the conversion holds it.
            change(input, sample * 16 * period + 24e-9, 3.3 - levels[sample]);
        }
        source = sim_pwl_voltage_source(vss, input);
        one_g::SARADCFrontend adc;
        adc.IN = resistor(source, resistance(50));
        adc.TRACK = sim_pwl_voltage_source(vss, track);
        adc.EVALUATE = sim_pwl_voltage_source(vss, evaluate);
        for (unsigned bit = 0; bit < 8; ++bit)
            adc.BIT_N[bit] = sim_pwl_voltage_source(vss, bits[bit]);
        adc.Vdd = vdd;
        adc.Vss = vss;
        adc.elaborate();
        sim.observe("POSITIVE", adc.POSITIVE, vss);
        sim.observe("NEGATIVE", adc.NEGATIVE, vss);
        sim.observe("HELD", adc.sampler.OUT, vss);
        sim.observe("DAC", adc.ladder.OUT, vss);
        sim.observe("INPUT", adc.IN, vss);
        const duration stop(clocks * period), step(period / 1536);
        verilog_a::write_experiment(sim, destination, {stop, step, duration(step.value / 2), 1e-16, 2, 1e-18});
        if (std::string_view(argv[2]) == "run") {
            sim.run(stop, step);
            sim.write(destination / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        if (std::string_view(argv[2]) != "export")
            throw std::runtime_error("use run or export");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
