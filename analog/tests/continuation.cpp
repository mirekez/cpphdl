#include <cpphdl_analog.h>
#include <iostream>

long _system_clock = -1;
using namespace analog;

class Counter : public cpphdl::Module {
    cpphdl::reg<cpphdl::logic<32>> count{};
public:
    unsigned resets = 0;
    _PORT(cpphdl::logic<32>) Q = _ASSIGN_REG(count);
    void _assign() {}
    void _work(bool reset) {
        resets += reset;
        count._next = reset ? 0 : static_cast<std::uint64_t>(count) + 1;
    }
    void _strobe() { count.strobe(); }
};

void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Action> void rejects(const char* code, Action action) {
    try { action(); }
    catch (const diagnostic& error) {
        check(error.code == code, "wrong transient-state diagnostic");
        return;
    }
    throw std::runtime_error(std::string("missing diagnostic ") + code);
}

std::vector<sample> scenario(bool segmented) {
    simulation sim;
    sim.analytic_jacobian = true;
    electrical ground, source, output;
    sim.reference(ground);
    source = sim_voltage_source(ground, voltage(1));
    output = resistor(source, resistance(1000));
    output = capacitor(ground, capacitance_value(100e-12));
    bits<1> clock, physical_clock;
    bits<8> data, stored;
    bits<32> count, physical_count;
    sim.clock(clock, duration(100e-9));
    sim.step(data, duration(250e-9), 4, 9);
    physical_clock = sim_schmitt_sense(output, ground, voltage(.35), voltage(.85));
    auto native = std::make_shared<Counter>();
    auto physical = std::make_shared<Counter>();
    sim_clock_cpphdl(native, clock);
    sim_clock_cpphdl(physical, physical_clock);
    count = sim_registered_output<32>(native, &Counter::Q);
    physical_count = sim_registered_output<32>(physical, &Counter::Q);
    stored = sim_store(data, clock);
    sim.observe("output", output, ground);
    sim.observe("count", count);
    sim.observe("physical_count", physical_count);
    sim.observe("stored", stored);
    sim.assume_voltage("supply-range response", output, ground, voltage(0), voltage(1));
    sim.require_final("native count", count, 10);
    sim.require_final("one RC threshold crossing", physical_count, 1);
    std::vector<sample> result;
    auto collect = [&] {
        result.insert(result.end(), sim.trace.begin(), sim.trace.end());
        sim.discard_trace();
    };
    if (segmented) {
        sim.run(duration(250e-9), duration(1e-9));
        collect();
        for (unsigned window = 2; window <= 4; ++window) {
            sim.advance_to(duration(window * 250e-9), duration(1e-9));
            collect();
        }
    } else {
        sim.run(duration(1e-6), duration(1e-9));
        collect();
    }
    check(sim.status() == "sampled_pass", "continuation did not evaluate the final full-history contract");
    check(native->resets == 1 && physical->resets == 1, "continuation reset a native controller");
    check(std::abs(sim.elapsed().value - 1e-6) < 1e-20, "accepted physical time did not survive trace disposal");
    for (const auto& row : result) {
        const double expected = 1 - std::exp(-row.time / 100e-9);
        check(row.values[0] && std::abs(*row.values[0] - expected) < .003,
              "segmented integration lost the independently known RC pole");
        if (std::abs(row.time - 250e-9) < 1e-20)
            check(row.exact_bits[1] == bit_value(3) && row.exact_bits[3] == bit_value(9),
                  "a source/clock edge at the window boundary was missed or repeated");
    }
    return result;
}

void long_digital_run() {
    simulation sim;
    bits<1> clock;
    bits<32> count;
    sim.clock(clock, duration(8e-9));
    auto native = std::make_shared<Counter>();
    sim_clock_cpphdl(native, clock);
    count = sim_registered_output<32>(native, &Counter::Q);
    sim.require_final("four milliseconds of reference edges", count, 500000);
    std::size_t samples = 0;
    for (unsigned window = 1; window <= 20; ++window) {
        if (window == 1)
            sim.run(duration(200e-6), duration(4e-9));
        else
            sim.advance_to(duration(window * 200e-6), duration(4e-9));
        check(sim.trace.size() <= 50002, "a streamed window retained earlier trace data");
        samples += sim.trace.size();
        sim.discard_trace();
    }
    check(samples >= 999980 && samples <= 1000020, "long transient lost or duplicated its sampling grid");
    check(native->resets == 1 && count.value() == bit_value(500000),
          "long absolute-time clock rounding or reset history corrupted native state");
    check(sim.status() == "sampled_pass", "long digital contract failed");
}

void historical_assumption() {
    simulation sim;
    electrical ground, source, output;
    sim.reference(ground);
    source = sim_pwl_voltage_source(ground, {{0, 1}, {50e-9, 1}, {50.1e-9, 0}, {100e-9, 0}});
    output = resistor(source, resistance(1000));
    output = capacitor(ground, capacitance_value(10e-12));
    sim.assume_voltage("historical bound", output, ground, voltage(0), voltage(.5));
    sim.require_final_voltage("settled output", output, ground, voltage(0), voltage(.1));
    sim.run(duration(25e-9), duration(.1e-9));
    sim.discard_trace();
    sim.advance_to(duration(100e-9), duration(.1e-9));
    check(sim.voltage(output, ground).value < .1, "RC pulse did not decay after its early violation");
    check(sim.status() == "out_of_contract", "discarding trace erased an earlier violated assumption");
}

void lifecycle() {
    simulation sim;
    electrical ground, source, output;
    sim.reference(ground);
    source = sim_voltage_source(ground, voltage(1));
    output = resistor(source, resistance(1000));
    output = capacitor(ground, capacitance_value(1e-12));
    rejects("E_TRANSIENT_STATE", [&] { sim.advance_to(duration(10e-9), duration(1e-9)); });
    sim.require_trace("retained trajectory", [](const auto& trace) { return std::optional<bool>(!trace.empty()); });
    sim.run(duration(10e-9), duration(1e-9));
    rejects("E_TRACE_HISTORY", [&] { sim.discard_trace(); });
    rejects("E_VALUE", [&] { sim.advance_to(duration(9e-9), duration(1e-9)); });
    sim.advance_to(duration(20e-9), duration(1e-9));
    check(sim.trace.size() == 21 && sim.status() == "sampled_pass", "invalid timing request poisoned a healthy state");
    rejects("E_ELABORATION", [&] { sim.run(duration(30e-9), duration(1e-9)); });
    sim.analytic_jacobian = true;
    sim.linear_backend = [](auto, const auto&, const auto&) -> std::vector<double> {
        throw diagnostic("E_LINEAR_ACCURACY", "injected fatal continuation solve failure");
    };
    rejects("E_LINEAR_ACCURACY", [&] { sim.advance_to(duration(30e-9), duration(1e-9)); });
    check(sim.status() == "unverified", "a failed continuation retained a previous successful status");
    rejects("E_TRANSIENT_STATE", [&] { sim.advance_to(duration(30e-9), duration(.5e-9)); });
}

int main() {
    try {
        auto whole = scenario(false), parts = scenario(true);
        check(whole.size() == parts.size(), "window boundaries duplicated or removed observations");
        for (std::size_t i = 0; i < whole.size(); ++i) {
            check(std::abs(whole[i].time - parts[i].time) < 1e-20, "segmented time grid differs");
            check(whole[i].exact_bits == parts[i].exact_bits, "segmentation changed a digital transition");
            check(std::abs(*whole[i].values[0] - *parts[i].values[0]) < 1e-12, "segmentation changed capacitor history");
        }
        long_digital_run();
        historical_assumption();
        lifecycle();
        std::cout << "PASS continuation: RC charge, native/store/clock history, million-point streaming, "
                     "historical assumptions, retained trace contracts and failed-session guards\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
