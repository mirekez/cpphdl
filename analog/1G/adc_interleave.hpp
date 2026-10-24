#pragma once
#include "adc_sar_control.hpp"

// Sixteen converter slices provide one result per controller clock. Each slice
// has sixteen clocks between starts and needs twelve clocks to convert.
// The 1G receive path targets a 250 MHz internal clock for Gardner's two
// observations per 125 MBd symbol; the external reference remains 125 MHz.
class InterleavedSARController : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) sample_enable_in;
    _PORT(u<16>) comparisons_in;
    _PORT(u<8>) trial_out[16];
    _PORT(bool) acquire_out[16];
    _PORT(bool) convert_out[16];
    _PORT(u<8>) data_out = _ASSIGN_REG(data);
    _PORT(u<4>) slice_out = _ASSIGN_REG(slice);
    _PORT(bool) valid_out = _ASSIGN_REG(valid);
    _PORT(bool) fault_out = _ASSIGN_REG(fault);
private:
    ADCSARController converters[16];
    reg<u<16>> launch_slot{};
    reg<u<4>> slice{};
    reg<u<8>> data{};
    reg<u1> valid{}, fault{};
public:
    void _assign() {
        for (unsigned i = 0; i < 16; ++i) {
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
        // counter and sixteen comparators on the converter's start path.
        launch_slot._next = (launch_slot << 1) | (launch_slot >> 15);
        valid._next = 0;
        for (unsigned i = 0; i < 16; ++i) {
            if (converters[i].valid_out()) {
                mask |= 1u << i;
                values[i / 4] = unsigned(converters[i].data_out());
                indexes[i / 4] = i;
            }
            if (enable_in() && sample_enable_in() && ((unsigned(launch_slot) >> i) & 1) && converters[i].busy_out())
                fault._next = 1;
            converters[i]._work(reset || !enable_in());
        }
        // Counting sixteen bits serially put an eight-XOR chain in the
        // routed valid path. Combine four-bit groups as (any, multiple)
        // instead; both predicates retain the original collision contract.
        for (unsigned i = 0; i < 4; ++i) {
            bits = (mask >> (4 * i)) & 15;
            group[i] = unsigned(bits != 0);
            // Preserve the original highest-slice priority through two
            // four-way selections instead of one sixteen-way chain.
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
        for (unsigned i = 0; i < 16; ++i)
            converters[i]._strobe();
    }
};
