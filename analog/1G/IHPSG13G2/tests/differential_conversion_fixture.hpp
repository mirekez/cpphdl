#pragma once
#include "../differential_receive_frontend.hpp"
#include "../receive_reference.hpp"
#define CPPHDL_ADC_COMPACT

// Reuse the independently checked controller, host protocol and CSV helpers.
#define CPPHDL_ADC_CORE
#define main standalone_programmable_adc_reference_main
#include "../../tests/adc_array.cpp"
#undef main

template <std::size_t Count>
static int differential_conversion(const std::filesystem::path& destination, bool native,
                                   unsigned gain, double storage, unsigned bursts) {
    constexpr double warmup = 16e-6, step = 2e-12;
    const unsigned launches = Count * bursts, clocks = launches + 24;
    const double stop = warmup + clocks * array_period;
    simulation sim;
    sim.analytic_jacobian = true;
    sim.minimum_step = duration(.1e-12);
    use_sparse_solver(sim, sparse_strategy::certified_lu);
    electrical ground, supply, vdd, logic_source, vlogic, positive, negative, evaluation;
    sim.reference(ground);
    supply = sim_pwl_voltage_source(ground, {{0, 0}, {.1e-6, 0}, {1e-6, 3.3}});
    vdd = resistor(supply, resistance(.25));
    vdd = capacitor(ground, capacitance_value(20e-12));
    logic_source = sim_pwl_voltage_source(ground, {{0, 0}, {.1e-6, 0}, {1e-6, 1.2}});
    vlogic = resistor(logic_source, resistance(10));
    vlogic = capacitor(ground, capacitance_value(20e-12));
    using wave = std::vector<std::pair<double, double>>;
    wave plus{{0, 0}, {1e-6, 0}, {1.1e-6, 2.3}}, minus = plus, evaluate{{0, 0}};
    auto change = [](wave& points, double time, double value) {
        if (points.back().second != value) {
            points.emplace_back(time, points.back().second);
            points.emplace_back(time + .1e-9, value);
        }
    };
    for (unsigned clock = 0; clock < clocks; ++clock) {
        const double begin = warmup + clock * array_period;
        const double differential = (gain == 4 ? 1 : .75) * (int((clock * 3) % 5) - 2);
        change(plus, begin + 1e-9, 2.3 + differential / 2);
        change(minus, begin + 1e-9, 2.3 - differential / 2);
        if (clock) {
            change(evaluate, begin, 3.3);
            change(evaluate, begin + .45 * array_period, 0);
        }
    }
    positive = sim_pwl_voltage_source(ground, plus);
    negative = sim_pwl_voltage_source(ground, minus);
    evaluation = sim_pwl_voltage_source(ground, evaluate);
    analog::one_g::ihp_sg13g2::differential_receive_frontend<Count> receiver;
    analog::one_g::ihp_sg13g2::adc_range_reference bias;
    analog::one_g::mdi_input_network input;
    input.P = resistor(positive, resistance(25));
    input.N = resistor(negative, resistance(25));
    input.Vdd = vdd;
    input.Vss = ground;
    input.elaborate();
    bias.Vdd = vdd;
    bias.Vss = ground;
    bias.elaborate();
    receiver.VREF = bias.OUT;
    receiver.sample_storage = capacitance_value(storage);
    receiver.PLUS = input.PLUS;
    receiver.MINUS = input.MINUS;
    receiver.Vdd = vdd;
    receiver.Vss = ground;
    receiver.Vlogic = vlogic;
    receiver.EVALUATE = resistor(evaluation, resistance(50));
    receiver.EVALUATE = capacitor(ground, capacitance_value(Count * .1e-12));
    receiver.CLEAR = sim_pwl_voltage_source(ground, {{0, 0}, {.1e-6, 0}, {1e-6, 3.3}, {warmup + 2e-9, 3.3}, {warmup + 2.1e-9, 0}});
    for (unsigned bit = 0; bit < 5; ++bit) {
        electrical control;
        control = sim_pwl_voltage_source(ground, {{0, 0}, {.1e-6, 0}, {1e-6, bit == gain - 4 ? 0.0 : 3.3}});
        receiver.SELECT_N[bit] = resistor(control, resistance(50));
    }
    bits<1> reference, launch;
    sim.clock(reference, duration(array_period));
    sim.step(launch, duration(warmup + array_period / 2 + launches * array_period - 1e-9), 1, 0);
    auto controller = std::make_shared<Controller>();
    std::array<std::array<bits<1>, 9>, Count> controls;
    std::vector<std::tuple<unsigned, unsigned, std::size_t>> bindings;
    bool warming = true, resetting = true;
    for (unsigned slice = 0; slice < Count; ++slice) {
        for (unsigned bit = 0; bit < 9; ++bit) {
            auto& signal = controls[slice][bit];
            if (native)
                signal = registered_bit(controller, [slice, bit](auto& node) {
                    return bit == 8 ? bool(node.acquire_out[slice]())
                                    : ((unsigned(node.trial_out[slice]()) >> bit) & 1) == 0;
                });
            else
                sim.drive(signal, 1);
            if (bit == 8)
                receiver.TRACK[slice] = sim_driver(signal, vdd, ground, resistance(50));
            else
                receiver.BIT_N[slice][bit] = sim_driver(signal, vdd, ground, resistance(50));
            bindings.emplace_back(slice, bit, signal.id());
        }
        receiver.CORE_DECISION[slice] = capacitor(ground, capacitance_value(20e-15));
    }
    receiver.elaborate();
    std::vector<control_record> records;
    std::array<bits<1>, Count> decisions;
    if (native) {
        for (unsigned slice = 0; slice < Count; ++slice)
            decisions[slice] = sim_sense(receiver.CORE_DECISION[slice], ground, voltage(.36), voltage(.84));
        controller->precharge_in = _ASSIGN(resetting);
        controller->enable_in = _ASSIGN(true);
        controller->sample_enable_in = _ASSIGN(!resetting && bool(sim_cpphdl_value(launch)));
        controller->offset_in = _ASSIGN(112);
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
            [controller, &resetting, &warming](bool reset) {
                resetting = reset || warming;
                ++_system_clock;
                controller->_work(resetting);
            },
            [controller, &records, &resetting, &warming] {
                controller->_strobe();
                if (!warming || records.empty())
                    records.push_back(capture(*controller, resetting ? -1 : int(records.size()) - 1));
            });
    }
    sim.observe("PLUS", receiver.PLUS, ground);
    sim.observe("MINUS", receiver.MINUS, ground);
    sim.observe("GAIN_INPUT", receiver.preamplifier.gain.IN_MINUS, ground);
    sim.observe("GAIN_OUTPUT", receiver.preamplifier.OUT, ground);
    sim.observe("VDD", vdd, ground);
    sim.observe("VLOGIC", vlogic, ground);
    sim.observe("GAIN_BIAS", receiver.preamplifier.gain.BIAS, ground);
    sim.observe("MDI_P", input.P, ground);
    sim.observe("MDI_N", input.N, ground);
    sim.observe("VREF", bias.OUT, ground);
    for (unsigned slice = 0; slice < Count; ++slice) {
        sim.observe("HELD" + std::to_string(slice), receiver.adc.slices[slice].sampler.OUT, ground);
        sim.observe("TRACK" + std::to_string(slice), receiver.TRACK[slice], ground);
    }
    for (unsigned slice = 0; slice < Count; ++slice)
        sim.observe("CORE_DECISION" + std::to_string(slice), receiver.CORE_DECISION[slice], ground);
    std::filesystem::create_directories(destination);
    if (native) {
        sim.run(duration(warmup), duration(1e-9));
        sim.discard_trace();
        warming = false;
        const double middle = std::min(stop, warmup + 128e-9);
        sim.advance_to(duration(middle), duration(step));
        if (stop > middle)
            sim.advance_to(duration(stop), duration(step));
        sim.write(destination / "cpp");
        std::ofstream output(destination / "controls.csv");
        control_header(output, Count);
        for (const auto& record : records)
            write_control(output, record, Count);
        return sim.status() == "sampled_pass" ? 0 : 1;
    }
    verilog_a::write_experiment(sim, destination,
        {duration(stop), duration(step), duration(20e-12), 1e-16, 2, 1e-18, duration(2e-15)});
    std::ofstream metadata(destination / "live.json");
    metadata << "{\"controller_bank_slices\":13,\"normalizer_offset\":112,\"cold_start\":true,\"precharge\":true,\"slices\":" << Count << ",\"bursts\":" << bursts << ",\"launches\":" << launches
             << ",\"clocks\":" << clocks << ",\"period_s\":" << std::setprecision(17) << array_period
             << ",\"clock_origin_s\":" << warmup << ",\"gain_code\":" << gain
             << ",\"sample_storage_F\":" << receiver.sample_storage.value
             << ",\"sampler_dummy_n_beta_A_per_V2\":" << receiver.adc.slices[0].sampler.dummy_n.beta.value
             << ",\"sampler_dummy_p_beta_A_per_V2\":" << receiver.adc.slices[0].sampler.dummy_p.beta.value
             << ",\"charge_defined_nodes\":"
             << (!receiver.adc.slices[0].sampler.leakage_return && !receiver.adc.slices[0].decision.weak_returns ? "true" : "false")
             << ",\"gain_compensation_F\":" << receiver.preamplifier.gain.compensation_step.value << ",\"drivers\":[";
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

template <template <std::size_t> class Receiver>
int run_programmable_conversion(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "server")
            return server(unsigned(std::stoul(argv[2])));
        if (argc != 7)
            throw std::runtime_error("expected destination run|export slices gain_code storage_pF bursts");
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        const unsigned count = unsigned(std::stoul(argv[3])), gain = unsigned(std::stoul(argv[4]));
        const double storage = std::stod(argv[5]) * 1e-12;
        const unsigned bursts = unsigned(std::stoul(argv[6]));
        if ((mode != "run" && mode != "export") || gain < 4 || gain > 8 || !std::isfinite(storage)
                || storage < 1e-12 || storage > 3e-12 || bursts == 0 || bursts > 2 || (bursts == 2 && count != 16))
            throw std::runtime_error("invalid programmable conversion experiment");
        if (count == 1)
            return programmable_conversion<1, Receiver>(destination, mode == "run", gain, storage, bursts);
        if (count == 4)
            return programmable_conversion<4, Receiver>(destination, mode == "run", gain, storage, bursts);
        if (count == 16)
            return programmable_conversion<16, Receiver>(destination, mode == "run", gain, storage, bursts);
        throw std::runtime_error("use 1, 4 or 16 slices");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
