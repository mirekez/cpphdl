#include "../processing_clock_oscillator.hpp"
#include "../../processing_clock_control.hpp"
#include <cpphdl_analog.h>
#include <VerilogA/experiment.hpp>
#include <sparse.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>

using namespace analog;
long _system_clock = -1;

int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    try {
        const std::filesystem::path destination(argv[1]);
        const bool native = std::string_view(argv[2]) == "run";
        if (!native && std::string_view(argv[2]) != "export")
            throw std::runtime_error("use run or export");
        std::filesystem::create_directories(destination);
        simulation sim;
        sim.analytic_jacobian = true;
        sim.minimum_step = duration(1e-13);
        use_sparse_solver(sim, sparse_strategy::certified_lu);
        electrical ground, source, vdd;
        sim.reference(ground);
        source = sim_voltage_source(ground, voltage(1.2));
        vdd = resistor(source, resistance(10));
        one_g::ihp_sg13g2::processing_clock_oscillator clock;
        clock.Vdd = vdd;
        clock.Vss = ground;
        auto controller = std::make_shared<ProcessingClockControl>();
        controller->enable_in = _ASSIGN(true);
        controller->_assign();
        int reference_identity = 0, oscillator_identity = 0;
        std::array<bits<1>, 17> controls;
        for (unsigned bit = 0; bit < controls.size(); ++bit) {
            if (native) {
                digital_operation operation{digital_law::sim_registered_output};
                operation.native_identity = &reference_identity;
                operation.read = [controller, bit]() -> bit_value {
                    ++_system_clock;
                    return bit == 16 ? unsigned(controller->clear_out()) :
                        bit < 8 ? ((unsigned(controller->coarse_n_out()) >> bit) & 1) :
                                  ((unsigned(controller->fine_n_out()) >> (bit - 8)) & 1);
                };
                controls[bit] = digital_purpose{&sim, 1, std::move(operation)};
            } else {
                // Replaced by the live host's DAC bridge in the SPICE flow.
                sim.drive(controls[bit], bit == 7 ? 0 : 1);
            }
            if (bit < 8)
                clock.COARSE_N[bit] = sim_driver(controls[bit], vdd, ground, resistance(50));
            else if (bit < 16)
                clock.FINE_N[bit - 8] = sim_driver(controls[bit], vdd, ground, resistance(50));
            else
                clock.CLEAR = sim_driver(controls[bit], vdd, ground, resistance(50));
        }
        clock.elaborate();
        clock.OUT = capacitor(ground, capacitance_value(20e-15));
        sim.observe("OUT", clock.OUT, ground);
        sim.observe("RING", clock.RING, ground);
        sim.observe("CONTROL", clock.CONTROL, ground);
        sim.observe("VDD", vdd, ground);
        sim.observe("CLEAR", clock.CLEAR, ground);
        const duration stop(64e-6), step(50e-12);
        if (!native) {
            verilog_a::write_experiment(sim, destination, {stop, step, duration(step.value / 2), 1e-16, 2, 1e-18});
            std::ofstream metadata(destination / "live.json");
            metadata << "{\"clock\":\"n" << sim.root(clock.OUT.id()) << "\",\"drivers\":[";
            for (unsigned bit = 0; bit < controls.size(); ++bit)
                metadata << (bit ? "," : "") << controls[bit].id();
            metadata << "],\"stop_s\":0.000064,\"step_s\":0.00000000005}\n";
            return 0;
        }
        bits<1> reference, achieved;
        sim.clock(reference, duration(8e-9));
        achieved = sim_schmitt_sense(clock.OUT, ground, voltage(.36), voltage(.84));
        std::ofstream records(destination / "controls.csv");
        records << "reference_edge,trim,clear,ready_ref,ready_pcs,fault,measured\n";
        int edge = -2;
        sim.register_clocked(reference, &reference_identity,
            [controller](bool reset) {
                ++_system_clock;
                if (reset) controller->_reset_pos_ref_clk();
                else controller->_work_ref_clk(false);
            },
            [&] {
                controller->_strobe_ref_clk();
                ++_system_clock;
                records << ++edge << ',' << (((unsigned(controller->coarse_n_out()) ^ 255) << 8) |
                    (unsigned(controller->fine_n_out()) ^ 255)) << ',' << controller->clear_out() << ','
                    << controller->ready_ref_out() << ',' << controller->ready_pcs_out() << ','
                    << controller->fault_out() << ',' << unsigned(controller->measured_edges_out()) << '\n';
            });
        sim.register_clocked(achieved, &oscillator_identity,
            [controller](bool reset) {
                ++_system_clock;
                if (reset) controller->_reset_pos_pcs_clk();
                else controller->_work_pcs_clk(false);
            },
            [controller] { controller->_strobe_pcs_clk(); });
        std::ofstream trace(destination / "trace.csv");
        trace << "time_s,OUT,RING,CONTROL,VDD,CLEAR\n" << std::setprecision(17);
        // Integer windows make the final endpoint exact. Accumulating a
        // floating duration can leave an extra sub-femtosecond continuation.
        for (unsigned window = 0; window < 250; ++window) {
            const double next = window == 249 ? stop.value : (window + 1) * 256e-9;
            if (window == 0)
                sim.run(duration(next), step);
            else
                sim.advance_to(duration(next), step);
            for (const auto& row : sim.trace) {
                trace << row.time;
                for (const auto& value : row.values) {
                    if (!value)
                        throw std::runtime_error("unknown closed-clock observation");
                    trace << ',' << *value;
                }
                trace << '\n';
            }
            sim.discard_trace();
        }
        ++_system_clock;
        if (!controller->ready_ref_out() || !controller->ready_pcs_out() || controller->fault_out())
            throw std::runtime_error("electrical oscillator did not acquire processing readiness");
        if (!records || !trace || sim.status() != "sampled_pass")
            throw std::runtime_error("incomplete closed-clock evidence");
        std::cout << "PASS autonomous electrical processing-clock acquisition\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
