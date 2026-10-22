#include <cpphdl_analog.h>
#include <VerilogA/export.hpp>
#include <iostream>

long _system_clock = 0;

using namespace cpphdl::analog;
using namespace analog::literals;

// Native C++HDL registers: the second stage samples the first stage's old value.
class SamplePipeline : public cpphdl::Module {
    cpphdl::reg<cpphdl::logic<1>> first, second;

public:
    _PORT(cpphdl::logic<1>) D;
    _PORT(cpphdl::logic<1>) FIRST;
    _PORT(cpphdl::logic<1>) SECOND;

    void _assign() {
        FIRST = _ASSIGN_REG(first);
        SECOND = _ASSIGN_REG(second);
    }
    void _work(bool reset) {
        first._next = reset ? cpphdl::logic<1>(0) : D();
        second._next = reset ? cpphdl::logic<1>(0) : static_cast<const cpphdl::logic<1>&>(first);
    }
    void _strobe() {
        first.strobe();
        second.strobe();
    }
};

class MixedReceiver : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) Y;
    _PORT(bits<1>) CLK;
    bits<1> DATA, FIRST, SECOND, RETURN;
    std::shared_ptr<SamplePipeline> digital = std::make_shared<SamplePipeline>();

    void _assign() override {
        DATA = sim_sense(IN, Vss, .35_V, .85_V); // Ideal interpretation; no physical receiver devices.
        digital->D = sim_to_cpphdl(DATA);
        sim_clock_cpphdl(digital, CLK);
        FIRST = sim_registered_output<1>(digital, &SamplePipeline::FIRST);
        SECOND = sim_registered_output<1>(digital, &SamplePipeline::SECOND);
        Y = sim_driver(SECOND, Vdd, Vss, 100_Ohm); // Native state selects a behavioral finite-resistance path.
        Y = capacitor(Vss, 1_nF);
        RETURN = sim_sense(Y, Vss, .35_V, .85_V); // Observe the achieved voltage, including load and delay.
    }
};

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        simulation sim;
        electrical VIN, Vdd, Vss;
        sim.reference(Vss);
        Vdd = sim_voltage_source(Vss, 1.2_V);
        VIN = sim_voltage_source(Vss, 0_V, 750_ns, 1.2_V);
        capacitance IN(100_pF, Vss);
        IN = resistor(VIN, 1_kOhm);
        bits<1> CLK;
        sim.clock(CLK, 1_us);

        MixedReceiver receiver;
        receiver.IN = IN;
        receiver.Vdd = Vdd;
        receiver.Vss = Vss;
        receiver.CLK = CLK;
        receiver.name("receiver");
        receiver.elaborate();
        Vss = resistor(receiver.Y, 10_kOhm); // The load reduces the output below Vdd.

        sim.observe("IN", IN, Vss);
        sim.observe("DATA", receiver.DATA);
        sim.observe("FIRST", receiver.FIRST);
        sim.observe("SECOND", receiver.SECOND);
        sim.observe("Y", receiver.Y, Vss);
        sim.observe("RETURN", receiver.RETURN);
        sim.require_final("native pipeline captured input", receiver.SECOND, 1);
        sim.require_final("loaded output interpreted high", receiver.RETURN, 1);
        sim.require_final_voltage("loaded high", receiver.Y, Vss, 1.18_V, 1.20_V);
        if (argc > 2 && std::string_view(argv[argc - 1]) == "--verilog-a") {
            std::filesystem::path out(argv[1]);
            std::filesystem::create_directories(out);
            std::ofstream file(out / "MixedReceiverAnalog.va");
            file << verilog_a::render_analog_partition(
                sim, "MixedReceiverAnalog", {{"Vdd", Vdd}, {"Vss", Vss}, {"VIN", VIN}, {"IN", IN}, {"Y", receiver.Y}});
            if (!file)
                throw std::runtime_error("cannot write mixed-signal analog partition");
            return 0;
        }
        sim.run(4_us, 25_ns);

        auto sample_at = [&](duration time) -> const sample& {
            return *std::min_element(sim.trace.begin(), sim.trace.end(), [&](const auto& a, const auto& b) {
                return std::abs(a.time - time.value) < std::abs(b.time - time.value);
            });
        };
        const auto& first = sample_at(1500_ns);
        require(first.exact_bits[2] == bit_value(1) && first.exact_bits[3] == bit_value(0),
                "native register pipeline lost its old-state latency");
        const auto& second = sample_at(2500_ns);
        require(second.exact_bits[3] == bit_value(1), "second native register did not capture");
        require(std::abs(*second.values[4]) < 1e-8, "digital commit changed capacitor voltage instantly");
        require(std::abs(sim.voltage(receiver.Y, Vss).value - 1.2 * 10000 / 10100) < 2e-6,
                "native output bypassed the loaded analog network");
        require(sim.status() == "sampled_pass", "mixed requirements failed");
        sim.write(argc > 1 ? argv[1] : "examples/mixed_signal/results");
        std::cout << "PASS native C++HDL pipeline, analog loading, charge continuity and return sensing\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
