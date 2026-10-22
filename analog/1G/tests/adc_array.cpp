#ifdef CPPHDL_ADC_CORE
#include "../adc_core.hpp"
#include "../adc_receive.hpp"
using Controller = ADCReceiveController;
#else
#include "../adc_feedback.hpp"
#include "../adc_interleave.hpp"
using Controller = InterleavedSARController;
#endif
#include <cpphdl.hpp>
#include <sparse.hpp>
#include <VerilogA/experiment.hpp>
#include <array>
#include <iostream>
#include <tuple>
#include <cstdlib>

long _system_clock = -1;
using namespace analog;
constexpr double array_period = 4e-9;

struct control_record {
    int clock = -1;
    unsigned data = 0, valid = 0, slice = 0, fault = 0, acquire = 0, convert = 0;
    std::array<unsigned, 16> trial{};
#ifdef CPPHDL_ADC_CORE
    unsigned normalized = 128, normalized_valid = 0, normalized_slice = 0, clipped = 0;
#endif
};

static control_record capture(Controller& controller, int clock) {
    ++_system_clock;
    control_record record;
    record.clock = clock;
    record.data = unsigned(controller.data_out());
    record.valid = bool(controller.valid_out());
    record.slice = unsigned(controller.slice_out());
    record.fault = bool(controller.fault_out());
#ifdef CPPHDL_ADC_CORE
    record.normalized = unsigned(controller.normalized_out());
    record.normalized_valid = bool(controller.normalized_valid_out());
    record.normalized_slice = unsigned(controller.normalized_slice_out());
    record.clipped = bool(controller.clipped_out());
#endif
    for (unsigned i = 0; i < 16; ++i) {
        record.trial[i] = unsigned(controller.trial_out[i]());
        record.acquire |= unsigned(controller.acquire_out[i]()) << i;
        record.convert |= unsigned(controller.convert_out[i]()) << i;
    }
    return record;
}

static void write_control(std::ostream& file, const control_record& record, unsigned count) {
    file << record.clock << ',' << record.data << ',' << record.valid << ',' << record.slice << ',' << record.fault
         << ',' << record.acquire << ',' << record.convert;
    for (unsigned i = 0; i < count; ++i)
        file << ',' << record.trial[i];
#ifdef CPPHDL_ADC_CORE
    file << ',' << record.normalized << ',' << record.normalized_valid
         << ',' << record.normalized_slice << ',' << record.clipped;
#endif
    file << '\n';
}

static void control_header(std::ostream& file, unsigned count) {
    file << "clock,data,valid,slice,fault,acquire,convert";
    for (unsigned i = 0; i < count; ++i)
        file << ",trial" << i;
#ifdef CPPHDL_ADC_CORE
    file << ",normalized,normalized_valid,normalized_slice,clipped";
#endif
    file << '\n';
}

static int server(unsigned count) {
    if (count == 0 || count > 16)
        throw std::runtime_error("invalid live ADC slice count");
    Controller controller;
    unsigned feedback = 0;
    bool launch = false;
    controller.enable_in = _ASSIGN(true);
    controller.sample_enable_in = _ASSIGN(launch);
    controller.comparisons_in = _ASSIGN(feedback);
#ifdef CPPHDL_ADC_CORE
    controller.offset_in = _ASSIGN(128);
    controller.gain_in = _ASSIGN(424);
#endif
    controller._assign();
    ++_system_clock;
    controller._work(true);
    controller._strobe();
    control_header(std::cout, count);
    write_control(std::cout, capture(controller, -1), count);
    std::cout.flush();
    unsigned enabled;
    for (int clock = 0; std::cin >> feedback >> enabled; ++clock) {
        if (feedback >= 65536 || enabled > 1)
            throw std::runtime_error("invalid live ADC feedback");
        launch = enabled;
        ++_system_clock;
        controller._work(false);
        controller._strobe();
        write_control(std::cout, capture(controller, clock), count);
        std::cout.flush();
    }
    return 0;
}

template <class Read>
static digital_purpose registered_bit(std::shared_ptr<Controller> controller, Read read) {
    digital_operation op{digital_law::sim_registered_output};
    op.native_identity = controller.get();
    op.read = [controller, read]() -> bit_value {
        ++_system_clock;
        return std::uint64_t(bool(read(*controller)));
    };
    return {&simulation::current(), 1, std::move(op)};
}

template <std::size_t Count>
static int experiment(const std::filesystem::path& destination, bool native, double supply_resistance,
                      unsigned bursts) {
    if (bursts == 0 || bursts > 16 || (bursts != 1 && Count != 16))
        throw std::runtime_error("repeated operation requires sixteen slices and one to sixteen bursts");
    const unsigned launches = Count * bursts;
#ifdef CPPHDL_ADC_CORE
    const unsigned clocks = launches + 24;
#else
    const unsigned clocks = launches + 16;
#endif
    simulation sim;
    sim.analytic_jacobian = true;
    sim.minimum_step = duration(array_period / 65536);
    if (!std::getenv("CPPHDL_ADC_DENSE"))
        use_sparse_solver(sim, sparse_strategy::certified_lu);
    if (sim.linear_backend) {
        auto solve = sim.linear_backend;
        sim.linear_backend = [solve, destination](std::size_t size, const std::vector<matrix_entry>& matrix,
                                                 const std::vector<double>& right) {
            try {
                return solve(size, matrix, right);
            } catch (const diagnostic&) {
                std::filesystem::create_directories(destination);
                std::ofstream file(destination / "failed_linear_system.txt");
                file << std::setprecision(17) << size << ' ' << matrix.size() << '\n';
                for (double value : right)
                    file << value << '\n';
                for (const auto& value : matrix)
                    file << value.row << ' ' << value.column << ' ' << value.value << '\n';
                throw;
            }
        };
    }
    electrical source, vdd, vss, input_source, evaluate_source, clear;
    sim.reference(vss);
    source = sim_voltage_source(vss, voltage(3.3));
    vdd = resistor(source, resistance(supply_resistance));
    vdd = capacitor(vss, capacitance_value(20e-12), voltage(3.3));
    clear = sim_pwl_voltage_source(vss, {{0, 3.3}, {2e-9, 3.3}, {2.1e-9, 0}});
    using wave = std::vector<std::pair<double, double>>;
    wave input{{0, 1.65}}, evaluate{{0, 0}};
    auto change = [](wave& points, double time, double value) {
        if (points.back().second != value) {
            points.emplace_back(time, points.back().second);
            points.emplace_back(time + .1e-9, value);
        }
    };
    for (unsigned i = 0; i < clocks; ++i) {
        change(input, i * array_period + 1e-9, 1.15 + .25 * ((i * 3) % 5));
        if (i) {
            change(evaluate, i * array_period, 3.3);
            change(evaluate, i * array_period + .45 * array_period, 0);
        }
    }
    input_source = sim_pwl_voltage_source(vss, input);
    evaluate_source = sim_pwl_voltage_source(vss, evaluate);
#ifdef CPPHDL_ADC_CORE
    electrical logic_source, vlogic;
    logic_source = sim_voltage_source(vss, voltage(1.2));
    vlogic = resistor(logic_source, resistance(10));
    vlogic = capacitor(vss, capacitance_value(20e-12));
    one_g::SARADCCoreArray<Count> array;
    array.Vlogic = vlogic;
#else
    one_g::SARADCArray<Count> array;
#endif
    array.IN = resistor(input_source, resistance(50));
    array.EVALUATE = resistor(evaluate_source, resistance(50));
    array.EVALUATE = capacitor(vss, capacitance_value(Count * .1e-12));
    array.CLEAR = clear;
    array.Vdd = vdd;
    array.Vss = vss;
    array.elaborate();
    std::array<electrical, Count> feedback_nodes;
    for (unsigned i = 0; i < Count; ++i) {
#ifdef CPPHDL_ADC_CORE
        feedback_nodes[i] = array.CORE_DECISION[i];
        // Model the controller input's actual load on the restored core node.
        feedback_nodes[i] = capacitor(vss, capacitance_value(20e-15));
        sim.observe("CORE_DECISION" + std::to_string(i), feedback_nodes[i], vss);
#else
        feedback_nodes[i] = array.DECISION[i];
#endif
    }
#ifdef CPPHDL_ADC_CORE
    sim.observe("VLOGIC", vlogic, vss);
#endif
    bits<1> reference, launch;
    sim.clock(reference, duration(array_period));
    sim.step(launch, duration(array_period / 2 + launches * array_period - 1e-9), 1, 0);
    auto controller = std::make_shared<Controller>();
    std::array<std::array<bits<1>, 9>, Count> controls;
    std::vector<std::tuple<unsigned, unsigned, std::size_t>> bindings;
    for (unsigned i = 0; i < Count; ++i) {
        for (unsigned bit = 0; bit < 9; ++bit) {
            auto& signal = controls[i][bit];
            if (native) {
                signal = registered_bit(controller, [i, bit](auto& node) {
                    return bit == 8 ? bool(node.acquire_out[i]()) : ((unsigned(node.trial_out[i]()) >> bit) & 1) == 0;
                });
            } else {
                sim.drive(signal, bit == 8 ? 0 : 1);
            }
            if (bit == 8)
                array.TRACK[i] = sim_driver(signal, vdd, vss, resistance(50));
            else
                array.BIT_N[i][bit] = sim_driver(signal, vdd, vss, resistance(50));
            bindings.emplace_back(i, bit, signal.id());
        }
    }
    std::vector<control_record> records;
    std::array<bits<1>, Count> decisions;
    bool resetting = true;
    if (native) {
        for (unsigned i = 0; i < Count; ++i)
            decisions[i] = sim_sense(feedback_nodes[i], vss,
#ifdef CPPHDL_ADC_CORE
                voltage(.36), voltage(.84));
#else
                voltage(.99), voltage(2.31));
#endif
        controller->enable_in = _ASSIGN(true);
        controller->sample_enable_in = _ASSIGN(!resetting && bool(sim_cpphdl_value(launch)));
#ifdef CPPHDL_ADC_CORE
        controller->offset_in = _ASSIGN(128);
        controller->gain_in = _ASSIGN(424);
#endif
        controller->comparisons_in = _ASSIGN([&] {
            unsigned mask = 0;
            for (unsigned i = 0; i < Count; ++i)
                if (!resetting && controller->convert_out[i]()) {
                    try {
                        mask |= unsigned(sim_cpphdl_value(decisions[i])) << i;
                    } catch (const diagnostic& error) {
                        throw std::runtime_error("unresolved ADC decision at slice " + std::to_string(i)
                            + ", controller clock " + std::to_string(records.size()) + ": " + error.what());
                    }
                }
            return mask;
        }());
        controller->_assign();
        sim.register_clocked(reference, controller.get(),
            [controller, &resetting](bool reset) {
                resetting = reset;
                ++_system_clock;
                controller->_work(reset);
            },
            [controller, &records, &resetting, clocks] {
                controller->_strobe();
                records.push_back(capture(*controller, resetting ? -1 : int(records.size()) - 1));
                if constexpr (Count == 16)
                    std::cerr << "ADC controller edge " << records.size() - 1 << " of " << clocks << '\n';
            });
    }
    for (unsigned i = 0; i < Count; ++i) {
        sim.observe("DECISION" + std::to_string(i), array.DECISION[i], vss);
        sim.observe("HELD" + std::to_string(i), array.slices[i].sampler.OUT, vss);
    }
    sim.observe("POSITIVE0", array.slices[0].POSITIVE, vss);
    sim.observe("NEGATIVE0", array.slices[0].NEGATIVE, vss);
    sim.observe("INPUT", array.IN, vss);
    sim.observe("COMMAND", input_source, vss);
    sim.observe("VDD", vdd, vss);
    sim.observe("EVALUATE", array.EVALUATE, vss);
    std::filesystem::create_directories(destination);
    if (native) {
        try {
            sim.run(duration(clocks * array_period), duration(array_period / 1024));
        } catch (const diagnostic& error) {
            if (!sim.trace.empty())
                sim.write(destination / "failed_partial");
            throw std::runtime_error(std::string(error.what()) + "; last accepted time "
                + (sim.trace.empty() ? "before initialization" : std::to_string(sim.trace.back().time / 1e-9) + " ns")
                + "; controller records " + std::to_string(records.size()));
        }
        sim.write(destination / "cpp");
        std::ofstream file(destination / "controls.csv");
        control_header(file, Count);
        for (const auto& record : records)
            write_control(file, record, Count);
        return sim.status() == "sampled_pass" ? 0 : 1;
    }
    verilog_a::write_experiment(sim, destination, {duration(clocks * array_period), duration(array_period / 1024),
        duration(array_period / 2048), 1e-16, 2, 1e-18});
    std::ofstream metadata(destination / "live.json");
    metadata << "{\"slices\":" << Count << ",\"bursts\":" << bursts
             << ",\"launches\":" << launches << ",\"clocks\":" << clocks << ",\"period_s\":" << std::setprecision(17)
             << array_period << ",\"drivers\":[";
    bool first = true;
    for (auto [slice, bit, id] : bindings) {
        metadata << (first ? "" : ",") << "{\"slice\":" << slice << ",\"bit\":" << bit << ",\"id\":" << id << '}';
        first = false;
    }
    metadata << "],\"decisions\":[";
    for (unsigned i = 0; i < Count; ++i)
        metadata << (i ? "," : "") << '"' << 'n' << sim.root(feedback_nodes[i].id()) << '"';
#ifdef CPPHDL_ADC_CORE
    metadata << "],\"feedback_low_V\":0.36,\"feedback_high_V\":0.84,\"core_interface\":true}\n";
#else
    metadata << "]}\n";
#endif
    return 0;
}

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "server")
            return server(unsigned(std::stoul(argv[2])));
        if (argc < 4 || argc > 6)
            throw std::runtime_error("expected destination run|export slices [supply resistance] [bursts]");
        const unsigned count = unsigned(std::stoul(argv[3]));
        const bool native = std::string_view(argv[2]) == "run";
        if (!native && std::string_view(argv[2]) != "export")
            throw std::runtime_error("use run or export");
        const double resistance = argc >= 5 ? std::stod(argv[4]) : .25;
        const unsigned bursts = argc == 6 ? unsigned(std::stoul(argv[5])) : 1;
        if (count == 1)
            return experiment<1>(argv[1], native, resistance, bursts);
        if (count == 4)
            return experiment<4>(argv[1], native, resistance, bursts);
        if (count == 16)
            return experiment<16>(argv[1], native, resistance, bursts);
        throw std::runtime_error("use 1, 4 or 16 physical slices");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
