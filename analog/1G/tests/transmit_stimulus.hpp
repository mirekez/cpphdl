#pragma once
#include "../pam5_driver_control.hpp"
#include <vector>

struct DriverWord {
    unsigned code, previous, main_p, main_n, history_p, history_n;
    bool enable, reset, valid, fault;
};

inline std::vector<DriverWord> stimulus() {
    PAM5DriverControl controller;
    unsigned code = 2, previous = 2;
    bool enable = false;
    controller.symbol_in = _ASSIGN(code);
    controller.enable_in = _ASSIGN(enable);
    controller._assign();
    std::vector<DriverWord> result;
    auto tick = [&](bool reset = false) {
        ++_system_clock;
        controller._work(reset);
        controller._strobe();
        result.push_back({code, previous, unsigned(controller.main_p_out()), unsigned(controller.main_n_out()),
            unsigned(controller.history_p_out()), unsigned(controller.history_n_out()), enable, reset,
            bool(controller.valid_out()), bool(controller.fault_out())});
        previous = controller.valid_out() ? code : 2;
    };
    tick(true);
    tick();
    enable = true;
    for (unsigned warm = 0; warm < 32; ++warm)
        tick();
    for (unsigned a = 0; a < 5; ++a)
        for (unsigned b = 0; b < 5; ++b) {
            code = a; tick();
            code = b; tick();
        }
    for (unsigned value : {0u, 4u, 2u}) {
        code = value;
        for (unsigned repeat = 0; repeat < 12; ++repeat)
            tick();
    }
    enable = false;
    tick(); tick();
    enable = true;
    code = 0;
    tick(); tick();
    code = 6;
    tick();
    code = 4;
    tick();
    tick(true);
    code = 2;
    tick(); tick();
    return result;
}
