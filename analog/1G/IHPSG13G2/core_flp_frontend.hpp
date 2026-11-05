#pragma once
#include "../flp_frontend.hpp"
#include "../clock_level_shifter.hpp"
#include <basic/level_down.hpp>

namespace analog::one_g::ihp_sg13g2 {

// Core negotiation controls drive the I/O pulse circuit through physical
// translators. TX and RX pairs stay explicit for MDI/MDI-X board wiring.
// Process sizing and pulse detection with the complete PAM5 load remain open.
class core_flp_frontend : public IC {
public:
    _PORT(electrical) Vlogic;
    _PORT(electrical) COMMAND;
    _PORT(electrical) ENABLE;
    _PORT(electrical) TX_P;
    _PORT(electrical) TX_N;
    _PORT(electrical) RX_P;
    _PORT(electrical) RX_N;
    _PORT(electrical) PULSE;
    level_shift_bias bias;
    std::array<clock_level_shifter, 2> controls;
    flp_pulse_driver driver;
    flp_pulse_detector detector;
    level_down returned;

    void _assign() override {
        bias.Vdd = Vdd;
        bias.Vss = Vss;
        bias.Vlogic = Vlogic;
        bias.elaborate();
        for (unsigned i = 0; i < 2; ++i) {
            controls[i].A = i == 0 ? COMMAND : ENABLE;
            controls[i].Vdd = Vdd;
            controls[i].Vss = Vss;
            controls[i].Vlogic = Vlogic;
            controls[i].BIAS = bias.BIAS;
            controls[i].elaborate();
        }
        driver.COMMAND = controls[0].Y;
        driver.ENABLE = controls[1].Y;
        driver.P = TX_P;
        driver.N = TX_N;
        driver.Vdd = Vdd;
        driver.Vss = Vss;
        driver.elaborate();
        detector.P = RX_P;
        detector.N = RX_N;
        detector.Vdd = Vdd;
        detector.Vss = Vss;
        detector.elaborate();
        returned.A = detector.PULSE;
        returned.Y = PULSE;
        returned.Vdd = Vlogic;
        returned.Vss = Vss;
        returned.elaborate();
    }
};
} // namespace analog::one_g::ihp_sg13g2
