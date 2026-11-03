#include <basic/IHPSG13G2/mos.hpp>
#include <behavior_devices.hpp>
#include <chrono>
#include <ctime>
#include <iostream>

namespace b = analog::behavior;
static_assert(std::is_base_of_v<analog::IC, analog::ihp::mos>);
static_assert(b::traits<analog::ihp::mos>::features.charge);
static_assert(b::traits<analog::ihp::mos>::features.rollback);

// Resolve intrinsic-charge relaxation at source corners without increasing
// the duration or using tiny steps throughout the whole smoke workload.
class charge_steps : public b::fixed_step {
    double step_;
public:
    explicit charge_steps(double step) : b::fixed_step(step), step_(step) {}
    double proposed(double time) const override {
        for (double corner : {.2e-9, .4e-9, 1e-9, 1.2e-9}) {
            if (time < corner - 8e-12 - step_ * 1e-9) return std::min(step_, corner - 8e-12 - time);
            if (time < corner - 1e-12 - step_ * 1e-9) return std::min(step_ / 8, corner - 1e-12 - time);
            if (time < corner + 1e-12) return step_ / 60;
            if (time < corner + 8e-12) return step_ / 8;
        }
        return step_;
    }
};

class transistor_load : public analog::IC {
public:
    analog::ihp::mos transistor;
    b::sim_voltage_source supply;
    b::sim_pwl_voltage_source gate;
    b::resistor load;
    bool pmos = false;
    void _assign() override {
        supply.P = Vdd;
        supply.N = Vss;
        supply.level = analog::voltage(1.2);
        gate.N = Vss;
        transistor.G = gate.P;
        transistor.S = pmos ? Vdd : Vss;
        transistor.B = transistor.S;
        load.P = pmos ? Vss : Vdd;
        load.N = transistor.D;
        load.resistance = analog::resistance(1000);
        supply.elaborate();
        gate.elaborate();
        transistor.elaborate();
        load.elaborate();
    }
};

void check_derivatives(b::network& net, const std::vector<double>& x) {
    const auto n = net.size();
    net.begin(0, 1e-12);
    const auto equation = net.evaluate(1e-12, x);
    std::vector<double> g(n * n), c(n * n);
    for (const auto& entry : equation.conductance) g[entry.row * n + entry.column] += entry.value;
    for (const auto& entry : equation.capacitance) c[entry.row * n + entry.column] += entry.value;
    bool has_charge = false;
    for (std::size_t column = 0; column < n; ++column) {
        // Check all physical potential derivatives. Current-valued auxiliary
        // coordinates include PSP's small-signal noise injection, which is
        // constrained to zero in these deterministic DC/transient analyses.
        if (net.voltage_row(column)) continue;
        auto plus = x, minus = x;
        constexpr double delta = 1e-6;
        plus[column] += delta;
        minus[column] -= delta;
        const auto high = net.evaluate(1e-12, plus), low = net.evaluate(1e-12, minus);
        for (std::size_t row = 0; row < n; ++row) {
            const double numerical_g = (high.current[row] - low.current[row]) / (2 * delta);
            const double numerical_c = (high.charge[row] - low.charge[row]) / (2 * delta);
            const double analytic_g = g[row * n + column], analytic_c = c[row * n + column];
            if (std::abs(numerical_g - analytic_g) > 1e-9 + 1e-4 * std::abs(analytic_g) ||
                std::abs(numerical_c - analytic_c) > 1e-21 + 1e-4 * std::abs(analytic_c)) {
                std::ostringstream message;
                message << std::setprecision(17) << "compact-model Jacobian row=" << row << " column=" << column
                        << " G=" << analytic_g << " numeric=" << numerical_g
                        << " C=" << analytic_c << " numeric=" << numerical_c;
                throw std::runtime_error(message.str());
            }
            has_charge |= std::abs(analytic_c) > 1e-18;
        }
    }
    if (!has_charge) throw std::runtime_error("process terminal charge was lost");
    net.reject();
    net.begin(0, 1e-12);
    const auto restored = net.evaluate(1e-12, x);
    if (restored.current != equation.current || restored.charge != equation.charge)
        throw std::runtime_error("process trial rollback changed the evaluated equations");
    net.reject();
}

int main(int argc, char** argv) {
    try {
        if (argc != 8) throw std::runtime_error("usage: process_bench BUNDLE OSDI OUT TEMPERATURE_C POLARITY dc|charge|contract STEP_S");
        const auto start = std::chrono::steady_clock::now();
        const auto cpu_start = std::clock();
        std::ifstream bundle(argv[1]);
        std::string kind, name, module;
        std::map<std::string, double> model_values, instance_values;
        if (!(bundle >> kind >> module) || kind != "module") throw std::runtime_error("invalid process bundle");
        double value;
        while (bundle >> kind) {
            if (!(bundle >> name >> value)) throw std::runtime_error("truncated process bundle");
            auto* destination = kind == "model" ? &model_values : kind == "instance" ? &instance_values : nullptr;
            if (!destination || !destination->emplace(name, value).second) throw std::runtime_error("invalid model parameter record");
        }
        if (!bundle.eof()) throw std::runtime_error("truncated process bundle");
        auto binary = std::make_shared<analog::osdi::library>(argv[2], module);
        auto model = std::make_shared<analog::osdi::model>(binary, model_values);
        analog::simulation context;
        transistor_load dut;
        if (std::string(argv[5]) != "p" && std::string(argv[5]) != "n") throw std::runtime_error("invalid polarity");
        const std::string mode = argv[6];
        if (mode != "dc" && mode != "charge" && mode != "contract") throw std::runtime_error("invalid analysis");
        dut.pmos = std::string(argv[5]) == "p";
        context.reference(dut.Vss);
        dut.transistor.process = model;
        dut.transistor.parameters = instance_values;
        dut.transistor.temperature_K = std::stod(argv[4]) + 273.15;
        if (mode == "charge") dut.gate.points = {{0, 0}, {.2e-9, 0}, {.4e-9, 1.2},
            {1e-9, 1.2}, {1.2e-9, 0}, {2e-9, 0}};
        dut.elaborate();
        b::network net(context);
        net.compile();
        b::damped_newton solver;
        solver.tolerance.current = 1e-13;
        solver.tolerance.voltage = 1e-10;
        b::dense_solver linear;
        std::vector<double> zero(net.size()), x(net.size());
        std::filesystem::path work = argv[3];
        std::filesystem::create_directories(work);
        std::ofstream trace(work / "cpp.csv");
        trace << std::setprecision(17);
        std::size_t points = 0;
        if (mode != "charge") {
            trace << "gate_V,drain_V,current_A\n";
            for (double gate : {0., .3, .6, .9, 1.2}) {
                dut.gate.points = {{0, gate}};
                net.begin(0, 0);
                try {
                    x = solver.solve(net, 0, x, {0, 0, 0}, zero, zero, linear);
                    net.accept();
                } catch (...) { net.reject(); throw; }
                const double drain = x[net.index(dut.transistor.D)];
                trace << gate << ',' << drain << ',' << (dut.pmos ? drain : 1.2 - drain) / 1000 << '\n';
                if (mode == "contract") {
                    try { check_derivatives(net, x); }
                    catch (...) {
                        for (const auto& [name, row] : dut.transistor.node_mapping()) std::cerr << name << '=' << row << ' ';
                        std::cerr << '\n';
                        throw;
                    }
                }
                ++points;
            }
        } else {
            b::coupled_solver transient;
            auto newton = std::make_shared<b::damped_newton>();
            newton->tolerance = solver.tolerance;
            transient.nonlinear = newton;
            charge_steps steps(std::stod(argv[7]));
            const auto waveform = transient.run(net, 2e-9, steps);
            trace << "time_s,drain_V,gate_V\n";
            for (const auto& point : waveform)
                trace << point.time << ',' << point.solution[net.index(dut.transistor.D)] << ','
                      << point.solution[net.index(dut.transistor.G)] << '\n';
            points = waveform.size();
        }
        trace.close();
        if (!trace) throw std::runtime_error("native output failed");
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const double cpu = double(std::clock() - cpu_start) / CLOCKS_PER_SEC;
        std::ofstream receipt(work / "native.json");
        receipt << std::setprecision(17) << "{\"engine\":\"cpphdl_osdi\",\"points\":" << points << ",\"unknowns\":"
                << net.size() << ",\"evaluations\":" << dut.transistor.evaluations
                << ",\"simulation_and_output_wall_s\":" << wall << ",\"cpu_s\":" << cpu << "}\n";
        std::cout << "PASS native IHP PSP " << mode << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
