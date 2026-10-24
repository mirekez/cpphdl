#include <cpphdl_analog.h>
#include <VerilogA/export.hpp>
#include <iostream>

long _system_clock = 0;
using namespace analog;

class EdgeCounter : public cpphdl::Module {
    cpphdl::reg<cpphdl::logic<8>> count;
public:
    _PORT(cpphdl::logic<8>) Q;
    void _assign() { Q = _ASSIGN_REG(count); }
    void _work(bool reset) { count._next = reset ? 0 : static_cast<unsigned>(count) + 1; }
    void _strobe() { count.strobe(); }
};

void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void trajectory(double step) {
    simulation sim;
    electrical vss, stimulus, input;
    sim.reference(vss);
    std::vector<std::pair<double, double>> wave{
        {0, 0}, {1, 0}, {2, 1.2}, {3, 1.2}, {4, .6}, {5, .6},
        {6, .79}, {7, .41}, {8, .6}, {9, .6}, {10, 0}, {11, 0},
        {12, .6}, {13, .6}, {14, .79}, {15, .6}, {16, 1.2},
        {17, .6}, {18, .41}, {19, .79}, {20, .6}, {21, 0}, {22, 0}};
    for (auto& point : wave)
        point.first *= 1e-9;
    stimulus = sim_pwl_voltage_source(vss, wave);
    input = resistor(stimulus, resistance(50));
    input = capacitor(vss, capacitance_value(100e-15));
    bits<1> clock, raw, inverted;
    bits<8> rising, falling;
    clock = sim_schmitt_sense(input, vss, voltage(.4), voltage(.8));
    raw = sim_sense(input, vss, voltage(.4), voltage(.8));
    inverted = sim_invert(clock);
    auto up = std::make_shared<EdgeCounter>();
    auto down = std::make_shared<EdgeCounter>();
    sim_clock_cpphdl(up, clock);
    sim_clock_cpphdl(down, inverted);
    rising = sim_registered_output<8>(up, &EdgeCounter::Q);
    falling = sim_registered_output<8>(down, &EdgeCounter::Q);
    sim.observe("clock", clock);
    sim.observe("raw", raw);
    sim.observe("rising", rising);
    sim.observe("falling", falling);
    sim.run(duration(22e-9), duration(step));
    const auto at = [&](double ns) -> const sample& {
        return *std::min_element(sim.trace.begin(), sim.trace.end(), [&](const auto& a, const auto& b) {
            return std::abs(a.time - ns * 1e-9) < std::abs(b.time - ns * 1e-9);
        });
    };
    for (double ns : {3., 8., 16.5, 19.})
        check(at(ns).exact_bits[0] == bit_value(1), "high state lost inside hysteresis band");
    for (double ns : {0., 10.5, 14., 21.5})
        check(at(ns).exact_bits[0] == bit_value(0), "low state lost inside hysteresis band");
    check(!at(8).exact_bits[1] && !at(14).exact_bits[1], "ordinary sense must preserve ambiguity");
    check(at(8).exact_bits[2] == bit_value(1) && at(14).exact_bits[2] == bit_value(1),
          "in-band excursions generated extra native clocks");
    check(rising.value() == bit_value(2) && falling.value() == bit_value(2),
          "electrical clock did not deliver exactly two edges in each direction");
    for (const auto& row : sim.trace)
        check(bool(row.exact_bits[0]), "known initial clock became unknown");
}

void rejected_step() {
    simulation sim;
    sim.analytic_jacobian = true;
    sim.minimum_step = duration(.125e-9);
    electrical vss, input;
    sim.reference(vss);
    input = sim_pwl_voltage_source(vss, {{0, 0}, {1e-9, 1.2}, {2e-9, 1.2}});
    bits<1> clock;
    bits<8> count;
    clock = sim_schmitt_sense(input, vss, voltage(.4), voltage(.8));
    auto counter = std::make_shared<EdgeCounter>();
    sim_clock_cpphdl(counter, clock);
    count = sim_registered_output<8>(counter, &EdgeCounter::Q);
    sim.observe("clock", clock);
    sim.observe("count", count);
    bool rejected = false;
    sim.linear_backend = [&](std::size_t size, const std::vector<matrix_entry>& entries,
                             const std::vector<double>& right) {
        // Fail after the first full-step trial has changed the sensed bit but
        // before that physical step can be accepted or clock a native module.
        if (!sim.trace.empty() && clock.value() == bit_value(1) && !rejected) {
            rejected = true;
            throw diagnostic("E_LINEAR_ACCURACY", "injected rejection after hysteretic trial update");
        }
        std::vector<std::vector<double>> matrix(size, std::vector<double>(size));
        for (const auto& entry : entries)
            matrix[entry.row][entry.column] += entry.value;
        return linear_solve(std::move(matrix), right);
    };
    sim.run(duration(2e-9), duration(1e-9));
    check(rejected, "rollback injection missed the changed trial sense state");
    check(std::abs(sim.trace[1].time - .5e-9) < 1e-24, "rejected step was not subdivided");
    check(sim.trace[1].exact_bits[0] == bit_value(0) && sim.trace[1].exact_bits[1] == bit_value(0),
          "rejected trial leaked hysteresis or an early native clock into the retry");
    check(count.value() == bit_value(1), "retry duplicated the native clock edge");
}

int main() {
    try {
        trajectory(50e-12);
        trajectory(25e-12);
        rejected_step();
        {
            simulation sim;
            electrical vss, input;
            sim.reference(vss);
            input = sim_voltage_source(vss, voltage(.6));
            bits<1> clock;
            clock = sim_schmitt_sense(input, vss, voltage(.4), voltage(.8));
            sim.run(duration(1e-9), duration(1e-10));
            check(!clock.value(), "unknown startup must not invent a clock level");
            bool rejected = false;
            try { (void)verilog_a::render_module(sim, "Clock", {{"IN", input}, {"Vss", vss}}); }
            catch (const diagnostic& error) { rejected = error.code == "E_VERILOGA"; }
            check(rejected, "Verilog-A export silently discarded hysteretic adapter state");
        }
        {
            simulation sim;
            electrical vss, input;
            sim.reference(vss);
            input = sim_voltage_source(vss, voltage(.6));
            bits<1> clock;
            clock = sim_schmitt_sense(input, vss, voltage(.4), voltage(.8));
            sim_clock_cpphdl(std::make_shared<EdgeCounter>(), clock);
            bool rejected = false;
            try { sim.run(duration(1e-9), duration(1e-10)); }
            catch (const diagnostic& error) { rejected = error.code == "E_UNKNOWN_CPPHDL"; }
            check(rejected, "ambiguous electrical startup reached a two-state module");
        }
        std::cout << "PASS electrical clock hysteresis, refinement, rollback and ambiguous startup\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
