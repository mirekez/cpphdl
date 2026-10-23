#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Data-derived timing correction. A sampling/interpolation circuit supplies
// observations at t - T/2, t, t + T/2 and consumes the returned next interval.
// Six-cycle latency, one observation per cycle; reset discards pending work.
class ReceiveTimingLoop : public Module {
public:
    _PORT(u<8>) early_in;
    _PORT(u<8>) middle_in;
    _PORT(u<8>) late_in;
    _PORT(bool) advance_in;
    _PORT(bool) track_in;
    _PORT(u<25>) interval_out = _ASSIGN_REG(interval);
    _PORT(u<25>) period_out = _ASSIGN_REG(period);
    _PORT(u<24>) phase_out = _ASSIGN_REG(phase);
    _PORT(u<14>) error_out = _ASSIGN_REG(error);
    _PORT(bool) activity_out = _ASSIGN_REG(activity);
    _PORT(bool) valid_out = _ASSIGN_REG(valid);

private:
    reg<u<23>> frequency{}; // Signed Q30 change in periods, limited to +/-2000 ppm.
    reg<u<25>> interval{}, period{};
    reg<u<24>> phase{};
    reg<u<14>> error{};
    reg<u1> activity{}, valid{};
    reg<u<9>> difference{};
    reg<u<8>> middle{};
    reg<u<18>> product{};
    reg<u<14>> limited{}, error_delay[3]{};
    reg<u<21>> proportional{};
    reg<u<25>> nominal{}, nominal_delay{}, next_interval{};
    reg<u<6>> accepted{}, tracking{}, transitions{};

    int signed_value(unsigned value, unsigned sign) {
        return int(value ^ sign) - int(sign);
    }

public:
    void _work(bool reset) {
        int delta, observation, accumulated, drift;
        u<24> candidate;
        delta = int(unsigned(early_in())) - int(unsigned(late_in()));
        difference._next = delta;
        middle._next = unsigned(middle_in()) ^ 128;
        accepted._next = (accepted << 1) | unsigned(advance_in());
        tracking._next = (tracking << 1) | unsigned(advance_in() && track_in());
        transitions._next = (transitions << 1) | unsigned(delta < -1 || delta > 1);
        // Register subtraction, product, clamp and integration separately.
        product._next = signed_value(unsigned(difference), 256) * signed_value(unsigned(middle), 128);
        observation = signed_value(unsigned(product), 131072);
        observation = observation < -4096 ? -4096 : observation > 4096 ? 4096 : observation;
        limited._next = observation;
        observation = signed_value(unsigned(limited), 8192);
        accumulated = signed_value(unsigned(frequency), 4194304);
        candidate = accumulated + observation;
        if ((accepted & 4) && (tracking & 4)) {
            accumulated = signed_value(unsigned(candidate), 8388608);
            frequency._next = accumulated < -2147484 ? -2147484 : accumulated > 2147484 ? 2147484 : accumulated;
        }
        error_delay[0]._next = limited;
        error_delay[1]._next = error_delay[0];
        error_delay[2]._next = error_delay[1];
        // Signed truncation uses explicit magnitude shifts for synthesis.
        accumulated = signed_value(unsigned(frequency), 4194304);
        drift = accumulated < 0 ? -int(unsigned(-accumulated) >> 6) : int(unsigned(accumulated) >> 6);
        nominal._next = 16777216 - drift;
        proportional._next = (tracking & 8) ? signed_value(unsigned(error_delay[0]), 8192) * 160 : 0;
        nominal_delay._next = nominal;
        next_interval._next = unsigned(nominal) - signed_value(unsigned(proportional), 1048576);
        valid._next = 0;
        activity._next = 0;
        if (accepted & 32) {
            period._next = nominal_delay;
            interval._next = next_interval;
            phase._next = unsigned(phase) + unsigned(next_interval);
            error._next = error_delay[2];
            activity._next = (transitions >> 5) & 1;
            valid._next = 1;
        }
        if (reset) {
            frequency._next = 0;
            interval._next = 16777216;
            period._next = 16777216;
            phase._next = 0;
            error._next = 0;
            activity._next = 0;
            valid._next = 0;
            accepted._next = 0;
            tracking._next = 0;
            transitions._next = 0;
        }
    }

    void _strobe() {
        frequency.strobe();
        interval.strobe();
        period.strobe();
        phase.strobe();
        error.strobe();
        activity.strobe();
        valid.strobe();
        difference.strobe();
        middle.strobe();
        product.strobe();
        limited.strobe();
        for (unsigned i = 0; i < 3; ++i)
            error_delay[i].strobe();
        proportional.strobe();
        nominal.strobe();
        nominal_delay.strobe();
        next_interval.strobe();
        accepted.strobe();
        tracking.strobe();
        transitions.strobe();
    }
};
