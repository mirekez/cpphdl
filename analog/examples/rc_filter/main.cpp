#include "../common.hpp"

class LowPass : public IC {
public:
    _PORT(electrical) IN;
    _PORT(capacitance) OUT { 1_nF, Vss }; // Inherited Vss exists before this active member initializes.

    void _assign() override {
        OUT = resistor(IN, 1_kOhm); // Deliver input through R; storage corrects fast changes.
    }
};

int main(int argc, char** argv) {
    return example_main(argc, argv, "examples/rc_filter/results", 5_us, 50_ns, [](simulation& sim) {
        electrical IN, Vss;
        sim.reference(Vss);
        IN = sim_voltage_source(Vss, 1_V);

        LowPass filter;
        filter.IN = IN;
        filter.Vss = Vss;
        filter.name("filter");
        filter.elaborate(); // Calls _assign() once before integration.

        sim.observe("IN", IN, Vss);
        sim.observe("OUT", filter.OUT, Vss);
        sim.require_final_voltage("charged output", filter.OUT, Vss, .99_V, 1.001_V);
    });
}
