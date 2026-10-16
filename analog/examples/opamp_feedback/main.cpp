#include "../common.hpp"

int main(int argc, char** argv) {
    return example_main(argc, argv, "examples/opamp_feedback/results", 40_us, 100_ns, [](simulation& sim) {
        electrical IN, Vdd, Vss;
        sim.reference(Vss);
        Vdd = sim_voltage_source(Vss, 3.3_V);
        IN = sim_voltage_source(Vss, .5_V);

        non_inverting_amplifier amp;
        amp.IN = IN;
        amp.Vdd = Vdd;
        amp.Vss = Vss;
        amp.name("amplifier");
        amp.elaborate();

        Vss = resistor(amp.OUT, 100_kOhm); // External loading changes achieved gain.
        sim.observe("OUT", amp.OUT, Vss);
        sim.observe("FB", amp.feedback.TAP, Vss);
        sim.observe("TAIL", amp.amplifier.TAIL, Vss);
        sim.observe("MIRROR", amp.amplifier.MIRROR, Vss);
        sim.assume_voltage("supply envelope", Vdd, Vss, 3_V, 3.6_V);
        sim.require_final_voltage("gain-two goal", amp.OUT, Vss, .99_V, 1.01_V);
    });
}
