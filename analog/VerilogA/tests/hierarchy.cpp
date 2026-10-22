#include <analog.hpp>
#include <VerilogA/experiment.hpp>
#include <filesystem>
#include <iostream>

using namespace analog;

class ResistorCell : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) OUT;
    resistance value{1000};
    void _assign() override {
        OUT = resistor(IN, value);
    }
};

// Loading the second node changes the first node through both child ICs.
class CoupledRC : public IC {
public:
    _PORT(electrical) IN;
    _PORT(electrical) Y;
    _PORT(electrical) Z;
    std::array<ResistorCell, 3> resistors;
    void _assign() override {
        resistors[0].IN = IN;
        resistors[0].OUT = Y;
        resistors[1].IN = Y;
        resistors[1].OUT = Z;
        resistors[1].value = resistance(2000);
        resistors[2].IN = Z;
        resistors[2].OUT = Vss;
        resistors[2].value = resistance(3000);
        for (auto& resistor : resistors) {
            resistor.Vdd = Vdd;
            resistor.Vss = Vss;
            resistor.elaborate();
        }
        Z = capacitor(Vss, capacitance_value(1e-9));
    }
};

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected experiment destination");
        simulation sim;
        electrical ground, source;
        sim.reference(ground);
        source = sim_voltage_source(ground, voltage(1));
        CoupledRC circuit;
        circuit.IN = source;
        circuit.Vdd = source;
        circuit.Vss = ground;
        circuit.elaborate();
        sim.observe("Y", circuit.Y, ground);
        sim.observe("Z", circuit.Z, ground);
        const std::filesystem::path destination = argv[1];
        std::filesystem::create_directories(destination);
        verilog_a::write_experiment(sim, destination, {duration(10e-6), duration(10e-9), duration(5e-9)});
        sim.run(duration(10e-6), duration(10e-9));
        sim.write(destination / "cpp");
        return sim.status() == "sampled_pass" ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
