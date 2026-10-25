#pragma once
#include "receive_timing.hpp"

// Eight contiguous, tagged 250 MS/s samples support three four-tap cubic
// interpolations. One triplet is produced per symbol, not per half-symbol,
// so a slightly faster remote source never needs two outputs in one clock.
class ReceiveSampleInterpolator : public Module {
public:
    _PORT(u<32>) samples_in;
    _PORT(u<16>) tag_in;
    _PORT(bool) valid_in;
    _PORT(u<25>) interval_in;
    _PORT(u<25>) period_in;
    _PORT(u<25>) initial_phase_in;
    _PORT(u<16>) origin_in;
    _PORT(u<32>) samples_out = _ASSIGN_REG(output);
    _PORT(u<8>) early_out = _ASSIGN_REG(early);
    _PORT(u<8>) late_out = _ASSIGN_REG(late);
    _PORT(u<16>) tag_out = _ASSIGN_REG(output_tag);
    _PORT(u<24>) fraction_out = _ASSIGN_REG(output_fraction);
    _PORT(bool) valid_out = _ASSIGN_REG(valid);
    _PORT(bool) fault_out = _ASSIGN_REG(fault);
    _PORT(bool) clipped_out = _ASSIGN_REG(clipped);

private:
    reg<u<32>> history[7]{};
    reg<u<16>> expected_tag{}, center_tag{}, tags[4]{}, output_tag{};
    reg<u<24>> center_fraction{}, fractions[4]{}, output_fraction{};
    reg<u<4>> filled{}, accepted{};
    reg<u<8>> fraction[3]{}, fraction_delay[3]{};
    reg<u<13>> squared[3]{}, squared_delay[3]{}, cubed[3]{};
    reg<u<8>> data[6][4]{}, data_delay[6][4]{};
    reg<u<24>> products[6][4]{};
    reg<u<26>> sums[6]{};
    reg<u<32>> output{};
    reg<u<8>> early{}, late{};
    reg<u1> valid{}, fault{}, clipped{};

    int signed_value(unsigned value, unsigned sign) {
        return int(value ^ sign) - int(sign);
    }

public:
    void _work(bool reset) {
        unsigned words[8], fraction_code[3], value, packed, bad, request, total;
        int location[3], relative, offset, base, coefficients[12], sum, rounded;
        bad = 0;
        request = 0;
        packed = 0;
        total = 0;
        value = 0;
        relative = 0;
        offset = 0;
        base = 0;
        sum = 0;
        rounded = 0;
        for (unsigned i = 0; i < 7; ++i)
            words[i] = unsigned(history[6 - i]);
        words[7] = unsigned(samples_in());
        for (unsigned phase = 0; phase < 3; ++phase) {
            fraction_code[phase] = 0;
            location[phase] = 0;
            // Shared Q12 basis for early, middle (all four lanes), and late.
            coefficients[4 * phase + 0] = -int(unsigned(fraction_delay[phase]) << 4) +
                                          2 * int(unsigned(squared_delay[phase])) - int(unsigned(cubed[phase]));
            coefficients[4 * phase + 1] =
                8192 - 5 * int(unsigned(squared_delay[phase])) + 3 * int(unsigned(cubed[phase]));
            coefficients[4 * phase + 2] = int(unsigned(fraction_delay[phase]) << 4) +
                                          4 * int(unsigned(squared_delay[phase])) - 3 * int(unsigned(cubed[phase]));
            coefficients[4 * phase + 3] = -int(unsigned(squared_delay[phase])) + int(unsigned(cubed[phase]));
            fraction_delay[phase]._next = fraction[phase];
            squared_delay[phase]._next = squared[phase];
            cubed[phase]._next = (unsigned(squared[phase]) * unsigned(fraction[phase]) + 128) >> 8;
        }
        for (unsigned path = 0; path < 6; ++path) {
            sum = 0;
            for (unsigned tap = 0; tap < 4; ++tap) {
                data_delay[path][tap]._next = data[path][tap];
                products[path][tap]._next = int(unsigned(data_delay[path][tap])) * coefficients[4 * (path == 0   ? 0
                                                                                                     : path == 5 ? 2
                                                                                                                 : 1) +
                                                                                                tap];
                sum += signed_value(unsigned(products[path][tap]), 8388608);
            }
            sums[path]._next = sum;
        }
        for (unsigned i = 1; i < 4; ++i) {
            tags[i]._next = tags[i - 1];
            fractions[i]._next = fractions[i - 1];
        }
        valid._next = 0;
        clipped._next = 0;
        if ((accepted & 8) && !fault) {
            for (unsigned path = 0; path < 6; ++path) {
                sum = signed_value(unsigned(sums[path]), 33554432);
                rounded = sum < 0 ? -int((unsigned(-sum) + 4096) >> 13) : int((unsigned(sum) + 4096) >> 13);
                value = rounded < 0 ? 0 : rounded > 255 ? 255 : unsigned(rounded);
                if (rounded < 0 || rounded > 255)
                    clipped._next = 1;
                if (path == 0)
                    early._next = value;
                else if (path == 5)
                    late._next = value;
                else
                    packed |= value << (8 * (path - 1));
            }
            output._next = packed;
            output_tag._next = tags[3];
            output_fraction._next = fractions[3];
            valid._next = 1;
        }
        if (valid_in() && !fault) {
            bad = unsigned(tag_in() != expected_tag || period_in() < 16743662 || period_in() > 16810770 ||
                           interval_in() < 16088302 || interval_in() > 17466130);
            if (!bad) {
                expected_tag._next = expected_tag + 1;
                history[0]._next = samples_in();
                for (unsigned i = 1; i < 7; ++i)
                    history[i]._next = history[i - 1];
                if (filled < 8)
                    filled._next = filled + 1;
                if (filled >= 7 && u<16>(tag_in() - center_tag) == 4) {
                    for (unsigned phase = 0; phase < 3; ++phase) {
                        relative = int(unsigned(center_fraction)) + (phase == 0   ? -int(unsigned(period_in()))
                                                                     : phase == 2 ? int(unsigned(period_in()))
                                                                                  : 0);
                        offset = relative < 0 ? -int((unsigned(-relative) + 16777215) >> 24) : (relative >> 24);
                        fraction_code[phase] = unsigned(relative - offset * 16777216) >> 16;
                        location[phase] = 2 + offset;
                        if (location[phase] < 0 || location[phase] > 4)
                            bad = 1;
                    }
                    if (!bad) {
                        request = 1;
                        for (unsigned phase = 0; phase < 3; ++phase) {
                            fraction[phase]._next = fraction_code[phase];
                            squared[phase]._next = (fraction_code[phase] * fraction_code[phase] + 8) >> 4;
                        }
                        for (unsigned path = 0; path < 6; ++path) {
                            base = location[path == 0 ? 0 : path == 5 ? 2 : 1];
                            for (unsigned tap = 0; tap < 4; ++tap)
                                data[path][tap]._next =
                                    (words[base + int(tap)] >> (8 * (path == 0 || path == 5 ? 0 : path - 1))) & 255;
                        }
                        tags[0]._next = center_tag;
                        fractions[0]._next = center_fraction;
                        total = unsigned(center_fraction) + 2 * unsigned(interval_in());
                        center_fraction._next = total;
                        center_tag._next = center_tag + (total >> 24);
                    }
                }
            }
            if (bad) {
                fault._next = 1;
                valid._next = 0;
            }
        }
        accepted._next = (accepted << 1) | request;
        if (bad || fault)
            accepted._next = 0;
        if (reset) {
            expected_tag._next = origin_in();
            center_tag._next = origin_in() + 4 + (initial_phase_in() >> 24);
            center_fraction._next = initial_phase_in();
            filled._next = 0;
            accepted._next = 0;
            valid._next = 0;
            fault._next = 0;
            clipped._next = 0;
            output._next = 0x80808080;
            early._next = 128;
            late._next = 128;
            output_tag._next = 0;
            output_fraction._next = 0;
        }
    }

    void _strobe() {
        for (unsigned i = 0; i < 7; ++i)
            history[i].strobe();
        expected_tag.strobe();
        center_tag.strobe();
        center_fraction.strobe();
        filled.strobe();
        accepted.strobe();
        valid.strobe();
        fault.strobe();
        clipped.strobe();
        output.strobe();
        early.strobe();
        late.strobe();
        output_tag.strobe();
        output_fraction.strobe();
        for (unsigned i = 0; i < 4; ++i) {
            tags[i].strobe();
            fractions[i].strobe();
        }
        for (unsigned i = 0; i < 3; ++i) {
            fraction[i].strobe();
            fraction_delay[i].strobe();
            squared[i].strobe();
            squared_delay[i].strobe();
            cubed[i].strobe();
        }
        for (unsigned path = 0; path < 6; ++path) {
            sums[path].strobe();
            for (unsigned tap = 0; tap < 4; ++tap) {
                data[path][tap].strobe();
                data_delay[path][tap].strobe();
                products[path][tap].strobe();
            }
        }
    }
};

// One reference-derived 250 MHz domain. Output is a tagged symbol-valid stream;
// existing PCSReceiver requires an explicit enable/clock-domain adapter.
class ReceiveSampleRecovery : public Module {
public:
    _PORT(u<32>) samples_in;
    _PORT(u<16>) tag_in;
    _PORT(bool) valid_in;
    _PORT(bool) track_in;
    _PORT(bool) restart_in;
    _PORT(u<25>) initial_phase_in;
    _PORT(u<16>) origin_in;
    _PORT(u<32>) samples_out = _ASSIGN(interpolator.samples_out());
    _PORT(u<16>) tag_out = _ASSIGN(interpolator.tag_out());
    _PORT(u<24>) fraction_out = _ASSIGN(interpolator.fraction_out());
    _PORT(bool) valid_out = _ASSIGN(interpolator.valid_out());
    _PORT(bool) fault_out = _ASSIGN(interpolator.fault_out());
    _PORT(bool) clipped_out = _ASSIGN(interpolator.clipped_out());
    _PORT(u<25>) interval_out = _ASSIGN(u<25>(applied_interval()));
    _PORT(u<25>) period_out = _ASSIGN(timing.period_out());
    _PORT(u<14>) error_out = _ASSIGN(timing.error_out());
    _PORT(bool) correction_valid_out = _ASSIGN(timing.valid_out());

private:
    ReceiveSampleInterpolator interpolator;
    ReceiveTimingLoop timing;
    unsigned applied_interval() {
        int correction;
        correction = int(unsigned(timing.interval_out())) - int(unsigned(timing.period_out()));
        correction = correction < 0 ? -int(unsigned(-correction) >> 2) : int(unsigned(correction) >> 2);
        return unsigned(int(unsigned(timing.period_out())) + correction);
    }

public:
    void _assign() {
        interpolator.samples_in = _ASSIGN(samples_in());
        interpolator.tag_in = _ASSIGN(tag_in());
        interpolator.valid_in = _ASSIGN(valid_in());
        interpolator.interval_in = _ASSIGN(u<25>(applied_interval()));
        interpolator.period_in = _ASSIGN(timing.period_out());
        interpolator.initial_phase_in = _ASSIGN(initial_phase_in());
        interpolator.origin_in = _ASSIGN(origin_in());
        timing.early_in = _ASSIGN(interpolator.early_out());
        timing.middle_in = _ASSIGN(u<8>(interpolator.samples_out()));
        timing.late_in = _ASSIGN(interpolator.late_out());
        timing.advance_in = _ASSIGN(interpolator.valid_out() && !interpolator.fault_out());
        timing.track_in = _ASSIGN(track_in());
        interpolator._assign();
        timing._assign();
    }
    void _work(bool reset) {
        interpolator._work(reset || restart_in());
        timing._work(reset || restart_in() || interpolator.fault_out());
    }
    void _strobe() {
        interpolator._strobe();
        timing._strobe();
    }
};
