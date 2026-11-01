#pragma once
#include "adc_interleaved_bank.hpp"
#include "receive_normalizer.hpp"
#include "adc_startup.hpp"

// One lane's digital acquisition sequence. Power-good/clock qualification is
// external. Track all real slices, open them for four clocks, then admit
// conversions. Analog level translators connect these controls to the IC.
template <unsigned Slices = 13>
class ADCReceiveLaneController : public Module {
public:
    static_assert(Slices >= 13 && Slices <= 16, "a streaming lane needs thirteen to sixteen slices");
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
    _PORT(bool) valid_out = _ASSIGN(enable_in() && adc.valid_out());
    _PORT(bool) fault_out = _ASSIGN(adc.fault_out());
    _PORT(u<8>) normalized_out = _ASSIGN(normalizer.sample_out());
    _PORT(bool) normalized_valid_out = _ASSIGN(enable_in() && normalizer.valid_out());
    _PORT(u<4>) normalized_slice_out = _ASSIGN_REG(slice_delay[7]);
    _PORT(bool) clipped_out = _ASSIGN(normalizer.clipped_out());
    _PORT(bool) precharge_out = _ASSIGN(startup.precharge_out());
    _PORT(bool) clear_out = _ASSIGN(startup.clear_out());
    _PORT(bool) startup_ready_out = _ASSIGN(startup.ready_out());
    _PORT(bool) evaluate_enable_out = _ASSIGN(enable_in() && startup.ready_out());
    _PORT(u<16>) aperture_out = _ASSIGN(enable_in() && startup.ready_out() ? adc.aperture_out() : u<16>(0));

private:
    ADCStartupSequencer<> startup;
    InterleavedSARBankController<Slices> adc;
    ReceiveSampleNormalizer normalizer;
    reg<u<4>> slice_delay[8]{};

public:
    void _assign() {
        startup.enable_in = _ASSIGN(enable_in());
        // Ready holds both pipelines reset. Their acquisition and valid bits
        // clear on the same edge as startup, so output gates need only enable.
        adc.enable_in = _ASSIGN(enable_in() && startup.ready_out());
        adc.sample_enable_in = _ASSIGN(sample_enable_in() && startup.ready_out());
        adc.comparisons_in = _ASSIGN(u<Slices>(unsigned(comparisons_in())));
        normalizer.sample_in = _ASSIGN(adc.data_out());
        normalizer.valid_in = _ASSIGN(adc.valid_out());
        normalizer.offset_in = _ASSIGN(offset_in());
        normalizer.gain_in = _ASSIGN(gain_in());
        normalizer.enable_in = _ASSIGN(enable_in() && startup.ready_out());
        for (unsigned i = 0; i < 16; ++i) {
            if (i < Slices) {
                trial_out[i] = _ASSIGN_I(adc.trial_out[i]());
                acquire_out[i] = _ASSIGN_I(enable_in() && (startup.precharge_out() || adc.acquire_out[i]()));
                convert_out[i] = _ASSIGN_I(adc.convert_out[i]());
            } else {
                trial_out[i] = _ASSIGN_I(0);
                acquire_out[i] = _ASSIGN_I(false);
                convert_out[i] = _ASSIGN_I(false);
            }
        }
        startup._assign();
        adc._assign();
        normalizer._assign();
    }
    void _work(bool reset) {
        slice_delay[0]._next = adc.slice_out();
        for (unsigned i = 1; i < 8; ++i)
            slice_delay[i]._next = slice_delay[i - 1];
        if (reset || !enable_in() || !startup.ready_out())
            for (unsigned i = 0; i < 8; ++i)
                slice_delay[i]._next = 0;
        startup._work(reset);
        adc._work(reset);
        normalizer._work(reset);
    }
    void _strobe() {
        startup._strobe();
        adc._strobe();
        normalizer._strobe();
        for (unsigned i = 0; i < 8; ++i)
            slice_delay[i].strobe();
    }
};
