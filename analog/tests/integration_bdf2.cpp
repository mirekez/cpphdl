#include <analog.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace relaxation {
using namespace analog;

static double relaxation(bool inductor_case, bool second_order, double step, bool split = false,
                         bool reject = false, bool analytic = true) {
    simulation sim;
    sim.method = second_order ? integration_method::bdf2 : integration_method::backward_euler;
    sim.analytic_jacobian = analytic;
    sim.minimum_step = duration(step / 16);
    electrical ground, supply, node;
    sim.reference(ground);
    supply = sim_voltage_source(ground, voltage(1));
    node = resistor(supply, resistance(1000));
    if (inductor_case)
        node = inductor(ground, inductance_value(.001));
    else
        node = capacitor(ground, capacitance_value(1e-9));
    sim.observe("response", inductor_case ? supply : node, inductor_case ? node : ground);
    const double tau = 1e-6;
    if (split || reject) {
        sim.run(duration(tau), duration(step));
        if (reject) {
            auto once = std::make_shared<bool>(true);
            sim.linear_backend = [once](std::size_t count, const std::vector<matrix_entry>& entries,
                                       const std::vector<double>& rhs) {
                if (*once) {
                    *once = false;
                    throw diagnostic("E_LINEAR_ACCURACY", "deliberate rejected Newton step");
                }
                std::vector<std::vector<double>> matrix(count, std::vector<double>(count));
                for (const auto& e : entries)
                    matrix[e.row][e.column] += e.value;
                return linear_solve(matrix, rhs);
            };
        }
        sim.advance_to(duration(5 * tau), duration(step / (split ? 2 : 1)));
    } else
        sim.run(duration(5 * tau), duration(step));
    double maximum = 0;
    for (const auto& row : sim.trace) {
        const double expected = 1 - std::exp(-row.time / tau);
        maximum = std::max(maximum, std::abs(*row.values[0] - expected));
    }
    if (sim.status() != "sampled_pass")
        throw std::runtime_error("failed analytic transient");
    return maximum;
}

static double oscillator(bool second_order, unsigned steps) {
    simulation sim;
    sim.method = second_order ? integration_method::bdf2 : integration_method::backward_euler;
    sim.analytic_jacobian = true;
    electrical ground, node;
    sim.reference(ground);
    node = capacitor(ground, capacitance_value(1e-9), voltage(1));
    node = inductor(ground, inductance_value(.001));
    sim.observe("voltage", node, ground);
    const double period = 2 * std::acos(-1.) * 1e-6;
    sim.run(duration(3 * period), duration(period / steps));
    double maximum = 0;
    for (const auto& row : sim.trace)
        maximum = std::max(maximum, std::abs(*row.values[0] - std::cos(row.time / 1e-6)));
    return maximum;
}

void check() {
    for (bool coil : {false, true}) {
        const double coarse = relaxation(coil, true, .05e-6);
        const double fine = relaxation(coil, true, .025e-6);
        const double euler = relaxation(coil, false, .025e-6);
        const double segmented = relaxation(coil, true, .025e-6, true);
        const double retried = relaxation(coil, true, .025e-6, false, true);
        const double numeric = relaxation(coil, true, .025e-6, false, false, false);
        std::cout << (coil ? "RL" : "RC") << " coarse=" << coarse << " fine=" << fine
                  << " Euler=" << euler << " segmented=" << segmented << " retried=" << retried
                  << " numeric=" << numeric << '\n';
        if (coarse / fine < 3.5 || fine >= .0005 || fine >= euler / 5 ||
                segmented >= .0005 || retried >= .0005 || std::abs(numeric - fine) > 1e-7)
            throw std::runtime_error("relaxation convergence, continuation or retry failure");
    }
    const double coarse = oscillator(true, 100), fine = oscillator(true, 200);
    const double euler = oscillator(false, 200);
    std::cout << "LC coarse=" << coarse << " fine=" << fine << " Euler=" << euler << '\n';
    if (coarse / fine < 3.5 || fine > .007 || fine >= euler / 10)
        throw std::runtime_error("oscillator convergence failure");
}
}

namespace events {
using namespace analog;

double error(bool triangle, double step, bool bdf) {
    simulation sim;
    sim.method = bdf ? integration_method::bdf2 : integration_method::backward_euler;
    sim.analytic_jacobian = true;
    electrical ground, source, node;
    sim.reference(ground);
    constexpr double tau = 1e-6;
    if (triangle)
        source = sim_pwl_voltage_source(ground, {{0, 0}, {tau, 1}, {2 * tau, 0}});
    else
        source = sim_voltage_source(ground, voltage(0), duration(.37 * tau), voltage(1));
    node = resistor(source, resistance(1000));
    node = capacitor(ground, capacitance_value(1e-9));
    sim.observe("response", node, ground);
    sim.run(duration(4 * tau), duration(step * tau));
    double maximum = 0;
    for (const auto& row : sim.trace) {
        const double t = row.time / tau;
        double expected = 0;
        if (triangle) {
            if (t <= 1)
                expected = t - 1 + std::exp(-t);
            else if (t <= 2)
                expected = 3 - t + (std::exp(-1) - 2) * std::exp(1 - t);
            else
                expected = std::pow(1 - std::exp(-1), 2) * std::exp(2 - t);
        } else if (t > .37)
            expected = 1 - std::exp(.37 - t);
        maximum = std::max(maximum, std::abs(*row.values[0] - expected));
    }
    return maximum;
}
void check() {
    for (bool triangle : {false, true}) {
        const double coarse = error(triangle, .037, true), fine = error(triangle, .0185, true);
        const double euler = error(triangle, .0185, false);
        std::cout << (triangle ? "PWL" : "STEP") << " coarse=" << coarse << " fine=" << fine
                  << " Euler=" << euler << '\n';
        if (coarse / fine < 3 || fine > .0005 || fine >= euler / 4)
            throw std::runtime_error("source-event convergence failure");
    }
}
}

namespace boundaries {
using namespace analog;

double error(bool triangle, double step, bool bdf) {
    simulation sim;
    sim.method = bdf ? integration_method::bdf2 : integration_method::backward_euler;
    sim.analytic_jacobian = true;
    electrical ground, source, node;
    sim.reference(ground);
    constexpr double tau = 1e-6;
    if (triangle)
        source = sim_pwl_voltage_source(ground, {{0, 0}, {tau, 1}, {2 * tau, 0}});
    else
        source = sim_voltage_source(ground, voltage(0), duration(.37 * tau), voltage(1));
    node = resistor(source, resistance(1000));
    node = capacitor(ground, capacitance_value(1e-9));
    sim.observe("response", node, ground);
    const double boundary = triangle ? tau : .37 * tau;
    sim.run(duration(boundary), duration(step * tau));
    sim.advance_to(duration(4 * tau), duration(step * tau));
    double maximum = 0;
    for (const auto& row : sim.trace) {
        const double t = row.time / tau;
        double expected = 0;
        if (triangle) {
            if (t <= 1)
                expected = t - 1 + std::exp(-t);
            else if (t <= 2)
                expected = 3 - t + (std::exp(-1) - 2) * std::exp(1 - t);
            else
                expected = std::pow(1 - std::exp(-1), 2) * std::exp(2 - t);
        } else if (t > .37)
            expected = 1 - std::exp(.37 - t);
        maximum = std::max(maximum, std::abs(*row.values[0] - expected));
    }
    return maximum;
}
void check() {
    std::cout << std::unitbuf;
    for (bool triangle : {false, true}) {
        const double coarse = error(triangle, .037, true), fine = error(triangle, .0185, true);
        const double euler = error(triangle, .0185, false);
        std::cout << (triangle ? "PWL" : "STEP") << " coarse=" << coarse << " fine=" << fine
                  << " Euler=" << euler << '\n';
        if (coarse / fine < 3 || fine > .0005 || fine >= euler / 4)
            throw std::runtime_error("source-event convergence failure");
    }
}
}

namespace sensing {
using namespace analog;

double response_error(double step) {
    simulation sim;
    sim.method = integration_method::bdf2;
    sim.analytic_jacobian = true;
    electrical ground, ramp, supply, output;
    bits<1> enable;
    constexpr double tau = 1e-6;
    sim.reference(ground);
    supply = sim_voltage_source(ground, voltage(1));
    ramp = sim_pwl_voltage_source(ground, {{0, 0}, {4 * tau, 4}});
    enable = sim_schmitt_sense(ramp, ground, voltage(.35), voltage(.85));
    // Seed the simulation observer consistently with the specified 0 V ramp;
    // this fixture tests a later event, not unknown-state driver startup.
    sim.digital[enable.id()].value = 0;
    output = sim_driver(enable, supply, ground, resistance(1000));
    output = capacitor(ground, capacitance_value(1e-9));
    sim.observe("output", output, ground);
    sim.observe("enable", enable);
    sim.run(duration(4 * tau), duration(step * tau));
    double crossing = -1, initial_voltage = 0, maximum = 0;
    for (const auto& row : sim.trace) {
        if (crossing < 0 && row.exact_bits[1] == bit_value(1)) {
            crossing = row.time;
            initial_voltage = *row.values[0];
        }
        // Start the analytic continuation from the accepted switching state.
        // Threshold localization within its preceding step is a separate
        // first-order event error; this checks only post-event history.
        const double expected = crossing < 0 ? 0 :
            1 - (1 - initial_voltage) * std::exp(-(row.time - crossing) / tau);
        maximum = std::max(maximum, std::abs(*row.values[0] - expected));
    }
    if (crossing < .85 * tau - step * tau || crossing > .85 * tau + step * tau)
        throw std::runtime_error("missing or invalid analog threshold event");
    return maximum;
}

void check() {
    const double coarse = response_error(.05), fine = response_error(.025);
    std::cout << "sensed RC edge coarse=" << coarse << " fine=" << fine << std::endl;
    if (coarse / fine < 3.5 || fine > .0005)
        throw std::runtime_error("sensed event failed second-order convergence");
}
}

namespace nonlinear {
using namespace analog;

// A diode-connected square-law MOS discharges a capacitor. The independent
// solution is Vth + u0 / (1 + beta * u0 * t / (2 * C)).
double mos_discharge(double step, bool second_order, bool analytic) {
    simulation sim;
    sim.method = second_order ? integration_method::bdf2 : integration_method::backward_euler;
    sim.analytic_jacobian = analytic;
    electrical ground, node;
    sim.reference(ground);
    node = capacitor(ground, capacitance_value(1e-9), voltage(1));
    ground = nmos(node, node, mos_model{channel_strength(.001), voltage(.35)});
    sim.observe("voltage", node, ground);
    sim.run(duration(10e-6), duration(step));
    double error = 0;
    for (const auto& row : sim.trace) {
        const double expected = .35 + .65 / (1 + .001 * .65 * row.time / (2e-9));
        error = std::max(error, std::abs(*row.values[0] - expected));
    }
    if (sim.status() != "sampled_pass")
        throw std::runtime_error("MOS discharge did not complete");
    return error;
}

// C = [[1.5, -.5], [-.5, 2.5]] nF, G = identity / 1 kohm.
// Its two eigenmodes give an exact coupled response, including the floating
// capacitor's voltage difference and both initial charges.
double coupled_storage(double step, bool second_order, bool analytic) {
    simulation sim;
    sim.method = second_order ? integration_method::bdf2 : integration_method::backward_euler;
    sim.analytic_jacobian = analytic;
    electrical ground, a, b;
    sim.reference(ground);
    a = capacitor(ground, capacitance_value(1e-9), voltage(1));
    b = capacitor(ground, capacitance_value(2e-9), voltage(.2));
    a = capacitor(b, capacitance_value(.5e-9), voltage(.8));
    a = resistor(ground, resistance(1000));
    b = resistor(ground, resistance(1000));
    sim.observe("a", a, ground);
    sim.observe("b", b, ground);
    sim.observe("difference", a, b);
    sim.run(duration(12e-6), duration(step));
    double error = 0;
    const double lambda_low = 2 - std::sqrt(.5), lambda_high = 2 + std::sqrt(.5);
    // The low-eigenvalue projector is (lambda_high I - C) / eigenvalue_gap.
    const double low_a = ((lambda_high - 1.5) + .5 * .2) / (lambda_high - lambda_low);
    const double low_b = (.5 + (lambda_high - 2.5) * .2) / (lambda_high - lambda_low);
    for (const auto& row : sim.trace) {
        const double slow = std::exp(-row.time / (lambda_high * 1e-6));
        const double fast = std::exp(-row.time / (lambda_low * 1e-6));
        const double expected_a = low_a * fast + (1 - low_a) * slow;
        const double expected_b = low_b * fast + (.2 - low_b) * slow;
        error = std::max({error, std::abs(*row.values[0] - expected_a),
            std::abs(*row.values[1] - expected_b),
            std::abs(*row.values[2] - expected_a + expected_b)});
    }
    return error;
}

void check() {
    std::cout << std::unitbuf;
    for (bool coupled : {false, true}) {
        const auto measure = coupled ? coupled_storage : mos_discharge;
        const double coarse = measure(.05e-6, true, true);
        const double fine = measure(.025e-6, true, true);
        const double euler = measure(.025e-6, false, true);
        const double numeric = measure(.025e-6, true, false);
        std::cout << (coupled ? "coupled capacitors" : "nonlinear MOS discharge")
                  << ": coarse=" << coarse << " fine=" << fine
                  << " Euler=" << euler << " numeric=" << numeric << '\n';
        if (coarse / fine < 3.5 || fine > .0005 || fine > euler / 5 ||
                std::abs(numeric - fine) > 1e-7)
            throw std::runtime_error("nonlinear or coupled storage convergence failure");
    }
}
}

int main() {
    std::cout << std::unitbuf;
    relaxation::check();
    events::check();
    boundaries::check();
    sensing::check();
    nonlinear::check();
}
