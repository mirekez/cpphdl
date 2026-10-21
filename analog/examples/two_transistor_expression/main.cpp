#include "../common.hpp"

class WireOutput : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) Y; // A bidirectional output wire exposed as a member port.

    void _assign() override {
        auto pmos_vdd = pmos::from(Vdd);
        Y = pmos_vdd(A); // First purpose enables output.
        Vss = nmos(A, Y); // Second disables it; both influence the same wire.
    }
};

class ActiveOutput : public IC {
public:
    _PORT(electrical) A;
    _PORT(capacitance) Y { 10_pF, Vss }; // Active port: the type supplies charge storage.

    void _assign() override {
        auto pmos_vdd = pmos::from(Vdd);
        Y = pmos_vdd(A); // Same purposes, with storage intrinsic to the port type.
        Vss = nmos(A, Y);
    }
};

int main(int argc, char** argv) {
    return example_main(argc, argv, "examples/two_transistor_expression/results", 4_us, 10_ns, [](simulation& sim) {
        electrical A, Vdd, Vss;
        sim.reference(Vss);
        Vdd = sim_voltage_source(Vss, 1.2_V);
        A = sim_voltage_source(Vss, 0_V, 2_us, 1.2_V);

        WireOutput wire;
        wire.A = A;
        wire.Vdd = Vdd;
        wire.Vss = Vss;
        wire.name("wire_output");
        wire.elaborate();
        wire.Y = capacitor(Vss, 10_pF); // Storage added explicitly to the plain wire.

        ActiveOutput active;
        active.A = A;
        active.Vdd = Vdd;
        active.Vss = Vss;
        active.name("active_output");
        active.elaborate(); // Storage already comes from its member port type.

        sim.observe("WIRE", wire.Y, Vss);
        sim.observe("ACTIVE", active.Y, Vss);
        sim.require_final_voltage("wire low", wire.Y, Vss, 0_V, 1_mV);
        sim.require_final_voltage("active low", active.Y, Vss, 0_V, 1_mV);
    });
}
