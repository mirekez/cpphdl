#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Translate an ADC code into the PCS convention: zero is 128 and one PAM
// level is 32 counts. Calibration supplies the measured offset and Q8.8 gain.
// Eight register stages preserve one-sample-per-clock throughput.
class ReceiveSampleNormalizer : public Module {
public:
    _PORT(u<8>) sample_in;
    _PORT(u<8>) offset_in;
    _PORT(u<16>) gain_in;
    _PORT(bool) valid_in;
    _PORT(bool) enable_in;
    _PORT(u<8>) sample_out = _ASSIGN_REG(sample);
    _PORT(bool) valid_out = _ASSIGN_REG(valid);
    _PORT(bool) clipped_out = _ASSIGN_REG(clipped);

private:
    reg<u<8>> raw_sample{}, raw_offset{};
    reg<u<16>> raw_gain{};
    reg<u<9>> centered{};
    reg<u<16>> calibration_gain{};
    reg<u<11>> half_products[8]{};
    reg<u<13>> partial[4]{};
    reg<u<17>> low{}, high{}, integral{};
    reg<u<8>> fraction{};
    reg<u<8>> rounded{};
    reg<u1> rounded_low{}, rounded_high{};
    reg<u<7>> accepted{};
    reg<u<8>> sample{};
    reg<u1> valid{}, clipped{};

    int signed_value(unsigned value, unsigned sign) {
        return int(value ^ sign) - int(sign);
    }

public:
    void _assign() {}
    void _work(bool reset) {
        int value, high_value;
        unsigned remainder, carry, rounding_sum, integral_low, integral_high;
        unsigned upper_plus, upper_minus, upper_word, low_negative;
        // Isolate external-input hold repair from the centering arithmetic.
        // Calibration travels through the same register boundary as its sample.
        raw_sample._next = sample_in();
        raw_offset._next = offset_in();
        raw_gain._next = gain_in();
        centered._next = int(unsigned(raw_sample)) - int(unsigned(raw_offset));
        calibration_gain._next = raw_gain;
        // Register signed 9x2 products before recombining each four-bit digit.
        // This bounds multiplier depth at the routed 250 MHz boundary.
        for (unsigned i = 0; i < 8; ++i)
            half_products[i]._next = signed_value(unsigned(centered), 256) *
                int((unsigned(calibration_gain) >> (2 * i)) & 3);
        for (unsigned i = 0; i < 4; ++i)
            partial[i]._next = signed_value(unsigned(half_products[2 * i]), 1024) +
                signed_value(unsigned(half_products[2 * i + 1]), 1024) * 4;
        low._next = signed_value(unsigned(partial[0]), 4096) +
            signed_value(unsigned(partial[1]), 4096) * 16;
        high._next = signed_value(unsigned(partial[2]), 4096) +
            signed_value(unsigned(partial[3]), 4096) * 16;
        // The sign-extended nine-bit addend changes the upper word by at
        // most one. Compute its alternatives alongside the low-byte sum,
        // avoiding carry propagation through all seventeen bits.
        integral_low = (unsigned(high) & 255) + ((unsigned(low) >> 8) & 255);
        integral_high = unsigned(high) >> 8;
        upper_plus = (integral_high + 1) & 511;
        upper_minus = (integral_high - 1) & 511;
        low_negative = (unsigned(low) >> 16) & 1;
        upper_word = low_negative ? ((integral_low >> 8) ? integral_high : upper_minus)
                                 : ((integral_low >> 8) ? upper_plus : integral_high);
        integral._next = (upper_word << 8) | (integral_low & 255);
        fraction._next = unsigned(low) & 255;
        value = signed_value(unsigned(integral), 65536);
        remainder = unsigned(fraction);
        // Floor quotient plus a carry rounds exact half-counts away from zero
        // without a 25-bit absolute-value or negation datapath.
        carry = unsigned(remainder > 128 || (remainder == 128 && value >= 0));
        // Only the low byte needs addition. Its carry and the signed upper
        // word determine clipping, avoiding a wide rounding carry chain.
        rounding_sum = (unsigned(integral) & 255) + (128 | carry);
        high_value = signed_value(unsigned(integral) >> 8, 256);
        rounded._next = rounding_sum & 255;
        rounded_low._next = high_value < -1 || (high_value == -1 && (rounding_sum >> 8) == 0);
        rounded_high._next = high_value > 0 || (high_value == 0 && (rounding_sum >> 8) != 0);
        accepted._next = (accepted << 1) | unsigned(valid_in());
        valid._next = (accepted >> 6) & 1;
        clipped._next = 0;
        if ((accepted >> 6) & 1) {
            sample._next = rounded_low ? 0 : rounded_high ? 255 : unsigned(rounded);
            clipped._next = rounded_low || rounded_high;
        }
        if (reset || !enable_in()) {
            accepted._next = 0;
            sample._next = 128;
            valid._next = 0;
            clipped._next = 0;
        }
    }
    void _strobe() {
        raw_sample.strobe();
        raw_offset.strobe();
        raw_gain.strobe();
        centered.strobe();
        calibration_gain.strobe();
        for (unsigned i = 0; i < 8; ++i)
            half_products[i].strobe();
        for (unsigned i = 0; i < 4; ++i)
            partial[i].strobe();
        low.strobe();
        high.strobe();
        integral.strobe();
        fraction.strobe();
        rounded.strobe();
        rounded_low.strobe();
        rounded_high.strobe();
        accepted.strobe();
        sample.strobe();
        valid.strobe();
        clipped.strobe();
    }
};
