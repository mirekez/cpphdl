#pragma once
#include "adc_interleave.hpp"
#include "receive_normalizer.hpp"

// One receive lane's SAR control and calibrated sample stream. Physical
// comparators supply comparisons_in through the core voltage interface.
class ADCReceiveController : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) sample_enable_in;
    _PORT(u<16>) comparisons_in;
    _PORT(u<8>) offset_in;
    _PORT(u<16>) gain_in;
    _PORT(u<8>) trial_out[16];
    _PORT(bool) acquire_out[16];
    _PORT(bool) convert_out[16];
    _PORT(u<8>) data_out = _ASSIGN(adc.data_out());
    _PORT(u<4>) slice_out = _ASSIGN(adc.slice_out());
    _PORT(bool) valid_out = _ASSIGN(adc.valid_out());
    _PORT(bool) fault_out = _ASSIGN(adc.fault_out());
    _PORT(u<8>) normalized_out = _ASSIGN(normalizer.sample_out());
    _PORT(bool) normalized_valid_out = _ASSIGN(normalizer.valid_out());
    _PORT(bool) clipped_out = _ASSIGN(normalizer.clipped_out());
    _PORT(u<4>) normalized_slice_out = _ASSIGN_REG(slice_delay[7]);

private:
    InterleavedSARController adc;
    ReceiveSampleNormalizer normalizer;
    reg<u<4>> slice_delay[8]{};

public:
    void _assign() {
        adc.enable_in = _ASSIGN(enable_in());
        adc.sample_enable_in = _ASSIGN(sample_enable_in());
        adc.comparisons_in = _ASSIGN(comparisons_in());
        for (unsigned i = 0; i < 16; ++i) {
            trial_out[i] = _ASSIGN_I(adc.trial_out[i]());
            acquire_out[i] = _ASSIGN_I(adc.acquire_out[i]());
            convert_out[i] = _ASSIGN_I(adc.convert_out[i]());
        }
        normalizer.sample_in = _ASSIGN(adc.data_out());
        normalizer.valid_in = _ASSIGN(adc.valid_out());
        normalizer.offset_in = _ASSIGN(offset_in());
        normalizer.gain_in = _ASSIGN(gain_in());
        normalizer.enable_in = _ASSIGN(enable_in());
        adc._assign();
        normalizer._assign();
    }
    void _work(bool reset) {
        slice_delay[0]._next = adc.slice_out();
        for (unsigned i = 1; i < 8; ++i)
            slice_delay[i]._next = slice_delay[i - 1];
        if (reset || !enable_in())
            for (unsigned i = 0; i < 8; ++i)
                slice_delay[i]._next = 0;
        adc._work(reset);
        normalizer._work(reset);
    }
    void _strobe() {
        adc._strobe();
        normalizer._strobe();
        for (unsigned i = 0; i < 8; ++i)
            slice_delay[i].strobe();
    }
};
