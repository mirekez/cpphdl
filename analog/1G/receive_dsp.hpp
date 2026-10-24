#pragma once
#include "receive_filter_datapath.hpp"

// Four physical receive lanes share the actual four local transmit references.
// Input transactions contain recovered-symbol observations, not half-symbol ADC
// samples. All ports belong to this module's clock domain.
class ReceiveDSP : public Module {
public:
    static constexpr unsigned OUTPUT_LATENCY = PipelinedReceiveFilter::OUTPUT_LATENCY;
    static constexpr unsigned UPDATE_LATENCY = PipelinedReceiveFilter::UPDATE_LATENCY;

    _PORT(u<32>) samples_in;
    _PORT(u<12>) local_in;
    _PORT(u<12>) desired_in;
    _PORT(u<16>) tag_in;
    _PORT(bool) clipped_in;
    _PORT(bool) train_in;
    _PORT(bool) valid_in;
    _PORT(bool) restart_in;
    _PORT(u<32>) samples_out = _ASSIGN(
        unsigned(lanes[0].sample_out()) | (unsigned(lanes[1].sample_out()) << 8) |
        (unsigned(lanes[2].sample_out()) << 16) | (unsigned(lanes[3].sample_out()) << 24));
    _PORT(u<16>) tag_out = _ASSIGN_REG(tags[OUTPUT_LATENCY]);
    _PORT(bool) valid_out = _ASSIGN(all_valid_comb_func() && !unavailable_comb_func());
    _PORT(bool) fault_out = _ASSIGN(fault || child_fault_comb_func() || inconsistent_valid_comb_func());
    _PORT(bool) clipped_out = _ASSIGN(clipped[OUTPUT_LATENCY] || output_at_rail_comb_func());
    // The final pending update has reached the weights when this deasserts.
    _PORT(bool) training_busy_out = _ASSIGN(
        !unavailable_comb_func() && (training != 0 || (valid_in() && train_in())));

private:
    ReceiveFilter lanes[4];
    reg<u<16>> tags[OUTPUT_LATENCY + 1]{};
    reg<u1> clipped[OUTPUT_LATENCY + 1]{};
    reg<u<UPDATE_LATENCY>> training{};
    reg<u1> fault{};

    _LAZY_COMB(all_valid_comb, bool)
        return all_valid_comb = lanes[0].valid_out() && lanes[1].valid_out() &&
               lanes[2].valid_out() && lanes[3].valid_out();
    }
    _LAZY_COMB(child_fault_comb, bool)
        return child_fault_comb = lanes[0].fault_out() || lanes[1].fault_out() ||
               lanes[2].fault_out() || lanes[3].fault_out();
    }
    _LAZY_COMB(inconsistent_valid_comb, bool)
        return inconsistent_valid_comb = lanes[0].valid_out() != lanes[1].valid_out() ||
               lanes[0].valid_out() != lanes[2].valid_out() ||
               lanes[0].valid_out() != lanes[3].valid_out();
    }
    _LAZY_COMB(illegal_input_comb, bool)
        bool bad = false;
        for (unsigned lane = 0; lane < 4; ++lane) {
            bad = bad || ((unsigned(local_in()) >> (3 * lane)) & 7) > 4;
            if (train_in())
                bad = bad || ((unsigned(desired_in()) >> (3 * lane)) & 7) > 4;
        }
        return illegal_input_comb = valid_in() && bad;
    }
    _LAZY_COMB(unavailable_comb, bool)
        return unavailable_comb = restart_in() || fault || child_fault_comb_func() || inconsistent_valid_comb_func() || illegal_input_comb_func();
    }
    _LAZY_COMB(output_at_rail_comb, bool)
        bool rail = false;
        for (unsigned lane = 0; lane < 4; ++lane)
            rail = rail || lanes[lane].sample_out() == 0 || lanes[lane].sample_out() == 255;
        return output_at_rail_comb = all_valid_comb_func() && rail;
    }

public:
    void _assign() {
        for (unsigned i = 0; i < 4; ++i) {
            lanes[i].sample_in = _ASSIGN_I((samples_in() >> (8 * i)) & 255);
            lanes[i].local_in = _ASSIGN(local_in());
            lanes[i].desired_in = _ASSIGN_I((desired_in() >> (3 * i)) & 7);
            lanes[i].train_in = _ASSIGN(train_in());
            lanes[i].advance_in = _ASSIGN(valid_in() && !unavailable_comb_func());
            lanes[i]._assign();
        }
    }
    void _work(bool reset) {
        bool stop = unavailable_comb_func();
        tags[0]._next = tag_in();
        clipped[0]._next = valid_in() && clipped_in();
        for (unsigned stage = 1; stage <= OUTPUT_LATENCY; ++stage) {
            tags[stage]._next = tags[stage - 1];
            clipped[stage]._next = clipped[stage - 1];
        }
        training._next = (training << 1) | unsigned(valid_in() && train_in());
        if (stop) {
            fault._next = 1;
            training._next = 0;
        }
        for (unsigned lane = 0; lane < 4; ++lane)
            lanes[lane]._work(reset || unavailable_comb_func());
        if (reset || restart_in()) {
            fault._next = 0;
            training._next = 0;
            for (unsigned stage = 0; stage <= OUTPUT_LATENCY; ++stage) {
                tags[stage]._next = 0;
                clipped[stage]._next = 0;
            }
        }
    }
    void _strobe() {
        for (unsigned lane = 0; lane < 4; ++lane)
            lanes[lane]._strobe();
        for (unsigned stage = 0; stage <= OUTPUT_LATENCY; ++stage) {
            tags[stage].strobe();
            clipped[stage].strobe();
        }
        training.strobe();
        fault.strobe();
    }
};
