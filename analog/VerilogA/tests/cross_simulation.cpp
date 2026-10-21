#include <cpphdl_veriloga.h>
#include <VerilogA/experiment.hpp>
#include <basic/basic.hpp>
#include <iostream>

using namespace analog;
using namespace analog::literals;

struct timing {
    duration stop, step;
};

static timing build(simulation& sim, const std::string& name) {
    electrical Vss, Vdd;
    sim.reference(Vss);
    if (name == "divider") {
        Vdd = sim_voltage_source(Vss, 1.2_V);
        resistor_divider divider;
        divider.TOP = Vdd;
        divider.BOTTOM = Vss;
        divider.upper = 1_kOhm;
        divider.lower = 2_kOhm;
        divider.elaborate();
        sim.observe("OUT", divider.TAP, Vss);
        sim.require_final_voltage("loaded fraction", divider.TAP, Vss, .799999_V, .800001_V);
        return {1_us, 100_ns};
    }
    if (name == "rc" || name == "rc_initial" || name == "floating_charge") {
        Vdd = sim_voltage_source(Vss, name == "rc_initial" ? 0_V : 1_V);
        electrical OUT, RETURN;
        if (name == "floating_charge") {
            OUT = resistor(Vdd, 2_kOhm);
            OUT = capacitor(RETURN, 1_nF, .7_V);
            Vss = resistor(RETURN, 1_kOhm);
            sim.observe("RETURN", RETURN, Vss);
        } else {
            OUT = resistor(Vdd, 1_kOhm);
            OUT = capacitor(Vss, 1_nF, name == "rc_initial" ? 1_V : 0_V);
        }
        sim.observe("OUT", OUT, Vss);
        return {name == "floating_charge" ? 15_us : 5_us, 100_ns};
    }
    if (name == "mos_regions") {
        Vdd = sim_voltage_source(Vss, 1.2_V);
        for (bool p : {false, true})
            for (bool reverse : {false, true})
                for (double gate : {.2, .7, 1.6}) {
                    electrical GATE, OUT;
                    GATE = sim_voltage_source(Vss, voltage(p ? 1.2 - gate : gate));
                    if (p) {
                        Vss = resistor(OUT, 10_kOhm);
                        if (reverse)
                            Vdd = pmos(GATE, OUT);
                        else
                            OUT = pmos(GATE, Vdd);
                    } else {
                        OUT = resistor(Vdd, 10_kOhm);
                        if (reverse)
                            OUT = nmos(GATE, Vss);
                        else
                            Vss = nmos(GATE, OUT);
                    }
                    sim.observe(std::string(p ? "PMOS" : "NMOS") + (reverse ? "_reverse_" : "_forward_") +
                                    std::to_string(gate),
                                OUT, Vss);
                }
        return {1_us, 100_ns};
    }
    if (name == "inverter") {
        Vdd = sim_voltage_source(Vss, 1.2_V);
        electrical A;
        A = sim_voltage_source(Vss, 0_V, 250_ns, 1.2_V);
        cmos<> gate;
        gate.A = A;
        gate.Vdd = Vdd;
        gate.Vss = Vss;
        gate.elaborate();
        gate.Y = capacitor(Vss, 10_pF);
        Vss = resistor(gate.Y, 100_kOhm);
        sim.observe("Y", gate.Y, Vss);
        return {500_ns, 1_ns};
    }
    if (name == "controlled_resistor") {
        electrical BIAS, HIGH, IN;
        BIAS = sim_voltage_source(Vss, .4_V);
        HIGH = sim_voltage_source(BIAS, 1.2_V);
        IN = sim_voltage_source(BIAS, .5_V, 1_us, .9_V);
        cmos<fast_model> gate;
        gate.A = IN;
        gate.Vdd = HIGH;
        gate.Vss = BIAS;
        gate.elaborate();
        gate.Y = capacitor(BIAS, 10_pF);
        Vss = resistor(gate.Y, 1_kOhm); // Load to a different reference; output may fall below its local Vss.
        sim.observe("Y_relative_to_bias", gate.Y, BIAS);
        return {2_us, 1_ns};
    }
    if (name == "driver") {
        Vdd = sim_voltage_source(Vss, 1.2_V, 1_us, .9_V);
        bits<1> enable;
        sim.step(enable, 250_ns, 0, 1);
        electrical OUT;
        OUT = sim_driver(enable, Vdd, Vss, 1_kOhm);
        OUT = capacitor(Vss, 10_pF);
        Vss = resistor(OUT, 1_kOhm);
        sim.observe("OUT", OUT, Vss);
        return {2_us, duration(.5e-9)};
    }
    if (name == "opamp") {
        Vdd = sim_voltage_source(Vss, 3.3_V);
        electrical IN;
        IN = sim_voltage_source(Vss, .5_V);
        non_inverting_amplifier amplifier;
        amplifier.IN = IN;
        amplifier.Vdd = Vdd;
        amplifier.Vss = Vss;
        amplifier.elaborate();
        Vss = resistor(amplifier.OUT, 100_kOhm);
        sim.observe("OUT", amplifier.OUT, Vss);
        sim.observe("FEEDBACK", amplifier.feedback.TAP, Vss);
        sim.observe("TAIL", amplifier.amplifier.TAIL, Vss);
        sim.observe("MIRROR", amplifier.amplifier.MIRROR, Vss);
        return {40_us, 100_ns};
    }
    if (name == "ldo" || name == "ldo_dropout") {
        Vdd = sim_voltage_source(Vss, 1.6_V, 20_us, name == "ldo_dropout" ? 1.15_V : 1.4_V);
        electrical REF, LOAD, GATE;
        REF = sim_voltage_source(Vss, .6_V);
        GATE = sim_voltage_source(Vss, 0_V, 40_us, 1.8_V);
        ldo regulator;
        regulator.Vdd = Vdd;
        regulator.Vss = Vss;
        regulator.REF = REF;
        regulator.elaborate();
        regulator.OUT = capacitor(Vss, 1_uF);
        Vss = resistor(regulator.OUT, 200_Ohm);
        LOAD = resistor(regulator.OUT, 200_Ohm);
        Vss = nmos(GATE, LOAD, mos_model{channel_strength(.1), .35_V});
        sim.observe("OUT", regulator.OUT, Vss);
        sim.observe("CONTROL", regulator.CONTROL, Vss);
        sim.observe("FEEDBACK", regulator.FEEDBACK, Vss);
        // Resolve startup ringing; the long examples intentionally check only settled windows.
        return {80_us, 2_ns};
    }
    if (name == "pll") {
        Vdd = sim_voltage_source(Vss, 1.2_V);
        bits<1> reference;
        sim.clock(reference, duration(1.0 / 12000));
        pll loop;
        loop.Vdd = Vdd;
        loop.Vss = Vss;
        loop.REF = sim_driver(reference, Vdd, Vss, 100_Ohm);
        loop.oscillator.stages[2].initial = 1.2_V;
        loop.elaborate();
        sim.observe("REF", loop.REF, Vss);
        sim.observe("OUT", loop.OUT, Vss);
        sim.observe("CONTROL", loop.CONTROL, Vss);
        return {15_ms, 1_us};
    }
    if (name == "agc") {
        Vdd = sim_voltage_source(Vss, 3.3_V);
        electrical BIAS, REF, HI1, HI, LO1, LO;
        BIAS = sim_voltage_source(Vss, 1.2_V);
        REF = sim_voltage_source(Vss, 1_V);
        HI1 = sim_voltage_source(BIAS, .05_V, 80_ms, .15_V);
        HI = sim_voltage_source(HI1, 0_V, 160_ms, voltage(-.1));
        LO1 = sim_voltage_source(BIAS, voltage(-.05), 80_ms, voltage(-.15));
        LO = sim_voltage_source(LO1, 0_V, 160_ms, .1_V);
        bits<1> carrier;
        sim.clock(carrier, 100_us);
        agc loop;
        loop.Vdd = Vdd;
        loop.Vss = Vss;
        loop.REF = REF;
        loop.BIAS = BIAS;
        loop.IN = sim_driver(carrier, HI, LO, 100_Ohm);
        loop.gain.amplifier.initial_output = 1.2_V;
        loop.controller.amplifier.initial_output = 2_V;
        loop.controller.initial_difference = voltage(-1);
        loop.detector.initial = .8_V;
        loop.elaborate();
        sim.observe("OUT", loop.OUT, Vss);
        sim.observe("CONTROL", loop.CONTROL, Vss);
        sim.observe("ENVELOPE", loop.ENVELOPE, Vss);
        return {240_ms, 5_us};
    }
    throw diagnostic("E_VALUE", "unknown cross-simulation case: " + name);
}

int main(int argc, char** argv) {
    if (argc != 4)
        return 2;
    try {
        simulation sim;
        auto timing = build(sim, argv[1]);
        double refinement = std::stod(argv[3]);
        if (refinement != 1 && refinement != 2)
            throw diagnostic("E_VALUE", "refinement must be 1 or 2");
        timing.step = duration(timing.step.value / refinement);
        std::filesystem::path output = argv[2];
        verilog_a::write_experiment(sim, output, {timing.stop, timing.step, duration(timing.step.value / 2)});
        sim.run(timing.stop, timing.step);
        sim.write(output / "cpp");
        return sim.status() == "sampled_pass" ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
