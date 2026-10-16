#include "../common.hpp"

class Inverter : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) Y;

    void _assign() override {
        auto pmos_vdd = pmos::from(Vdd); // Source-bound alias, using the inherited supply port.
        Y = pmos_vdd(A); // Enable the output when A is low.
        Vss = nmos(A, Y); // Correct/disable it by pulling the same Y low when A is high.
    }
};

int main(int argc, char** argv) {
    return example_main(argc, argv, "examples/cmos_inverter/results", 4_us, 10_ns, [](simulation& sim) {
        electrical A, Vdd, Vss;
        sim.reference(Vss);
        Vdd = sim_voltage_source(Vss, 1.2_V);
        A = sim_voltage_source(Vss, 0_V, 2_us, 1.2_V);

        Inverter gate;
        gate.A = A;
        gate.Vdd = Vdd;
        gate.Vss = Vss;
        gate.name("gate");
        gate.elaborate();

        gate.Y = capacitor(Vss, 10_pF); // External loading retains both internal device purposes.
        sim.observe("A", A, Vss);
        sim.observe("Y", gate.Y, Vss);
        sim.assume_voltage("supply envelope", Vdd, Vss, 1.1_V, 1.3_V);
        sim.require_final_voltage("output disabled", gate.Y, Vss, 0_V, 1_mV);
    });
}
