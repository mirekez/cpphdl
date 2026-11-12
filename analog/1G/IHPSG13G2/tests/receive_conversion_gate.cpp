#include "../core_receive_slice.hpp"
#include <iostream>

using namespace analog;

int main()
{
    try {
        for (unsigned inputs = 0; inputs < 4; ++inputs) {
            simulation sim;
            sim.analytic_jacobian = true;
            sim.minimum_step = duration(1e-14);
            electrical ground, supply, vdd, evaluate, convert;
            sim.reference(ground);
            supply = sim_pwl_voltage_source(ground, {{0, 0}, {2e-9, 1.2}});
            vdd = resistor(supply, resistance(1));
            vdd = capacitor(ground, capacitance_value(1e-12));
            evaluate = sim_pwl_voltage_source(ground,
                {{0, 0}, {2e-9, (inputs & 1) ? 1.2 : 0.0}});
            convert = sim_pwl_voltage_source(ground,
                {{0, 0}, {2e-9, (inputs & 2) ? 1.2 : 0.0}});
            one_g::ihp_sg13g2::receive_conversion_gate gate;
            gate.Vdd = vdd;
            gate.Vss = ground;
            gate.EVALUATE = resistor(evaluate, resistance(50));
            gate.CONVERT = resistor(convert, resistance(50));
            gate.elaborate();
            gate.OUT = capacitor(ground, capacitance_value(40e-15));
            sim.run(duration(10e-9), duration(10e-12));
            const double output = sim.voltage(gate.OUT, ground).value;
            const bool high = inputs == 3;
            if (sim.status() != "sampled_pass"
                || !(high ? output > 1.1 : output < 0.1)) {
                std::cerr << "conversion gate inputs=" << inputs
                          << " output=" << output << '\n';
                return 1;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
