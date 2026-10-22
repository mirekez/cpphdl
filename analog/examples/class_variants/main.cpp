#include "../common.hpp"

int main(int argc, char** argv) {
    return example_main(argc, argv, "examples/class_variants/results", 400_ns, 1_ns, [](simulation& sim) {
        electrical A, Vdd, Vss;
        sim.reference(Vss);
        Vdd = sim_voltage_source(Vss, 1.2_V);
        A = sim_voltage_source(Vss, 0_V, 200_ns, 1.2_V);

        cmos<fast_model> fast; // A type choice changes the model, retaining the same member ports.
        fast.A = A;
        fast.Vdd = Vdd;
        fast.Vss = Vss;
        fast.name("fast");
        fast.elaborate();

        cmos<channel_model> channel;
        channel.A = A;
        channel.Vdd = Vdd;
        channel.Vss = Vss;
        channel.name("channel");
        channel.elaborate();

        fast.Y = capacitor(Vss, 10_pF);
        channel.Y = capacitor(Vss, 10_pF);
        sim.observe("FAST", fast.Y, Vss);
        sim.observe("CHANNEL", channel.Y, Vss);
        sim.require_final_voltage("fast endpoint", fast.Y, Vss, 0_V, 1_mV);
        sim.require_final_voltage("channel endpoint", channel.Y, Vss, 0_V, 1_mV);
        // More detail alone is not a certificate of greater accuracy.
    });
}
