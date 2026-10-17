#include <analog.hpp>
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;

std::vector<sample> exercise(bool analytic, bool reverse, bool pmos_device) {
    simulation sim;
    sim.analytic_jacobian = analytic;
    electrical ground, supply, gate, output, source, sense, gm_output, clipped;
    sim.reference(ground);
    supply = sim_voltage_source(ground, voltage(1.2));
    gate = sim_pwl_voltage_source(ground, {{0, .2}, {20e-12, .2}, {30e-12, .8}, {80e-12, 1.1}});
    if (pmos_device) {
        ground = resistor(output, resistance(1000));
        if (reverse)
            supply = pmos(gate, output);
        else
            output = pmos(gate, supply);
    } else {
        output = resistor(supply, resistance(1000));
        if (reverse)
            output = nmos(gate, ground);
        else
            ground = nmos(gate, output);
    }
    output = capacitor(ground, capacitance_value(20e-15), voltage(pmos_device ? 0 : 1.2));
    source = resistor(supply, resistance(90));
    source = inductor(sense, inductance_value(5e-9));
    ground = resistor(sense, resistance(10));
    gm_output = resistor(ground, resistance(1000));
    gm_output = transconductor(ground, gate, ground, transconductance(.001));
    clipped = resistor(gate, resistance(100));
    clipped = diode(ground, resistance(10), voltage(.25));
    sim.observe("MOS", output, ground);
    sim.observe("RL", sense, ground);
    sim.observe("GM", gm_output, ground);
    sim.observe("diode", clipped, ground);
    sim.run(duration(200e-12), duration(.5e-12));
    return sim.trace;
}

int main() {
    try {
        for (bool reverse : {false, true})
            for (bool p : {false, true}) {
                auto exact = exercise(true, reverse, p), numerical = exercise(false, reverse, p);
                if (exact.size() != numerical.size())
                    throw std::runtime_error("Jacobian schedules differ");
                for (std::size_t i = 0; i < exact.size(); ++i)
                    for (std::size_t col = 0; col < exact[i].values.size(); ++col)
                        if (std::abs(*exact[i].values[col] - *numerical[i].values[col]) > 1e-7)
                            throw std::runtime_error("analytic and finite-difference Newton solutions disagree");
            }
        simulation sim;
        sim.analytic_jacobian = true;
        electrical ground, stored;
        sim.reference(ground);
        stored = inductor(ground, inductance_value(1e-9), current(.01));
        ground = resistor(stored, resistance(100));
        sim.observe("stored energy decay", stored, ground);
        bool rejected = false;
        try {
            verilog_a::write_experiment(sim, "unused", {duration(100e-12), duration(.1e-12), duration(.1e-12)});
        } catch (const diagnostic& error) {
            rejected = error.code == "E_VERILOGA";
        }
        if (!rejected)
            throw std::runtime_error("nonzero inductor initialization was silently lost in export");
        sim.run(duration(100e-12), duration(.1e-12));
        if (std::abs(*sim.trace.front().values[0] + 1) > 1e-8 ||
            std::abs(*sim.trace[100].values[0] + std::exp(-1.0)) > .002)
            throw std::runtime_error("inductor initial current/energy decay is wrong");
        std::cout << "PASS analytic/finite-difference Jacobians, MOS polarity/reversal, RL state, gm and diode\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
