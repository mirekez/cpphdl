#include "../flp_frontend.hpp"
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    if (argc != 6 && argc != 7)
        return 2;
    try {
        const std::filesystem::path destination(argv[1]);
        const bool driver_case = std::string_view(argv[3]) == "driver";
        if (!driver_case && std::string_view(argv[3]) != "detector")
            throw std::runtime_error("use driver or detector");
        const double parameter = std::stod(argv[4]);
        const bool reversed = std::stoi(argv[5]) != 0;
        const unsigned refinement = argc == 7 ? unsigned(std::stoul(argv[6])) : 4;
        if (refinement != 1 && refinement != 2 && refinement != 4 && refinement != 8)
            throw std::runtime_error("invalid integration refinement");
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(1e-12);
        electrical supply, vdd, vss, common, command, enable, p, n;
        sim.reference(vss);
        supply = sim_voltage_source(vss, voltage(3.3));
        vdd = resistor(supply, resistance(1));
        vdd = capacitor(vss, capacitance_value(5e-12));
        common = sim_voltage_source(vss, voltage(1.65));
        std::vector<std::pair<double, double>> waveform{{0, 0}};
        for (auto [start, width] : std::vector<std::pair<double, double>>{
                 {160e-9, 96e-9}, {320e-9, 104e-9}, {480e-9, 16e-9},
                 {640e-9, 120e-9}, {800e-9, 104e-9}}) {
            waveform.push_back({start, 0});
            waveform.push_back({start + 2e-9, 1});
            waveform.push_back({start + width, 1});
            waveform.push_back({start + width + 2e-9, 0});
        }
        auto scaled = [&](double offset, double scale) {
            auto values = waveform;
            for (auto& value : values)
                value.second = offset + scale * value.second;
            return values;
        };
        command = sim_pwl_voltage_source(vss, scaled(0, 3.3));
        enable = sim_pwl_voltage_source(vss, {{0, 3.3}, {840e-9, 3.3}, {842e-9, 0}});
        std::unique_ptr<one_g::flp_pulse_driver> driver;
        if (driver_case) {
            if (parameter < 60 || parameter > 150)
                throw std::runtime_error("invalid differential load");
            driver = std::make_unique<one_g::flp_pulse_driver>();
            driver->COMMAND = command;
            driver->ENABLE = enable;
            driver->P = p;
            driver->N = n;
            driver->Vdd = vdd;
            driver->Vss = vss;
            driver->elaborate();
            p = resistor(n, resistance(parameter));
            p = resistor(common, resistance(100000));
            n = resistor(common, resistance(100000));
            p = capacitor(vss, capacitance_value(10e-12));
            n = capacitor(vss, capacitance_value(10e-12));
        } else {
            if (parameter < 0 || parameter > 2.8)
                throw std::runtime_error("invalid detector amplitude");
            electrical source_p, source_n;
            source_p = sim_pwl_voltage_source(vss, scaled(1.65, parameter / 2));
            source_n = sim_pwl_voltage_source(vss, scaled(1.65, -parameter / 2));
            p = resistor(source_p, resistance(25));
            n = resistor(source_n, resistance(25));
        }
        one_g::flp_pulse_detector detector;
        detector.P = reversed ? n : p;
        detector.N = reversed ? p : n;
        detector.Vdd = vdd;
        detector.Vss = vss;
        detector.elaborate();
        sim.observe("P", p, vss);
        sim.observe("N", n, vss);
        sim.observe("PULSE", detector.PULSE, vss);
        sim.observe("COMMAND", command, vss);
        sim.observe("ENABLE", enable, vss);
        sim.observe("VDD", vdd, vss);
        sim.observe("POSITIVE", detector.comparators[0].OUT, vss);
        sim.observe("NEGATIVE", detector.comparators[1].OUT, vss);
        std::filesystem::create_directories(destination);
        const duration stop(1e-6), step(.25e-9 / refinement);
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
