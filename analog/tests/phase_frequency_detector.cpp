#include <basic/phase_frequency_detector.hpp>
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

int main(int argc, char** argv) {
    try {
        if (argc != 5)
            throw std::runtime_error("expected destination run|export profile loaded");
        const std::filesystem::path destination(argv[1]);
        const std::string_view mode(argv[2]), profile(argv[3]);
        if (mode != "run" && mode != "export")
            throw std::runtime_error("use run or export");
        double period = 8e-9, phase = 0;
        if (profile == "ref_leads")
            phase = 1e-9;
        else if (profile == "fb_leads" || profile == "fb_wrapped")
            phase = -1e-9;
        else if (profile == "slow")
            period = 24e-9;
        else if (profile == "fast")
            period = 4e-9;
        else if (profile != "same" && profile != "missing")
            throw std::runtime_error("unknown phase/frequency profile");
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(1e-13);
        electrical source, vdd, vss, reference, feedback, clear;
        sim.reference(vss);
        source = sim_voltage_source(vss, voltage(1.2));
        vdd = resistor(source, resistance(10));
        vdd = capacitor(vss, capacitance_value(2e-12));
        auto waveform = [](double cycle, double delay, bool missing) {
            std::vector<std::pair<double, double>> points{{0, 0}};
            if (!missing)
                for (double time = 16e-9 + delay; time + cycle / 2 + .1e-9 < 384e-9; time += cycle) {
                    points.push_back({time, 0});
                    points.push_back({time + .1e-9, 1.2});
                    points.push_back({time + cycle / 2, 1.2});
                    points.push_back({time + cycle / 2 + .1e-9, 0});
                }
            return points;
        };
        reference = sim_pwl_voltage_source(vss, waveform(8e-9, 0, false));
        feedback = sim_pwl_voltage_source(vss, waveform(period, phase, profile == "missing"));
        const double release = profile == "fb_wrapped" ? 151e-9 : 157e-9;
        clear = sim_pwl_voltage_source(vss, {{0, 1.2}, {12e-9, 1.2}, {12.1e-9, 0},
            {137e-9, 0}, {137.1e-9, 1.2}, {release, 1.2}, {release + .1e-9, 0}});
        cmos_phase_frequency_detector detector;
        detector.REF = resistor(reference, resistance(50));
        detector.FEEDBACK = resistor(feedback, resistance(50));
        detector.CLEAR = resistor(clear, resistance(50));
        detector.Vdd = vdd;
        detector.Vss = vss;
        detector.elaborate();
        if (std::stoi(argv[4])) {
            detector.UP = capacitor(vss, capacitance_value(20e-15));
            detector.DOWN = capacitor(vss, capacitance_value(20e-15));
        }
        sim.observe("UP", detector.UP, vss);
        sim.observe("DOWN", detector.DOWN, vss);
        sim.observe("UP_N", detector.UP_N, vss);
        sim.observe("DOWN_N", detector.DOWN_N, vss);
        sim.observe("REF", detector.REF, vss);
        sim.observe("FEEDBACK", detector.FEEDBACK, vss);
        sim.observe("RESET", detector.RESET, vss);
        sim.observe("VDD", vdd, vss);
        std::filesystem::create_directories(destination);
        const duration stop(384e-9), step(8e-9 / 512);
        verilog_a::write_experiment(sim, destination, {stop, step, duration(step.value / 2), 1e-16, 2, 1e-18});
        if (mode == "run") {
            sim.run(stop, step);
            sim.write(destination / "cpp");
            return sim.status() == "sampled_pass" ? 0 : 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
