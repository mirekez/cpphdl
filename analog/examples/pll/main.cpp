#include "../feedback_checks.hpp"

int main(int argc, char** argv) {
    std::string scenario = argc > 2 ? argv[2] : "nominal";
    if (!feedback_checks::scenario_is(scenario, {"nominal", "loaded", "refined"}))
        return 2;
    return example_main(
        argc, argv, "examples/pll/results", 15_ms, scenario == "refined" ? 500_ns : 1_us, [=](simulation& sim) {
            electrical Vdd, Vss;
            sim.reference(Vss);
            Vdd = sim_voltage_source(Vss, 1.2_V);
            bits<1> reference;
            sim.clock(reference, duration(1.0 / 12000)); // Only the testbench reference is an ideal clock.
            pll loop;
            loop.Vdd = Vdd;
            loop.Vss = Vss;
            loop.REF = sim_driver(reference, Vdd, Vss, 100_Ohm);
            loop.oscillator.stages[2].initial = 1.2_V; // Explicit asymmetric initial charge starts the ring.
            loop.name("pll");
            loop.elaborate();
            if (scenario == "loaded")
                Vss =
                    resistor(loop.OUT, 500_kOhm); // An external load changes oscillator currents and propagation time.

            sim.observe("REF", loop.REF, Vss); // Column 0.
            sim.observe("OUT", loop.OUT, Vss); // Column 1.
            sim.observe("CONTROL", loop.CONTROL, Vss); // Column 2.
            sim.observe("PHASE_DETECTOR", loop.detector.Y, Vss);
            feedback_checks::lock(sim, 12000);
            feedback_checks::bounds(sim, "control stays inside tuning range", 2, .01, .015, .4, 1.1);
            feedback_checks::bounds(sim, "oscillator stays inside supply rails", 1, .01, .015, -.01, 1.21);
        });
}
