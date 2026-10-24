#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// One lane of a four-pair receiver: four receive FFE taps and five echo/NEXT
// taps for each of the four actual local transmit streams. All arithmetic is
// fixed point. The PMA supplies offset-binary samples (zero = 128, level = 32).
// Training symbols are accepted only when train_in is explicitly asserted.
class ReceiveAdaptiveFilter : public Module {
public:
    _PORT(u<8>) sample_in;
    _PORT(u<12>) local_in;
    _PORT(u<3>) desired_in;
    _PORT(bool) train_in;
    _PORT(bool) advance_in;
    _PORT(u<8>) sample_out = _ASSIGN_REG(output_reg);
    _PORT(bool) valid_out = _ASSIGN_REG(valid_reg);
    _PORT(bool) fault_out = _ASSIGN_REG(fault_reg);
    _PORT(u<18>) coefficient_out[24];

private:
    // Coefficients are signed 18-bit Q14 values in two's-complement storage.
    reg<u<18>> weights[24]{};
    reg<u<8>> history[24]{};
    reg<u<8>> output_reg{};
    reg<u1> valid_reg{}, fault_reg{};

public:
    void _assign() {
        for (unsigned i = 0; i < 24; ++i)
            coefficient_out[i] = _ASSIGN_I(weights[i]);
    }

    void _work(bool reset) {
        int feature[24];
        int coefficient, total, filtered, error, product, delta, next_weight, code;
        unsigned energy, shift, magnitude, invalid;
        // Explicitly initialize temporaries: converted work methods are tasks,
        // and old synthesis tools may otherwise infer retained task variables.
        total = 0;
        filtered = 0;
        error = 0;
        product = 0;
        delta = 0;
        next_weight = 0;
        coefficient = 0;
        code = 0;
        energy = 0;
        shift = 0;
        magnitude = 0;
        invalid = 0;
        for (unsigned i = 0; i < 24; ++i)
            feature[i] = 0;
        valid_reg._next = 0;
        if (advance_in() && !fault_reg) {
            feature[0] = int(unsigned(sample_in())) - 128;
            for (unsigned i = 1; i < 4; ++i)
                feature[i] = int(unsigned(history[i - 1]) ^ 128) - 128;
            for (unsigned lane = 0; lane < 4; ++lane) {
                code = (unsigned(local_in()) >> (lane * 3)) & 7;
                invalid = invalid | unsigned(code > 4);
                feature[4 + lane * 5] = (2 - code) * 32;
                for (unsigned tap = 1; tap < 5; ++tap)
                    feature[4 + lane * 5 + tap] = int(unsigned(history[3 + lane * 5 + tap]) ^ 128) - 128;
            }
            // The worst-case sum is below 2^29; every signed operation fits
            // int32. Multiplication precedes a single truncation toward zero.
            for (unsigned i = 0; i < 24; ++i) {
                coefficient = int(unsigned(weights[i]) ^ 131072) - 131072;
                total = total + coefficient * feature[i];
                energy = energy + unsigned(feature[i] * feature[i]);
                history[i]._next = feature[i];
            }
            // Explicit signed magnitude also preserves truncation in old
            // synthesis frontends that mis-lower signed division by 2^14.
            filtered = total < 0 ? -int(unsigned(-total) >> 14) : int(unsigned(total) >> 14);
            output_reg._next = filtered < -128 ? 0 : filtered > 127 ? 255 : filtered + 128;
            valid_reg._next = 1;
            if (train_in()) {
                invalid = invalid | unsigned(desired_in() > 4);
                error = (int(unsigned(desired_in())) - 2) * 32 - filtered;
                error = error < -255 ? -255 : error > 255 ? 255 : error;
                // Normalize by the next power of two at or above the feature
                // energy. This bounds the effective NLMS step to (0.5, 1].
                // It uses a priority encoder and shifts, without a divider.
                for (unsigned bit = 0; bit < 18; ++bit)
                    if (energy > (1u << bit))
                        shift = bit + 1;
                for (unsigned i = 0; i < 24; ++i) {
                    coefficient = int(unsigned(weights[i]) ^ 131072) - 131072;
                    product = error * feature[i] * 16384;
                    magnitude = unsigned(product < 0 ? -product : product);
                    delta = int(magnitude >> shift);
                    if (product < 0)
                        delta = -delta;
                    next_weight = coefficient + delta;
                    next_weight = next_weight < -131071 ? -131071 : next_weight > 131071 ? 131071 : next_weight;
                    weights[i]._next = next_weight;
                }
            }
            if (invalid) {
                // Invalid PAM codes are a protocol failure, not another level.
                output_reg._next = 128;
                valid_reg._next = 0;
                fault_reg._next = 1;
            }
        }
        if (reset) {
            for (unsigned i = 0; i < 24; ++i) {
                weights[i]._next = i == 0 ? 21845 : i == 1 ? -7282 : i == 2 ? 2427 : i == 3 ? -809 : 0;
                history[i]._next = 0;
            }
            output_reg._next = 128;
            valid_reg._next = 0;
            fault_reg._next = 0;
        }
    }

    void _strobe() {
        for (unsigned i = 0; i < 24; ++i) {
            weights[i].strobe();
            history[i].strobe();
        }
        output_reg.strobe();
        valid_reg.strobe();
        fault_reg.strobe();
    }
};
