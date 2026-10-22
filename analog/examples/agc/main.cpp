#include "../feedback_checks.hpp"

int main(int argc, char** argv) {
    std::string scenario = argc > 2 ? argv[2] : "nominal";
    if (!feedback_checks::scenario_is(scenario, {"nominal", "weak", "refined"}))
        return 2;
    bool weak = scenario == "weak";
    return example_main(
        argc, argv, "examples/agc/results", 240_ms, scenario == "refined" ? duration(2.5e-6) : 5_us,
        [=](simulation& sim) {
            electrical Vdd, Vss, BIAS, REF, HIGH_FIRST, HIGH, LOW_FIRST, LOW;
            sim.reference(Vss);
            Vdd = sim_voltage_source(Vss, 3.3_V);
            BIAS = sim_voltage_source(Vss, 1.2_V);
            REF = sim_voltage_source(Vss, 1_V); // Detector target, not output amplitude.
            double amplitude = weak ? .005 : .05;
            HIGH_FIRST = sim_voltage_source(BIAS, voltage(amplitude), 80_ms, voltage(3 * amplitude));
            HIGH = sim_voltage_source(HIGH_FIRST, 0_V, 160_ms, voltage(-2 * amplitude));
            LOW_FIRST = sim_voltage_source(BIAS, voltage(-amplitude), 80_ms, voltage(-3 * amplitude));
            LOW = sim_voltage_source(LOW_FIRST, 0_V, 160_ms, voltage(2 * amplitude));
            bits<1> carrier;
            sim.clock(carrier, 100_us);
            agc loop;
            loop.Vdd = Vdd;
            loop.Vss = Vss;
            loop.BIAS = BIAS;
            loop.REF = REF;
            loop.IN = sim_driver(carrier, HIGH, LOW, 100_Ohm);
            // This experiment begins biased: these are capacitor initial conditions, not ideal gain/control laws.
            loop.gain.amplifier.initial_output = 1.2_V;
            loop.controller.amplifier.initial_output = 2_V;
            loop.controller.initial_difference = voltage(-1);
            loop.detector.initial = .8_V;
            loop.name("agc");
            loop.elaborate();

            sim.observe("IN", loop.IN, Vss); // Column 0.
            sim.observe("OUT", loop.OUT, Vss); // Column 1.
            sim.observe("CONTROL", loop.CONTROL, Vss); // Column 2.
            sim.observe("ENVELOPE", loop.ENVELOPE, Vss); // Column 3.
            if (weak) {
                feedback_checks::bounds(sim, "weak input cannot reach detector target", 3, .22, .235, .7, .91);
                feedback_checks::bounds(sim, "gain control saturates", 2, .22, .235, 3.2, 3.31);
            } else {
                for (double start : {.06, .14, .22}) {
                    std::string name = "settled window " + std::to_string(start);
                    feedback_checks::bounds(sim, name + " detector", 3, start, start + .015, .98, 1.02);
                    sim.require_trace(name + " positive output peak", [=](const auto& trace) -> std::optional<bool> {
                        auto out = feedback_checks::window(trace, 1, start, start + .015);
                        if (!out)
                            return std::nullopt;
                        std::cout << name << ": positive excursion=" << out->high - 1.2 << " V\n";
                        return out->high > 1.35 && out->high < 1.39;
                    });
                }
                feedback_checks::bounds(sim, "initial gain", 2, .06, .075, 2.8, 3.15);
                feedback_checks::bounds(sim, "strong input reduces gain", 2, .14, .155, 1.70, 1.80);
                feedback_checks::bounds(sim, "small input restores gain", 2, .22, .235, 2.8, 3.15);
            }
        });
}
