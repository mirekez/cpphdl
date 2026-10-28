#pragma once
#include "adc_receive_lane.hpp"

// Four SAR lanes share an aperture clock. The metadata pipeline begins at
// switch opening, ten clocks before raw conversion and eighteen clocks before
// normalized data. It does not use a slice-index lookup: that slice has already
// been reused by the time normalization completes.
class ADCReceiveCapture : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) sample_enable_in;
    _PORT(u<16>) comparisons_in[4];
    _PORT(u<8>) offset_in[4];
    _PORT(u<16>) gain_in[4];
    _PORT(u<16>) time_in;
    _PORT(u<12>) local_in;
    _PORT(u<12>) desired_in;
    _PORT(bool) train_in;
    _PORT(u<8>) trial_out[64];
    _PORT(bool) acquire_out[64];
    _PORT(bool) convert_out[64];
    _PORT(u<16>) aperture_out = _ASSIGN(lanes[0].aperture_out());
    _PORT(bool) startup_ready_out = _ASSIGN(enable_in() && lanes[0].startup_ready_out() &&
        lanes[1].startup_ready_out() && lanes[2].startup_ready_out() && lanes[3].startup_ready_out());
    _PORT(u<32>) samples_out = _ASSIGN(unsigned(lanes[0].normalized_out()) |
        (unsigned(lanes[1].normalized_out()) << 8) | (unsigned(lanes[2].normalized_out()) << 16) |
        (unsigned(lanes[3].normalized_out()) << 24));
    _PORT(u<16>) tag_out = _ASSIGN(metadata[18] >> 25);
    _PORT(u<12>) local_out = _ASSIGN(metadata[18] & 4095);
    _PORT(u<12>) desired_out = _ASSIGN((metadata[18] >> 12) & 4095);
    _PORT(bool) train_out = _ASSIGN((metadata[18] >> 24) & 1);
    _PORT(bool) clipped_out = _ASSIGN(lanes[0].clipped_out() || lanes[1].clipped_out() ||
        lanes[2].clipped_out() || lanes[3].clipped_out());
    _PORT(bool) valid_out = _ASSIGN(enable_in() && ((accepted >> 18) & 1) && !fault_comb_func());
    _PORT(bool) fault_out = _ASSIGN(enable_in() && fault_comb_func());

private:
    ADCReceiveLaneController<13> lanes[4];
    reg<u<41>> metadata[19]{};
    reg<u<19>> accepted{};
    reg<u1> fault{};

    _LAZY_COMB(fault_comb, bool)
        bool bad;
        unsigned aperture;
        aperture = unsigned(lanes[0].aperture_out());
        bad = fault || ((aperture & (aperture - 1)) != 0);
        for (unsigned lane = 0; lane < 4; ++lane)
            bad = bad || lanes[lane].fault_out() || lanes[lane].aperture_out() != aperture ||
                  lanes[lane].normalized_valid_out() != bool((accepted >> 18) & 1);
        return fault_comb = bad;
    }

public:
    void _assign() {
        for (unsigned i = 0; i < 4; ++i) {
            lanes[i].enable_in = _ASSIGN(enable_in());
            lanes[i].sample_enable_in = _ASSIGN(sample_enable_in());
            lanes[i].comparisons_in = _ASSIGN_I(comparisons_in[i]());
            lanes[i].offset_in = _ASSIGN_I(offset_in[i]());
            lanes[i].gain_in = _ASSIGN_I(gain_in[i]());
            lanes[i]._assign();
        }
        for (unsigned i = 0; i < 64; ++i) {
            trial_out[i] = _ASSIGN_I(lanes[i / 16].trial_out[i % 16]());
            acquire_out[i] = _ASSIGN_I(lanes[i / 16].acquire_out[i % 16]());
            convert_out[i] = _ASSIGN_I(lanes[i / 16].convert_out[i % 16]());
        }
    }
    void _work(bool reset) {
        metadata[0]._next = (u<41>(time_in()) << 25) | (u<41>(train_in()) << 24) |
                            (u<41>(desired_in()) << 12) | local_in();
        accepted._next = (accepted << 1) | unsigned(lanes[0].aperture_out() != 0);
        for (unsigned i = 1; i < 19; ++i)
            metadata[i]._next = metadata[i - 1];
        if (fault_comb_func())
            fault._next = 1;
        if (reset || !enable_in()) {
            accepted._next = 0;
            fault._next = 0;
            for (unsigned i = 0; i < 19; ++i)
                metadata[i]._next = 0;
        }
        for (unsigned i = 0; i < 4; ++i)
            lanes[i]._work(reset);
    }
    void _strobe() {
        for (unsigned i = 0; i < 4; ++i)
            lanes[i]._strobe();
        for (unsigned i = 0; i < 19; ++i)
            metadata[i].strobe();
        accepted.strobe();
        fault.strobe();
    }
};
