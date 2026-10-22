#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Estimate normalization from balanced, known PAM5 training observations.
// Samples are achieved ADC codes; training labels are supplied by acquisition.
class ReceiveGainCalibration : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) start_in;
    _PORT(bool) valid_in;
    _PORT(bool) finish_in;
    _PORT(u<8>) sample_in;
    _PORT(u<3>) level_in; // 0..4 labels PAM levels -2..+2.
    _PORT(u<8>) offset_out = _ASSIGN_REG(offset);
    _PORT(u<16>) gain_out = _ASSIGN_REG(gain);
    _PORT(bool) ready_out = _ASSIGN_REG(ready);
    _PORT(bool) fault_out = _ASSIGN_REG(fault);
    _PORT(bool) busy_out = _ASSIGN(phase != 0);
    _PORT(bool) collecting_out = _ASSIGN(phase == 1);

private:
    reg<u<24>> sums[5]{}, span{};
    reg<u<16>> counts[5]{}, population{}, gain{};
    reg<u<8>> offset{}, pending_offset{};
    reg<u<32>> numerator{}, quotient{};
    reg<u<25>> remainder{}, denominator{};
    reg<u<6>> remaining{};
    reg<u<2>> phase{};
    reg<u1> ready{}, fault{};

public:
    void _assign() {}
    void _work(bool reset) {
        unsigned index, next_remainder, next_quotient;
        bool balanced;
        index = unsigned(level_in());
        next_remainder = 0;
        next_quotient = 0;
        balanced = counts[0] != 0;
        if (phase == 1) {
            if (finish_in()) {
                for (unsigned i = 1; i < 5; ++i)
                    balanced = balanced && counts[i] == counts[0] && sums[i] > sums[i - 1];
                // Finish describes the preceding collected set. Requiring a
                // separate cycle avoids silently omitting its final sample.
                if (!balanced || valid_in()) {
                    fault._next = 1;
                    phase._next = 0;
                } else {
                    population._next = counts[0];
                    span._next = sums[4] - sums[0];
                    numerator._next = unsigned(sums[2]) + (unsigned(counts[0]) >> 1);
                    denominator._next = counts[0];
                    quotient._next = 0;
                    remainder._next = 0;
                    remaining._next = 32;
                    phase._next = 2;
                }
            } else if (valid_in()) {
                if (index > 4 || (index <= 4 && counts[index] == 65535)) {
                    fault._next = 1;
                    phase._next = 0;
                } else {
                    counts[index]._next = counts[index] + 1;
                    sums[index]._next = unsigned(sums[index]) + unsigned(sample_in());
                }
            }
        } else if (phase == 2 || phase == 3) {
            // One restoring division bit per clock; no combinational divider.
            next_remainder = (unsigned(remainder) << 1) | (unsigned(numerator) >> 31);
            next_quotient = unsigned(quotient) << 1;
            if (next_remainder >= unsigned(denominator)) {
                next_remainder -= unsigned(denominator);
                next_quotient |= 1;
            }
            numerator._next = numerator << 1;
            remainder._next = next_remainder;
            quotient._next = next_quotient;
            remaining._next = remaining - 1;
            if (remaining == 1) {
                if (phase == 2) {
                    pending_offset._next = next_quotient;
                    // Four PAM spacings span 128 normalized counts.
                    numerator._next = (unsigned(population) << 15) + (unsigned(span) >> 1);
                    denominator._next = span;
                    quotient._next = 0;
                    remainder._next = 0;
                    remaining._next = 32;
                    phase._next = 3;
                } else {
                    phase._next = 0;
                    if (next_quotient == 0 || next_quotient > 65535) {
                        fault._next = 1;
                    } else {
                        offset._next = pending_offset;
                        gain._next = next_quotient;
                        ready._next = 1;
                    }
                }
            }
        }
        if (start_in()) {
            for (unsigned i = 0; i < 5; ++i) {
                sums[i]._next = 0;
                counts[i]._next = 0;
            }
            phase._next = 1;
            ready._next = 0;
            fault._next = 0;
        }
        if (reset || !enable_in()) {
            phase._next = 0;
            ready._next = 0;
            fault._next = 0;
            offset._next = 128;
            gain._next = 256;
        }
    }
    void _strobe() {
        for (unsigned i = 0; i < 5; ++i) {
            sums[i].strobe();
            counts[i].strobe();
        }
        span.strobe();
        population.strobe();
        gain.strobe();
        offset.strobe();
        pending_offset.strobe();
        numerator.strobe();
        quotient.strobe();
        remainder.strobe();
        denominator.strobe();
        remaining.strobe();
        phase.strobe();
        ready.strobe();
        fault.strobe();
    }
};
