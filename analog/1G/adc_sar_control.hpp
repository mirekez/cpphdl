#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Eight-bit successive-approximation sequencer. Its comparator evaluates on
// the opposite half of the controller clock, after DAC settling. This module
// supplies control logic; sampling, references, DAC and comparator are circuits.
class ADCSARController : public Module {
public:
    _PORT(bool) start_in;
    _PORT(bool) comparison_in; // True when the held input is at/above the trial threshold.
    _PORT(u<8>) trial_out = _ASSIGN_REG(trial);
    _PORT(u<8>) data_out = _ASSIGN_REG(data);
    _PORT(bool) valid_out = _ASSIGN_REG(valid);
    _PORT(bool) acquire_out = _ASSIGN_REG(acquire);
    _PORT(bool) convert_out = _ASSIGN_REG(convert);
    _PORT(bool) busy_out = _ASSIGN(phase != 0);

private:
    reg<u<3>> phase{};
    reg<u<8>> trial{}, mask{}, data{};
    reg<u1> valid{}, acquire{}, convert{};

public:
    void _assign() {}

    void _work(bool reset) {
        unsigned accepted;
        accepted = 0;
        valid._next = 0;
        switch (unsigned(phase)) {
        case 0:
            if (start_in()) {
                acquire._next = 0;
                convert._next = 0;
                trial._next = 0;
                mask._next = 0;
                phase._next = 1;
            }
            break;
        case 1:
            acquire._next = 1;
            phase._next = 2;
            break;
        case 2:
            phase._next = 3;
            break;
        case 3:
            acquire._next = 0; // A full clock separates sampling from reference switching.
            phase._next = 4;
            break;
        case 4:
            convert._next = 1;
            trial._next = 128;
            mask._next = 128;
            phase._next = 5;
            break;
        case 5:
            accepted = comparison_in() ? unsigned(trial) : unsigned(trial & ~mask);
            if (mask == 1) {
                trial._next = accepted;
                data._next = accepted;
                valid._next = 1;
                convert._next = 0;
                phase._next = 0;
            } else {
                mask._next = mask >> 1;
                trial._next = accepted | unsigned(mask >> 1);
            }
            break;
        default:
            acquire._next = 0;
            convert._next = 0;
            phase._next = 0;
            break;
        }
        if (reset) {
            phase._next = 0;
            trial._next = 0;
            mask._next = 0;
            data._next = 0;
            valid._next = 0;
            acquire._next = 0;
            convert._next = 0;
        }
    }

    void _strobe() {
        phase.strobe();
        trial.strobe();
        mask.strobe();
        data.strobe();
        valid.strobe();
        acquire.strobe();
        convert.strobe();
    }
};
