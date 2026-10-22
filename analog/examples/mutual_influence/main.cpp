#include "../common.hpp"

int main(int argc, char** argv) {
    return example_main(argc, argv, "examples/mutual_influence/results", 2_us, 5_ns, [](simulation& sim) {
        electrical ideal, Vdd, Vss, A;
        sim.reference(Vss);
        ideal = sim_voltage_source(Vss, 1.2_V);
        Vdd = resistor(ideal, 200_Ohm); // Both ICs share this imperfect supply wire.
        A = sim_voltage_source(Vss, 0_V);

        cmos<> loaded, neighbor;
        loaded.A = A;
        loaded.Vdd = Vdd;
        loaded.Vss = Vss;
        loaded.name("loaded");
        loaded.elaborate();
        neighbor.A = A;
        neighbor.Vdd = Vdd;
        neighbor.Vss = Vss;
        neighbor.name("neighbor");
        neighbor.elaborate();

        loaded.Y = capacitor(Vss, 10_pF, 1.2_V);
        neighbor.Y = capacitor(Vss, 10_pF, 1.2_V);
        bits<1> select_rail;
        sim.step(select_rail, 1_us, 1, 0);
        loaded.Y = sim_driver(select_rail, Vdd, Vss, 100_Ohm); // Competes with the internal pull-up at 1 us.

        sim.observe("LOADED", loaded.Y, Vss);
        sim.observe("NEIGHBOR", neighbor.Y, Vss);
        sim.observe("VDD", Vdd, Vss);
        sim.assume_voltage("shared supply envelope", Vdd, Vss, 1_V, 1.3_V);
        sim.require_final_voltage("loaded IC should be high", loaded.Y, Vss, 1_V, 1.3_V);
        sim.require_final_voltage("neighbor should be high", neighbor.Y, Vss, 1_V, 1.3_V);
        // Expected report: failed. The environment defeats one IC's purpose.
    });
}
