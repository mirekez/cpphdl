#include "../common.hpp"
#include <basic/regulator.hpp>

int main(int argc, char** argv) {
    return example_main(argc, argv, "examples/regulator/results", 500_us, 500_ns, [](simulation& sim) {
        electrical VIN, REF, Vss;
        sim.reference(Vss);
        VIN = sim_voltage_source(Vss, 3.3_V, 250_us, 2.5_V);
        REF = sim_voltage_source(Vss, 1.2_V); // Reference stimulus, separate from the achieved output.
        capacitance OUT(100_nF, Vss);

        regulator supply;
        supply.Vdd = VIN;
        supply.Vss = Vss;
        supply.REF = REF;
        supply.OUT = OUT;
        supply.name("supply");
        supply.elaborate();

        Vss = resistor(OUT, 100_Ohm); // External load competes with the pass device.
        sim.observe("VIN", VIN, Vss);
        sim.observe("REF", REF, Vss);
        sim.observe("OUT", OUT, Vss);
        sim.observe("CONTROL", supply.CONTROL, Vss);
        sim.observe("TAIL", supply.error.TAIL, Vss);
        sim.assume_voltage("input envelope", VIN, Vss, 2_V, 5_V);
        sim.require_final_voltage("regulation goal", OUT, Vss, 1.18_V, 1.24_V);
    });
}
