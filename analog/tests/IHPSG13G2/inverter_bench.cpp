#include <basic/IHPSG13G2/inverter.hpp>
#include <behavior_devices.hpp>
#include <chrono>
#include <ctime>
#include <iostream>

namespace b = analog::behavior;

// Adding a capacitor changes both transistor trajectories through the shared
// electrical output. Inheritance retains the original two-device topology.
class loaded_inverter : public analog::ihp::inverter {
public:
    b::capacitor load;
    void _assign() override {
        analog::ihp::inverter::_assign();
        load.P = Y;
        load.N = Vss;
        load.elaborate();
    }
};

static_assert(std::is_base_of_v<analog::IC, loaded_inverter>);
static_assert(b::traits<analog::ihp::mos>::features.charge);

void configure(analog::ihp::mos& device, const char* path, const char* binary, double temperature) {
    std::ifstream bundle(path);
    std::string kind, module, name;
    std::map<std::string, double> model, instance;
    double value;
    if (!(bundle >> kind >> module) || kind != "module") throw std::runtime_error("invalid process bundle");
    while (bundle >> kind) {
        if (!(bundle >> name >> value)) throw std::runtime_error("truncated process bundle");
        auto* destination = kind == "model" ? &model : kind == "instance" ? &instance : nullptr;
        if (!destination || !destination->emplace(name, value).second)
            throw std::runtime_error("invalid model parameter record");
    }
    if (!bundle.eof()) throw std::runtime_error("truncated process bundle");
    device.process = std::make_shared<analog::osdi::model>(
        std::make_shared<analog::osdi::library>(binary, module), model);
    device.parameters = instance;
    device.temperature_K = temperature + 273.15;
}

// Resolve terminal-charge relaxation at PWL slope changes, using ordinary
// steps throughout the plateaus. The solver also lands on source breakpoints.
class inverter_steps : public b::fixed_step {
    double step_;
public:
    explicit inverter_steps(double step) : b::fixed_step(step), step_(step) {}
    double proposed(double time) const override {
        for (double corner : {.2e-9, .4e-9, 1.4e-9, 1.6e-9}) {
            if (time < corner - 8e-12 - step_ * 1e-9) return std::min(step_, corner - 8e-12 - time);
            if (time < corner - 1e-12 - step_ * 1e-9) return std::min(step_ / 8, corner - 1e-12 - time);
            if (time < corner + 1e-12) return step_ / 60;
            if (time < corner + 8e-12) return step_ / 8;
        }
        return step_;
    }
};

int main(int argc, char** argv) {
    try {
        if (argc != 10) throw std::runtime_error(
            "usage: inverter_bench N_BUNDLE P_BUNDLE OSDI OUT TEMPERATURE_C SUPPLY_V LOAD_F dc|charge STEP_S");
        const auto start = std::chrono::steady_clock::now();
        const auto cpu_start = std::clock();
        const double temperature = std::stod(argv[5]), voltage = std::stod(argv[6]);
        const std::string mode = argv[8];
        if (mode != "dc" && mode != "charge") throw std::runtime_error("invalid analysis");
        analog::simulation context;
        loaded_inverter dut;
        b::sim_voltage_source supply;
        b::sim_pwl_voltage_source input;
        configure(dut.pull_down, argv[1], argv[3], temperature);
        configure(dut.pull_up, argv[2], argv[3], temperature);
        dut.load.capacitance = analog::capacitance_value(std::stod(argv[7]));
        context.reference(dut.Vss);
        supply.P = dut.Vdd;
        supply.N = dut.Vss;
        supply.level = analog::voltage(voltage);
        input.P = dut.A;
        input.N = dut.Vss;
        if (mode == "charge") input.points = {{0, 0}, {.2e-9, 0}, {.4e-9, voltage},
            {1.4e-9, voltage}, {1.6e-9, 0}, {3e-9, 0}};
        supply.elaborate();
        input.elaborate();
        dut.elaborate();
        b::network net(context);
        net.compile();
        auto newton = std::make_shared<b::damped_newton>();
        newton->tolerance.current = 1e-13;
        newton->tolerance.voltage = 1e-10;
        std::filesystem::path output = argv[4];
        std::filesystem::create_directories(output);
        std::ofstream trace(output / "cpp.csv");
        trace << std::setprecision(17);
        std::size_t points = 0;
        if (mode == "dc") {
            b::dense_solver linear;
            std::vector<double> zero(net.size()), x(net.size());
            trace << "input_V,output_V\n";
            for (int i = 0; i <= 20; ++i) {
                input.points = {{0, voltage * i / 20}};
                net.begin(0, 0);
                try {
                    x = newton->solve(net, 0, x, {0, 0, 0}, zero, zero, linear);
                    net.accept();
                } catch (...) { net.reject(); throw; }
                trace << input.points[0].second << ',' << x[net.index(dut.Y)] << '\n';
                ++points;
            }
        } else {
            b::coupled_solver transient;
            transient.nonlinear = newton;
            inverter_steps steps(std::stod(argv[9]));
            const auto waveform = transient.run(net, 3e-9, steps);
            trace << "time_s,output_V,input_V\n";
            for (const auto& point : waveform)
                trace << point.time << ',' << point.solution[net.index(dut.Y)] << ','
                      << point.solution[net.index(dut.A)] << '\n';
            points = waveform.size();
        }
        trace.close();
        if (!trace) throw std::runtime_error("native trace output failed");
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const double cpu = double(std::clock() - cpu_start) / CLOCKS_PER_SEC;
        std::ofstream receipt(output / "native.json");
        receipt << std::setprecision(17) << "{\"engine\":\"cpphdl_osdi\",\"points\":" << points
                << ",\"unknowns\":" << net.size() << ",\"evaluations\":"
                << dut.pull_up.evaluations + dut.pull_down.evaluations
                << ",\"simulation_and_output_wall_s\":" << wall << ",\"cpu_s\":" << cpu << "}\n";
        receipt.close();
        if (!receipt) throw std::runtime_error("native receipt output failed");
        std::cout << "PASS coupled IHP inverter " << mode << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
