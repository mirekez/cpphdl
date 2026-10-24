#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Throughput-oriented implementation of the 24-tap receive filter interface.
// Six cycles from accepted sample to output, ten to coefficient update.
// Delayed NLMS uses a smaller 1/16 step to keep its feedback loop stable.
class PipelinedReceiveFilter : public Module {
public:
    static constexpr unsigned OUTPUT_LATENCY = 6;
    static constexpr unsigned UPDATE_LATENCY = 10;

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
    reg<u<18>> weights[24]{};
    // ADC features need eight bits; local transmit history remains PAM5.
    // Retain its three-bit code through the delay line and decode at use.
    reg<u<8>> history[4]{}, features[7][4]{};
    reg<u<3>> local_history[20]{}, local_features[7][20]{};
    reg<u<17>> product_low[24]{}, product_high[24]{};
    reg<u<26>> products[24]{};
    reg<u<28>> sum4[6]{};
    reg<u<29>> sum8[3]{};
    reg<u<31>> total{};
    reg<u<16>> squares[24]{};
    reg<u<11>> correction_low[24]{}, correction_high[24]{};
    reg<u<24>> product_negative{};
    reg<u<26>> correction_magnitudes[24]{};
    reg<u<18>> adjustment_magnitudes[24]{};
    reg<u<24>> correction_negative{}, adjustment_negative{};
    reg<u<18>> energy4[6]{};
    reg<u<19>> energy8[3]{};
    reg<u<20>> energy{}, energy_delay{};
    reg<u<3>> targets[6]{};
    reg<u<10>> accepted{}, training{};
    reg<u<9>> error_reg{};
    reg<u<5>> exponent{}, correction_exponent{}, adjustment_exponent{};
    reg<u<8>> output_reg{};
    reg<u1> valid_reg{}, fault_reg{};

    int signed_value(unsigned value, unsigned sign) {
        return int(value ^ sign) - int(sign);
    }

public:
    void _assign() {
        for (unsigned i = 0; i < 24; ++i)
            coefficient_out[i] = _ASSIGN_I(weights[i]);
    }

    void _work(bool reset) {
        int current[24], stage0[24], stage6[24];
        int a, b, c, d, value, error, product, delta, next;
        unsigned code, invalid, shift, magnitude;
        u<20> candidate;
        u<8> absolute_error, absolute_feature;
        u<9> weight_low, weight_high;
        u<17> magnitude_low, magnitude_high;
        a = 0;
        b = 0;
        c = 0;
        d = 0;
        value = 0;
        error = 0;
        product = 0;
        delta = 0;
        next = 0;
        code = 0;
        invalid = 0;
        shift = 0;
        magnitude = 0;
        candidate = 0;
        absolute_error = 0;
        absolute_feature = 0;
        for (unsigned i = 0; i < 24; ++i)
            current[i] = 0;
        current[0] = int(unsigned(sample_in())) - 128;
        for (unsigned i = 1; i < 4; ++i)
            current[i] = signed_value(unsigned(history[i - 1]), 128);
        for (unsigned lane = 0; lane < 4; ++lane) {
            code = (unsigned(local_in()) >> (lane * 3)) & 7;
            invalid = invalid | unsigned(code > 4);
            current[4 + lane * 5] = (2 - int(code)) * 32;
            for (unsigned tap = 1; tap < 5; ++tap)
                current[4 + lane * 5 + tap] = (2 - int(unsigned(local_history[lane * 5 + tap - 1]))) * 32;
        }
        invalid = unsigned(advance_in()) & (invalid | unsigned(train_in() && desired_in() > 4));
        accepted._next = (accepted << 1) | unsigned(advance_in());
        training._next = (training << 1) | unsigned(advance_in() && train_in());
        targets[0]._next = desired_in();
        for (unsigned stage = 1; stage < 6; ++stage)
            targets[stage]._next = targets[stage - 1];
        if (advance_in() && !invalid && !fault_reg) {
            for (unsigned i = 0; i < 4; ++i) {
                history[i]._next = current[i];
                features[0][i]._next = current[i];
            }
            for (unsigned lane = 0; lane < 4; ++lane) {
                code = (unsigned(local_in()) >> (lane * 3)) & 7;
                local_history[lane * 5]._next = code;
                local_features[0][lane * 5]._next = code;
                for (unsigned tap = 1; tap < 5; ++tap) {
                    local_history[lane * 5 + tap]._next = local_history[lane * 5 + tap - 1];
                    local_features[0][lane * 5 + tap]._next = local_history[lane * 5 + tap - 1];
                }
            }
        }
        for (unsigned stage = 1; stage < 7; ++stage) {
            for (unsigned i = 0; i < 4; ++i)
                features[stage][i]._next = features[stage - 1][i];
            for (unsigned i = 0; i < 20; ++i)
                local_features[stage][i]._next = local_features[stage - 1][i];
        }
        for (unsigned i = 0; i < 4; ++i) {
            stage0[i] = signed_value(unsigned(features[0][i]), 128);
            stage6[i] = signed_value(unsigned(features[6][i]), 128);
        }
        for (unsigned i = 0; i < 20; ++i) {
            stage0[i + 4] = (2 - int(unsigned(local_features[0][i]))) * 32;
            stage6[i + 4] = (2 - int(unsigned(local_features[6][i]))) * 32;
        }

        // Stages 1..2: split each signed 18x8 weight product into unsigned
        // low-nine and signed high-nine parts. The weights visible one cycle
        // after acceptance define the complete product, before any update.
        for (unsigned i = 0; i < 24; ++i) {
            value = stage0[i];
            // Bound both multipliers to unsigned 9x8 magnitudes. Restore
            // signs at the existing register boundary, preserving all bits
            // while avoiding wide signed multiplier cones in cell mapping.
            absolute_feature = unsigned(value < 0 ? -value : value);
            weight_low = unsigned(weights[i]) & 511;
            a = signed_value(unsigned(weights[i]) >> 9, 256);
            weight_high = unsigned(a < 0 ? -a : a);
            if (i < 4) {
                magnitude_low = unsigned(weight_low) * unsigned(absolute_feature);
                magnitude_high = unsigned(weight_high) * unsigned(absolute_feature);
                squares[i]._next = unsigned(absolute_feature) * unsigned(absolute_feature);
            } else {
                // Local magnitudes are exactly 0, 32 or 64. Shifts preserve
                // every product bit without a general-purpose multiplier.
                code = unsigned(local_features[0][i - 4]);
                magnitude_low = code == 0 || code == 4 ? unsigned(weight_low) << 6 :
                                code == 1 || code == 3 ? unsigned(weight_low) << 5 : 0;
                magnitude_high = code == 0 || code == 4 ? unsigned(weight_high) << 6 :
                                 code == 1 || code == 3 ? unsigned(weight_high) << 5 : 0;
                squares[i]._next = code == 0 || code == 4 ? 4096 : code == 1 || code == 3 ? 1024 : 0;
            }
            product_low[i]._next = value < 0 ? -unsigned(magnitude_low) : unsigned(magnitude_low);
            product_high[i]._next = (a < 0) != (value < 0) ? -unsigned(magnitude_high) : unsigned(magnitude_high);
            products[i]._next =
                signed_value(unsigned(product_low[i]), 65536) + signed_value(unsigned(product_high[i]), 65536) * 512;
        }
        // Stages 3..5: a balanced reduction with explicit register boundaries.
        for (unsigned group = 0; group < 6; ++group) {
            a = signed_value(unsigned(products[group * 4]), 33554432);
            b = signed_value(unsigned(products[group * 4 + 1]), 33554432);
            c = signed_value(unsigned(products[group * 4 + 2]), 33554432);
            d = signed_value(unsigned(products[group * 4 + 3]), 33554432);
            sum4[group]._next = (a + b) + (c + d);
            energy4[group]._next =
                (squares[group * 4] + squares[group * 4 + 1]) + (squares[group * 4 + 2] + squares[group * 4 + 3]);
        }
        for (unsigned group = 0; group < 3; ++group) {
            sum8[group]._next = signed_value(unsigned(sum4[group * 2]), 134217728) +
                                signed_value(unsigned(sum4[group * 2 + 1]), 134217728);
            energy8[group]._next = energy4[group * 2] + energy4[group * 2 + 1];
        }
        total._next = (signed_value(unsigned(sum8[0]), 268435456) + signed_value(unsigned(sum8[1]), 268435456)) +
                      signed_value(unsigned(sum8[2]), 268435456);
        energy._next = (energy8[0] + energy8[1]) + energy8[2];
        energy_delay._next = energy;

        // Stage 6: quantized output and a target aligned to this observation.
        value = signed_value(unsigned(total), 1073741824);
        value = value < 0 ? -int(unsigned(-value) >> 14) : int(unsigned(value) >> 14);
        error = (int(unsigned(targets[5])) - 2) * 32 - value;
        error_reg._next = error < -255 ? -255 : error > 255 ? 255 : error;
        for (unsigned bit = 0; bit < 18; ++bit)
            if (energy_delay > (1u << bit))
                shift = bit + 1;
        exponent._next = shift;
        valid_reg._next = 0;
        if ((accepted & 32) && !invalid && !fault_reg) {
            output_reg._next = value < -128 ? 0 : value > 127 ? 255 : value + 128;
            valid_reg._next = 1;
        }

        // Stages 7..10: two 4x8 products, add/scale their magnitudes, shift,
        // then update current weights. Splitting the product bounds the
        // combinational depth driven by the shared training-error bits.
        error = signed_value(unsigned(error_reg), 256);
        absolute_error = unsigned(error < 0 ? -error : error);
        product_negative._next = 0;
        for (unsigned i = 0; i < 24; ++i) {
            value = stage6[i];
            absolute_feature = unsigned(value < 0 ? -value : value);
            if (i < 4) {
                correction_low[i]._next = (absolute_error & 15) * absolute_feature;
                correction_high[i]._next = (absolute_error >> 4) * absolute_feature;
            } else {
                code = unsigned(local_features[6][i - 4]);
                correction_low[i]._next = code == 0 || code == 4 ? (unsigned(absolute_error) & 15) << 6 :
                                         code == 1 || code == 3 ? (unsigned(absolute_error) & 15) << 5 : 0;
                correction_high[i]._next = code == 0 || code == 4 ? (unsigned(absolute_error) >> 4) << 6 :
                                          code == 1 || code == 3 ? (unsigned(absolute_error) >> 4) << 5 : 0;
            }
            if ((error < 0) != (value < 0))
                product_negative._next = product_negative._next | (u<24>(1) << i);
        }
        correction_exponent._next = exponent;
        correction_negative._next = product_negative;
        for (unsigned i = 0; i < 24; ++i) {
            correction_magnitudes[i]._next = (unsigned(correction_low[i]) + (unsigned(correction_high[i]) << 4)) * 1024;
        }
        adjustment_exponent._next = correction_exponent;
        adjustment_negative._next = correction_negative;
        for (unsigned i = 0; i < 24; ++i) {
            // E >= x[i]^2 gives |1024 * e * x[i] / E| <= 261120.
            // E=1 and x[i]=0 gives zero. Eighteen magnitude bits suffice.
            adjustment_magnitudes[i]._next = unsigned(correction_magnitudes[i]) >> unsigned(adjustment_exponent);
        }
        if ((training & 512) && !invalid && !fault_reg) {
            for (unsigned i = 0; i < 24; ++i) {
                delta = int(unsigned(adjustment_magnitudes[i]));
                if ((adjustment_negative >> i) & 1)
                    delta = -delta;
                // The bounded weight and correction sum fits signed 20 bits.
                candidate = signed_value(unsigned(weights[i]), 131072) + delta;
                next = signed_value(unsigned(candidate), 524288);
                weights[i]._next = next < -131071 ? -131071 : next > 131071 ? 131071 : next;
            }
        }
        if (invalid || fault_reg) {
            fault_reg._next = 1;
            accepted._next = 0;
            training._next = 0;
            output_reg._next = 128;
            valid_reg._next = 0;
        }
        if (reset) {
            for (unsigned i = 0; i < 4; ++i)
                history[i]._next = 0;
            for (unsigned i = 0; i < 20; ++i)
                local_history[i]._next = 2;
            for (unsigned i = 0; i < 24; ++i) {
                weights[i]._next = i == 0 ? 21845 : i == 1 ? -7282 : i == 2 ? 2427 : i == 3 ? -809 : 0;
            }
            accepted._next = 0;
            training._next = 0;
            output_reg._next = 128;
            valid_reg._next = 0;
            fault_reg._next = 0;
        }
    }

    void _strobe() {
        for (unsigned i = 0; i < 24; ++i) {
            weights[i].strobe();
            products[i].strobe();
            product_low[i].strobe();
            product_high[i].strobe();
            squares[i].strobe();
            correction_low[i].strobe();
            correction_high[i].strobe();
            correction_magnitudes[i].strobe();
            adjustment_magnitudes[i].strobe();
        }
        for (unsigned i = 0; i < 4; ++i)
            history[i].strobe();
        for (unsigned i = 0; i < 20; ++i)
            local_history[i].strobe();
        for (unsigned stage = 0; stage < 7; ++stage) {
            for (unsigned i = 0; i < 4; ++i)
                features[stage][i].strobe();
            for (unsigned i = 0; i < 20; ++i)
                local_features[stage][i].strobe();
        }
        for (unsigned i = 0; i < 6; ++i) {
            sum4[i].strobe();
            energy4[i].strobe();
        }
        for (unsigned i = 0; i < 3; ++i) {
            sum8[i].strobe();
            energy8[i].strobe();
        }
        for (unsigned i = 0; i < 6; ++i)
            targets[i].strobe();
        total.strobe();
        energy.strobe();
        energy_delay.strobe();
        accepted.strobe();
        training.strobe();
        error_reg.strobe();
        exponent.strobe();
        correction_exponent.strobe();
        adjustment_exponent.strobe();
        correction_negative.strobe();
        product_negative.strobe();
        adjustment_negative.strobe();
        output_reg.strobe();
        valid_reg.strobe();
        fault_reg.strobe();
    }
};
