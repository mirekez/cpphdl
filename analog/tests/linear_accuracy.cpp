#include <analog.hpp>
#include <iostream>

using namespace analog;

static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

static void exercise(bool subdivision, const std::string& code, bool persistent = false, bool initial = false) {
    simulation sim;
    sim.analytic_jacobian = true;
    const double tau = 1e-9, step = tau / 32;
    if (subdivision)
        sim.minimum_step = duration(tau / 4096);
    electrical supply, output, ground;
    sim.reference(ground);
    supply = sim_voltage_source(ground, voltage(1));
    output = resistor(supply, resistance(1000));
    output = capacitor(ground, capacitance_value(1e-12));
    sim.observe("output", output, ground);
    unsigned rejected = 0;
    sim.linear_backend = [&](std::size_t size, const std::vector<matrix_entry>& entries, const std::vector<double>& right) {
        if ((initial || !sim.trace.empty()) && (persistent || rejected == 0)) {
            ++rejected;
            throw diagnostic(code, "deliberately rejected backend direction");
        }
        std::vector<std::vector<double>> matrix(size, std::vector<double>(size));
        for (const auto& entry : entries)
            matrix[entry.row][entry.column] += entry.value;
        return linear_solve(std::move(matrix), right);
    };
    const bool should_pass = subdivision && code == "E_LINEAR_ACCURACY" && !persistent && !initial;
    bool passed = false;
    try {
        sim.run(duration(5 * tau), duration(step));
        passed = true;
    } catch (const diagnostic& error) {
        require(error.code == code, "rejected direction changed the diagnostic category");
    }
    require(passed == should_pass, "accuracy rejection was accepted or subdivision was not honored");
    require(rejected >= 1, "fault injection did not reach the equation backend");
    if (passed) {
        require(rejected == 1 && std::abs(sim.trace[1].time - step / 2) < 1e-24,
                "rejected full step did not produce an independently visible half step");
        for (const auto& sample : sim.trace)
            require(sample.values[0] && std::abs(*sample.values[0] - (1 - std::exp(-sample.time / tau))) < .007,
                    "retry corrupted the capacitor state or missed its independent RC pole");
    } else {
        require(sim.trace.size() <= 1, "an invalid Newton direction advanced physical time");
        if (persistent && subdivision && !initial)
            require(rejected > 1 && rejected < 16, "minimum-step bound did not terminate persistent failure");
    }
}

int main() {
    try {
        exercise(true, "E_LINEAR_ACCURACY");
        exercise(false, "E_LINEAR_ACCURACY");
        exercise(true, "E_LINEAR_ACCURACY", true);
        exercise(true, "E_LINEAR_ACCURACY", false, true);
        exercise(true, "E_SINGULAR");
        exercise(true, "E_LINEAR_SOLVER");
        std::cout << "PASS rejected-direction rollback, independent RC response, fixed-step rejection, "
                     "bounded subdivision and fatal structural/initial failures\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
