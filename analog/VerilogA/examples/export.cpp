#include <cpphdl_veriloga.h>
#include "inverter.hpp"
#include <iostream>

int main(int argc, char** argv) {
    try {
        analog::simulation context;
        circuits::Inverter inverter;
        inverter.name("inverter");
        inverter.elaborate();
        // Keep the reusable module separate from the testbench, including its ground and supplies.
        analog::verilog_a::write_module(
            inverter, argc > 1 ? argv[1] : "Inverter.va", "Inverter",
            {{"Vdd", inverter.Vdd}, {"Vss", inverter.Vss}, {"A", inverter.A}, {"Y", inverter.Y}});
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
