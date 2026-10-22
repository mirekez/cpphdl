#pragma once
#include "pma_test_pattern.hpp"
#include "pam5_driver_control.hpp"

// One physical transmit lane shares its weighted driver between normal PCS
// symbols and test patterns. Symbol codes follow PCS: code = symbol + 2.
class PMATransmitLane : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(u<3>) symbol_in;
    _PORT(u<3>) test_mode_in;
    _PORT(u<11>) test_seed_in;
    _PORT(bool) restart_in;
    _PORT(u<4>) main_p_out = _ASSIGN(driver.main_p_out());
    _PORT(u<4>) main_n_out = _ASSIGN(driver.main_n_out());
    _PORT(u<4>) history_p_out = _ASSIGN(driver.history_p_out());
    _PORT(u<4>) history_n_out = _ASSIGN(driver.history_n_out());
    _PORT(bool) valid_out = _ASSIGN(driver.valid_out());
    _PORT(bool) fault_out = _ASSIGN(driver.fault_out() || (test_mode_in() != 0 && pattern.fault_out()));
    _PORT(bool) slave_clock_out = _ASSIGN(test_mode_in() == 3 && pattern.slave_clock_out());

private:
    PMATestPattern pattern;
    PAM5DriverControl driver;
    reg<u<3>> previous_mode{};

public:
    void _assign() {
        pattern.enable_in = _ASSIGN(enable_in());
        pattern.mode_in = _ASSIGN(test_mode_in());
        pattern.seed_in = _ASSIGN(test_seed_in());
        pattern.restart_in = _ASSIGN(restart_in());
        // Complement the code to preserve PCS polarity at physical P minus N.
        // Preserve invalid codes so the driver's fault detector can reject them.
        driver.symbol_in = _ASSIGN(test_mode_in() == 0 ?
            (symbol_in() <= 4 ? 4 - unsigned(symbol_in()) : unsigned(symbol_in())) : 4 - unsigned(pattern.symbol_out()));
        driver.enable_in = _ASSIGN(enable_in() && (test_mode_in() == 0 || pattern.valid_out()));
        pattern._assign();
        driver._assign();
    }
    void _work(bool reset) {
        previous_mode._next = test_mode_in();
        pattern._work(reset);
        // Quiet one edge and clear history when selecting a different stream.
        // The next edge consumes the new pattern's first registered symbol.
        driver._work(reset || !enable_in() || restart_in() || test_mode_in() != previous_mode);
        if (reset || !enable_in())
            previous_mode._next = 0;
    }
    void _strobe() {
        pattern._strobe();
        driver._strobe();
        previous_mode.strobe();
    }
};
