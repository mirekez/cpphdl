#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef VERILATOR
#include "VProcessingClockControl.h"
using DUT = VProcessingClockControl;
#define READ(name) unsigned(dut.name)
#else
#ifndef PROCESSING_CLOCK_HEADER
#define PROCESSING_CLOCK_HEADER "../processing_clock_control.hpp"
#endif
#include PROCESSING_CLOCK_HEADER
using DUT = ProcessingClockControl;
long _system_clock = -1;
#define READ(name) unsigned(dut.name())
#endif
#include "processing_clock_load_step.hpp"
#include "processing_clock_drift.hpp"

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("expected trace path");
        std::ofstream trace(argv[1]);
        trace.exceptions(std::ios::failbit | std::ios::badbit);
        unsigned events = 0;
        check_clock_drift(trace, events);
        std::cout << "PASS two clock drift recoveries; " << events << " exact events\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
