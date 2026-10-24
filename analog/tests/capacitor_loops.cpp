#include <analog.hpp>
#ifdef CPPHDL_TEST_SPARSE
#include <sparse.hpp>
#endif
#include <array>
#include <iostream>

using namespace analog;

static void configure(simulation& sim, bool analytic) {
    sim.analytic_jacobian = analytic;
#ifdef CPPHDL_TEST_SPARSE
    use_sparse_solver(sim);
#endif
}

static void near(double actual, double expected, double tolerance) {
    if (std::abs(actual - expected) > tolerance)
        throw std::runtime_error("capacitor-loop voltage differs from independent solution");
}

static std::array<double, 2> exact_triangle(double time) {
    // C = [[4,-3],[-3,5]] nF, G = diag(1,0.5) mS. Closed-form exp(-C^-1 G t).
    double a = -5e-9 / (11e-18 * 1000), b = -3e-9 / (11e-18 * 2000);
    double c = -3e-9 / (11e-18 * 1000), d = -4e-9 / (11e-18 * 2000);
    double mean = (a + d) / 2, delta = std::sqrt((a - d) * (a - d) / 4 + b * c);
    double diagonal = std::cosh(delta * time), scale = std::sinh(delta * time) / delta;
    return {std::exp(mean * time) * (diagonal * .4 + scale * ((a - mean) * .4 + b * .1)),
            std::exp(mean * time) * (diagonal * .1 + scale * (c * .4 + (d - mean) * .1))};
}

static std::array<double, 2> triangle(std::array<unsigned, 3> order, bool analytic, double step) {
    simulation sim;
    configure(sim, analytic);
    electrical a, b, ground;
    sim.reference(ground);
    a = resistor(ground, resistance(1000));
    b = resistor(ground, resistance(2000));
    for (unsigned which : order) {
        if (which == 0)
            a = capacitor(ground, capacitance_value(1e-9), voltage(.4));
        else if (which == 1)
            b = capacitor(ground, capacitance_value(2e-9), voltage(.1));
        else
            a = capacitor(b, capacitance_value(3e-9), voltage(.3));
    }
    sim.observe("a", a, ground);
    sim.observe("b", b, ground);
    sim.observe("ab", a, b);
    sim.require_trace("all three initial charges", [](const std::vector<sample>& rows) -> std::optional<bool> {
        return rows.size() > 100 && rows.front().values[0] && rows.front().values[1] && rows.front().values[2] &&
               std::abs(*rows.front().values[0] - .4) < 1e-8 && std::abs(*rows.front().values[1] - .1) < 1e-8 &&
               std::abs(*rows.front().values[2] - .3) < 1e-8;
    });
    sim.run(duration(1e-6), duration(step));
    if (sim.status() != "sampled_pass")
        throw std::runtime_error("capacitor initial charge contract failed");
    return {sim.voltage(a, ground).value, sim.voltage(b, ground).value};
}

int main() {
    try {
        auto exact = exact_triangle(1e-6);
        std::array<unsigned, 3> order{0, 1, 2};
        unsigned runs = 0;
        do {
            for (bool analytic : {false, true}) {
                auto coarse = triangle(order, analytic, 2e-9);
                auto fine = triangle(order, analytic, 1e-9);
                for (unsigned i = 0; i < 2; ++i) {
                    near(fine[i], exact[i], 8e-5);
                    if (std::abs(fine[i] - exact[i]) >= std::abs(coarse[i] - exact[i]))
                        throw std::runtime_error("capacitor loop did not converge under step refinement");
                }
                runs += 2;
            }
        } while (std::next_permutation(order.begin(), order.end()));
        for (bool analytic : {false, true}) {
            {
                simulation sim;
                configure(sim, analytic);
                electrical input, node, ground;
                sim.reference(ground);
                input = sim_voltage_source(ground, voltage(1));
                node = resistor(input, resistance(1000));
                node = capacitor(ground, capacitance_value(1e-9), voltage(.5));
                node = capacitor(ground, capacitance_value(2e-9), voltage(.5));
                sim.run(duration(3e-6), duration(1e-9));
                // Both capacitors must contribute to the 3 us time constant.
                near(sim.voltage(node, ground).value, 1 - .5 / std::exp(1.), 8e-5);
            }
            {
                simulation sim;
                configure(sim, analytic);
                electrical node, ground;
                sim.reference(ground);
                node = sim_voltage_source(ground, voltage(1));
                node = capacitor(ground, capacitance_value(1e-9), voltage(1));
                sim.run(duration(1e-6), duration(1e-8));
                near(sim.voltage(node, ground).value, 1, 1e-10);
            }
            bool rejected = false;
            try {
                simulation sim;
                configure(sim, analytic);
                electrical node, ground;
                sim.reference(ground);
                node = resistor(ground, resistance(1000));
                node = capacitor(ground, capacitance_value(1e-9), voltage(.5));
                node = capacitor(ground, capacitance_value(2e-9), voltage(.6));
                sim.run(duration(1e-6), duration(1e-8));
            } catch (const diagnostic& error) {
                rejected = error.code == "E_INITIAL_CHARGE";
            }
            if (!rejected)
                throw std::runtime_error("inconsistent capacitor initial voltages were accepted");
            rejected = false;
            try {
                simulation sim;
                configure(sim, analytic);
                electrical node, ground;
                sim.reference(ground);
                node = sim_voltage_source(ground, voltage(0), duration(1e-6), voltage(1));
                node = capacitor(ground, capacitance_value(1e-9));
                sim.run(duration(2e-6), duration(1e-8));
            } catch (const diagnostic& error) {
                rejected = error.code == "E_CHARGE_DISCONTINUITY";
            }
            if (!rejected)
                throw std::runtime_error("unsupported ideal-source charge impulse was accepted");
        }
        std::cout << runs << " capacitor-mesh refinement runs; declaration-order invariance, both Jacobians, "
                  << "parallel charge, source clamp and inconsistent-state/impulse rejection passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
