#include "../feedback_checks.hpp"

int main(int argc, char** argv) {
    std::string scenario = argc > 2 ? argv[2] : "nominal";
    if (!feedback_checks::scenario_is(scenario, {"nominal", "dropout", "refined"}))
        return 2;
    bool dropout = scenario == "dropout";
    return example_main(
        argc, argv, "examples/ldo/results", 4_ms, scenario == "refined" ? 500_ns : 1_us, [=](simulation& sim) {
            electrical VIN, REF, Vss, LOAD_GATE, LOAD;
            sim.reference(Vss);
            VIN = sim_voltage_source(Vss, 1.6_V, 1_ms, dropout ? 1.15_V : 1.4_V);
            REF = sim_voltage_source(Vss, .6_V);
            LOAD_GATE = sim_voltage_source(Vss, 0_V, 2_ms, 1.8_V);
            ldo supply;
            supply.Vdd = VIN;
            supply.Vss = Vss;
            supply.REF = REF;
            supply.name("ldo");
            supply.elaborate();
            supply.OUT = capacitor(Vss, 1_uF); // External storage affects recovery and startup.
            Vss = resistor(supply.OUT, 200_Ohm);
            LOAD = resistor(supply.OUT, 200_Ohm);
            Vss = nmos(LOAD_GATE, LOAD, mos_model{channel_strength(.1), .35_V}); // Double the load at 2 ms.

            sim.observe("VIN", VIN, Vss); // Column 0.
            sim.observe("OUT", supply.OUT, Vss); // Column 1.
            sim.observe("CONTROL", supply.CONTROL, Vss); // Column 2.
            feedback_checks::bounds(sim, "initial regulation", 1, .0007, .00095, 1.18, 1.22);
            if (dropout) {
                feedback_checks::bounds(sim, "insufficient headroom loses regulation", 1, .003, .004, 1.03, 1.15);
                feedback_checks::bounds(sim, "pass gate approaches lower rail", 2, .003, .004, .05, .15);
            } else {
                feedback_checks::bounds(sim, "line step recovery", 1, .0017, .00195, 1.18, 1.22);
                feedback_checks::bounds(sim, "load step recovery", 1, .003, .004, 1.18, 1.22);
            }
        });
}
