#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Charge every real hold capacitor before allowing the conversion bank to
// launch. The parent combines precharge_out with each slice's tracking bit,
// holds comparator evaluation off while clear_out is asserted, and admits
// samples only after ready_out. enable_in requires valid supplies and clock.
template <unsigned PrechargeClocks = 4096, unsigned ReleaseClocks = 4>
class ADCStartupSequencer : public Module {
public:
    static_assert(PrechargeClocks > 0 && PrechargeClocks <= 65535, "invalid ADC precharge duration");
    static_assert(ReleaseClocks > 0 && ReleaseClocks <= 65535, "invalid ADC release duration");
    _PORT(bool) enable_in;
    _PORT(bool) precharge_out = _ASSIGN(enable_in() && phase == 0);
    _PORT(bool) clear_out = _ASSIGN(!enable_in() || phase != 2);
    _PORT(bool) ready_out = _ASSIGN(enable_in() && phase == 2);

private:
    reg<u<2>> phase{};
    reg<u<16>> elapsed{};

public:
    void _assign() {}
    void _work(bool reset) {
        if (phase == 0) {
            elapsed._next = elapsed + 1;
            if (elapsed == PrechargeClocks - 1) {
                phase._next = 1;
                elapsed._next = 0;
            }
        } else if (phase == 1) {
            elapsed._next = elapsed + 1;
            if (elapsed == ReleaseClocks - 1) {
                phase._next = 2;
                elapsed._next = 0;
            }
        }
        if (reset || !enable_in()) {
            phase._next = 0;
            elapsed._next = 0;
        }
    }
    void _strobe() {
        phase.strobe();
        elapsed.strobe();
    }
};
