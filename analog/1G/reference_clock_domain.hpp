#pragma once
#include "reference_clock.hpp"
#include "clock_level_shifter.hpp"
#include <basic/inverter.hpp>

namespace analog::one_g {

// One low-voltage reference drives the physical PLL and its loaded 3.3 V
// clock interface. Both supplies and the shared translator bias remain wires.
template <class Multiplier = reference_clock_multiplier>
class basic_reference_clock_domain : public IC {
public:
    _PORT(electrical) Vlogic;
    _PORT(electrical) REF;
    _PORT(electrical) CLEAR;
    _PORT(electrical) OUT;
    _PORT(electrical) OUT_N;
    Multiplier multiplier;
    cmos<> predriver, driver;
    level_shift_bias bias;
    clock_level_shifter translator;
    capacitance_value logic_decoupling{5e-12};

    void _assign() override {
        multiplier.REF = REF;
        multiplier.CLEAR = CLEAR;
        multiplier.Vdd = Vlogic;
        multiplier.Vss = Vss;
        multiplier.elaborate();
        // A small first input isolates the oscillator from the much larger
        // translator gates. The second stage supplies their switching charge.
        predriver.A = multiplier.OUT;
        predriver.model.transistor_parameters = {channel_strength(.004), voltage(.35)};
        predriver.Vdd = Vlogic;
        predriver.Vss = Vss;
        predriver.elaborate();
        driver.A = predriver.Y;
        driver.model.transistor_parameters = {channel_strength(.02), voltage(.35)};
        driver.Vdd = Vlogic;
        driver.Vss = Vss;
        driver.elaborate();
        multiplier.OUT = capacitor(Vss, capacitance_value(2e-15));
        multiplier.OUT = capacitor(Vlogic, capacitance_value(2e-15));
        multiplier.OUT = capacitor(predriver.Y, capacitance_value(1e-15));
        predriver.Y = capacitor(Vss, capacitance_value(15e-15));
        predriver.Y = capacitor(Vlogic, capacitance_value(10e-15));
        predriver.Y = capacitor(driver.Y, capacitance_value(5e-15));
        driver.Y = capacitor(Vss, capacitance_value(10e-15));
        Vlogic = capacitor(Vss, logic_decoupling);
        bias.Vdd = Vdd;
        bias.Vss = Vss;
        bias.Vlogic = Vlogic;
        bias.BIAS = translator.BIAS;
        bias.elaborate();
        translator.A = driver.Y;
        translator.Y = OUT;
        translator.Y_N = OUT_N;
        translator.Vdd = Vdd;
        translator.Vlogic = Vlogic;
        translator.Vss = Vss;
        translator.elaborate();
    }
};
class reference_clock_domain : public basic_reference_clock_domain<> {};
} // namespace analog::one_g
