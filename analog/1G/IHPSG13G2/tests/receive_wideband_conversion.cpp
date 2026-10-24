// Reuse the qualified controller protocol and capture helpers. The electrical
// experiment below adds the coupled amplifier/buffer hierarchy to that host.
#define CPPHDL_ADC_CORE
#define main standalone_adc_core_main
#include "../../tests/adc_array.cpp"
#undef main
#include "../wideband_receive_frontend.hpp"
template <std::size_t Count>
using ConversionFrontend = one_g::ihp_sg13g2::wideband_receive_frontend<Count>;

template <std::size_t Count>
static int receive_experiment(const std::filesystem::path& destination, bool native, unsigned bursts) {
    const unsigned launches = Count * bursts, clocks = launches + 24;
    simulation sim;
    sim.analytic_jacobian = true;
    sim.minimum_step = duration(array_period / 65536);
    use_sparse_solver(sim, sparse_strategy::certified_lu);
    electrical source, vdd, vss, logic_source, vlogic, plus_source, minus_source, evaluate_source;
    sim.reference(vss);
    source = sim_voltage_source(vss, voltage(3.3));
    vdd = resistor(source, resistance(.25));
    vdd = capacitor(vss, capacitance_value(20e-12), voltage(3.3));
    logic_source = sim_voltage_source(vss, voltage(1.2));
    vlogic = resistor(logic_source, resistance(10));
    vlogic = capacitor(vss, capacitance_value(20e-12));
    using wave = std::vector<std::pair<double, double>>;
    wave plus{{0, 1.65}}, minus{{0, 1.65}}, evaluate{{0, 0}};
    auto change = [](wave& points, double time, double value) {
        if (points.back().second != value) {
            points.emplace_back(time, points.back().second);
            points.emplace_back(time + .1e-9, value);
        }
    };
    for (unsigned clock = 0; clock < clocks; ++clock) {
        const double differential = .12 * (int((clock * 3) % 5) - 2);
        change(plus, clock * array_period + 1e-9, 1.65 + differential / 2);
        change(minus, clock * array_period + 1e-9, 1.65 - differential / 2);
        if (clock) {
            change(evaluate, clock * array_period, 3.3);
            change(evaluate, clock * array_period + .45 * array_period, 0);
        }
    }
    plus_source = sim_pwl_voltage_source(vss, plus);
    minus_source = sim_pwl_voltage_source(vss, minus);
    evaluate_source = sim_pwl_voltage_source(vss, evaluate);
    ConversionFrontend<Count> receiver;
    receiver.PLUS = resistor(plus_source, resistance(50));
    receiver.MINUS = resistor(minus_source, resistance(50));
    receiver.EVALUATE = resistor(evaluate_source, resistance(50));
    receiver.EVALUATE = capacitor(vss, capacitance_value(Count * .1e-12));
    receiver.CLEAR = sim_pwl_voltage_source(vss, {{0, 3.3}, {2e-9, 3.3}, {2.1e-9, 0}});
    receiver.Vdd = vdd;
    receiver.Vss = vss;
    receiver.Vlogic = vlogic;
    receiver.elaborate();
    bits<1> reference, launch;
    sim.clock(reference, duration(array_period));
    sim.step(launch, duration(array_period / 2 + launches * array_period - 1e-9), 1, 0);
    auto controller = std::make_shared<Controller>();
    std::array<std::array<bits<1>, 9>, Count> controls;
    std::vector<std::tuple<unsigned, unsigned, std::size_t>> bindings;
    for (unsigned slice = 0; slice < Count; ++slice) {
        for (unsigned bit = 0; bit < 9; ++bit) {
            auto& signal = controls[slice][bit];
            if (native)
                signal = registered_bit(controller, [slice, bit](auto& node) {
                    return bit == 8 ? bool(node.acquire_out[slice]())
                                    : ((unsigned(node.trial_out[slice]()) >> bit) & 1) == 0;
                });
            else
                sim.drive(signal, bit == 8 ? 0 : 1);
            if (bit == 8)
                receiver.TRACK[slice] = sim_driver(signal, vdd, vss, resistance(50));
            else
                receiver.BIT_N[slice][bit] = sim_driver(signal, vdd, vss, resistance(50));
            bindings.emplace_back(slice, bit, signal.id());
        }
        receiver.CORE_DECISION[slice] = capacitor(vss, capacitance_value(20e-15));
    }
    std::vector<control_record> records;
    std::array<bits<1>, Count> decisions;
    bool resetting = true;
    if (native) {
        for (unsigned slice = 0; slice < Count; ++slice)
            decisions[slice] = sim_sense(receiver.CORE_DECISION[slice], vss, voltage(.36), voltage(.84));
        controller->enable_in = _ASSIGN(true);
        controller->sample_enable_in = _ASSIGN(!resetting && bool(sim_cpphdl_value(launch)));
        // Preserve the same host protocol as adc_core. Calibration accuracy
        // is checked separately from exact normalization arithmetic here.
        controller->offset_in = _ASSIGN(128);
        controller->gain_in = _ASSIGN(424);
        controller->comparisons_in = _ASSIGN([&] {
            unsigned mask = 0;
            for (unsigned slice = 0; slice < Count; ++slice)
                if (!resetting && controller->convert_out[slice]())
                    mask |= unsigned(sim_cpphdl_value(decisions[slice])) << slice;
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
                    std::cerr << "Receive controller edge " << records.size() - 1 << " of " << clocks << '\n';
            });
    }
    sim.observe("PLUS", receiver.PLUS, vss);
    sim.observe("MINUS", receiver.MINUS, vss);
    sim.observe("DRAIN", receiver.amplifier.OUTP, vss);
    sim.observe("INPUT", receiver.adc.IN, vss);
    sim.observe("VDD", vdd, vss);
    sim.observe("VLOGIC", vlogic, vss);
    for (unsigned slice = 0; slice < Count; ++slice) {
        sim.observe("HELD" + std::to_string(slice), receiver.adc.slices[slice].sampler.OUT, vss);
        sim.observe("CORE_DECISION" + std::to_string(slice), receiver.CORE_DECISION[slice], vss);
    }
    const duration stop(clocks * array_period), step(array_period / 1024);
    std::filesystem::create_directories(destination);
    if (native) {
        sim.run(stop, step);
        sim.write(destination / "cpp");
        std::ofstream file(destination / "controls.csv");
        control_header(file, Count);
        for (const auto& record : records)
            write_control(file, record, Count);
        return sim.status() == "sampled_pass" ? 0 : 1;
    }
    verilog_a::write_experiment(sim, destination, {stop, step, duration(step.value / 2), 1e-16, 2, 1e-18});
    std::ofstream metadata(destination / "live.json");
    metadata << "{\"slices\":" << Count << ",\"bursts\":" << bursts << ",\"launches\":" << launches
             << ",\"clocks\":" << clocks << ",\"period_s\":" << std::setprecision(17) << array_period
             << ",\"drivers\":[";
    bool first = true;
    for (auto [slice, bit, id] : bindings) {
        metadata << (first ? "" : ",") << "{\"slice\":" << slice << ",\"bit\":" << bit << ",\"id\":" << id << '}';
        first = false;
    }
    metadata << "],\"decisions\":[";
    for (unsigned slice = 0; slice < Count; ++slice)
        metadata << (slice ? "," : "") << "\"n" << sim.root(receiver.CORE_DECISION[slice].id()) << '"';
    metadata << "],\"feedback_low_V\":0.36,\"feedback_high_V\":0.84,\"core_interface\":true}\n";
    return 0;
}

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "server")
            return server(unsigned(std::stoul(argv[2])));
        if (argc != 6)
            throw std::runtime_error("expected destination run|export slices supply_resistance bursts");
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        const unsigned count = unsigned(std::stoul(argv[3])), bursts = unsigned(std::stoul(argv[5]));
        if ((mode != "run" && mode != "export") || std::stod(argv[4]) != .25 ||
                bursts == 0 || bursts > 2 || (bursts > 1 && count != 16))
            throw std::runtime_error("invalid receive-conversion experiment");
        if (count == 1)
            return receive_experiment<1>(destination, mode == "run", bursts);
        if (count == 4)
            return receive_experiment<4>(destination, mode == "run", bursts);
        if (count == 16)
            return receive_experiment<16>(destination, mode == "run", bursts);
        throw std::runtime_error("use 1, 4 or 16 slices");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
