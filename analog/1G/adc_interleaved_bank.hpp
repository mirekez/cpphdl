#pragma once
#include "adc_sar_control.hpp"

// A new launch may reuse a slice only after its SAR phase returns to idle.
// Thirteen slices are the minimum for this twelve-clock conversion plus the
// launch boundary. Smaller banks deliberately retain collision reporting.
template <unsigned Slices = 13>
class InterleavedSARBankController : public Module {
public:
    static_assert(Slices >= 1 && Slices <= 16, "SAR bank supports one to sixteen slices");
    _PORT(bool) enable_in;
    _PORT(bool) sample_enable_in;
    _PORT(u<Slices>) comparisons_in;
    _PORT(u<8>) trial_out[Slices];
    _PORT(bool) acquire_out[Slices];
    _PORT(bool) convert_out[Slices];
    _PORT(u<8>) data_out = _ASSIGN_REG(data);
    _PORT(u<4>) slice_out = _ASSIGN_REG(slice);
    _PORT(bool) valid_out = _ASSIGN_REG(valid);
    _PORT(bool) fault_out = _ASSIGN_REG(fault);
private:
    ADCSARController converters[Slices];
    reg<u<Slices>> launch_slot{};
    reg<u<4>> slice{};
    reg<u<8>> data{};
    reg<u1> valid{}, fault{};
public:
    void _assign() {
        for (unsigned i = 0; i < Slices; ++i) {
            converters[i].start_in = _ASSIGN_I(enable_in() && sample_enable_in() && ((unsigned(launch_slot) >> i) & 1));
            converters[i].comparison_in = _ASSIGN_I((unsigned(comparisons_in()) >> i) & 1);
            trial_out[i] = _ASSIGN_I(converters[i].trial_out());
            acquire_out[i] = _ASSIGN_I(converters[i].acquire_out());
            convert_out[i] = _ASSIGN_I(converters[i].convert_out());
            converters[i]._assign();
        }
    }
    void _work(bool reset) {
        unsigned mask, bits, group[4], values[4], indexes[4], multiple;
        mask = 0;
        multiple = 0;
        for (unsigned i = 0; i < 4; ++i) {
            group[i] = 0;
            values[i] = 0;
            indexes[i] = 0;
        }
        // Each slice consumes its own launch bit, avoiding a broadcast binary
        // counter and per-slice comparators on the converter's start path.
        launch_slot._next = (launch_slot << 1) | (launch_slot >> (Slices - 1));
        valid._next = 0;
        for (unsigned i = 0; i < Slices; ++i) {
            if (converters[i].valid_out()) {
                mask |= 1u << i;
                values[i / 4] = unsigned(converters[i].data_out());
                indexes[i / 4] = i;
            }
            if (enable_in() && sample_enable_in() && ((unsigned(launch_slot) >> i) & 1) && converters[i].busy_out())
                fault._next = 1;
            converters[i]._work(reset || !enable_in());
        }
        // Four-bit groups bound collision logic depth. Unused high positions
        // remain zero and disappear during synthesis of smaller banks.
        for (unsigned i = 0; i < 4; ++i) {
            bits = (mask >> (4 * i)) & 15;
            group[i] = unsigned(bits != 0);
            // Preserve the original highest-slice priority through two
            // four-way selections with constant empty positions removed.
            if (group[i]) {
                data._next = values[i];
                slice._next = indexes[i];
            }
            multiple |= unsigned((bits & 3) == 3 || (bits & 12) == 12 ||
                                 ((bits & 3) != 0 && (bits & 12) != 0));
        }
        valid._next = group[0] || group[1] || group[2] || group[3];
        if (multiple || (group[0] && group[1]) || (group[2] && group[3]) ||
            ((group[0] || group[1]) && (group[2] || group[3]))) {
            fault._next = 1;
            valid._next = 0;
        }
        if (reset || !enable_in()) {
            launch_slot._next = 1;
            slice._next = 0;
            data._next = 0;
            valid._next = 0;
            fault._next = 0;
        }
    }
    void _strobe() {
        launch_slot.strobe();
        slice.strobe();
        data.strobe();
        valid.strobe();
        fault.strobe();
        for (unsigned i = 0; i < Slices; ++i)
            converters[i]._strobe();
    }
};
