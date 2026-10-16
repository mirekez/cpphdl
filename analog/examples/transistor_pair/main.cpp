#include "../common.hpp"

class NamedDevices : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) Y;

    void _assign() override {
        pmos pull_up(A, Vdd); // A device variable, declared during the assignment phase.
        Y = pull_up;
        nmos pull_down(A, Y); // A second device corrects the same output.
        Vss = pull_down;
        // These handles may leave scope; the simulation owns the declared physical models.
    }
};

int main(int argc, char** argv) {
    return example_main(argc, argv, "examples/transistor_pair/results", 4_us, 10_ns, [](simulation& sim) {
        electrical A, Vdd, Vss;
        sim.reference(Vss);
        Vdd = sim_voltage_source(Vss, 1.2_V);
        A = sim_voltage_source(Vss, 0_V, 2_us, 1.2_V);

        NamedDevices gate;
        gate.A = A;
        gate.Vdd = Vdd;
        gate.Vss = Vss;
        gate.name("named_devices");
        gate.elaborate();

        gate.Y = capacitor(Vss, 10_pF);
        sim.observe("A", A, Vss);
        sim.observe("Y", gate.Y, Vss);
        sim.require_final_voltage("low output", gate.Y, Vss, 0_V, 1_mV);
    });
}
