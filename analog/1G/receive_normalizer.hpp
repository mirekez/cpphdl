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

    unsigned combine_digits(unsigned a, unsigned b) {
        unsigned middle, upper, upper_plus, upper_minus, upper_word;
        // a and b are signed thirteen-bit partial products. The low nibble
        // passes through unchanged in a + 16*b. Above it, add eight bits and
        // select the upper five-bit correction in parallel with that sum.
        middle = (b & 255) + ((a >> 4) & 255);
        upper = (b >> 8) & 31;
        upper_plus = (upper + 1) & 31;
        upper_minus = (upper - 1) & 31;
        upper_word = ((a >> 12) & 1) ? ((middle >> 8) ? upper : upper_minus)
                                     : ((middle >> 8) ? upper_plus : upper);
        return (upper_word << 12) | ((middle & 255) << 4) | (a & 15);
    }

public:
    void _assign() {}
    void _work(bool reset) {
        unsigned remainder, carry, rounding_sum, integral_low, integral_high;
        unsigned upper_plus, upper_minus, upper_word, low_negative;
        unsigned quotient_upper, rounding_carry;
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
        low._next = combine_digits(unsigned(partial[0]), unsigned(partial[1]));
        high._next = combine_digits(unsigned(partial[2]), unsigned(partial[3]));
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
        remainder = unsigned(fraction);
        // Floor quotient plus a carry rounds exact half-counts away from zero
        // without a 25-bit absolute-value or negation datapath.
        carry = unsigned(remainder > 128 || (remainder == 128 && (unsigned(integral) >> 16) == 0));
        // Only the low byte needs addition. Its carry and the signed upper
        // word determine clipping, avoiding a wide rounding carry chain.
        rounding_sum = (unsigned(integral) & 255) + (128 | carry);
        quotient_upper = unsigned(integral) >> 8;
        // Adding 128 plus the rounding bit overflows the low byte exactly
        // when its high bit is set, or when 127 receives the rounding bit.
        // Decode this carry directly so clipping avoids the byte adder and
        // signed upper-word arithmetic on its critical path.
        rounding_carry = ((unsigned(integral) >> 7) & 1) ||
                         ((unsigned(integral) & 127) == 127 && carry);
        rounded._next = rounding_sum & 255;
        rounded_low._next = (quotient_upper & 256) && (quotient_upper != 511 || !rounding_carry);
        rounded_high._next = !(quotient_upper & 256) && (quotient_upper != 0 || rounding_carry);
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
