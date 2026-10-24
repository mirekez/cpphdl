#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// ADC amplitude acquisition for five ordered analog gains. Selection changes
// only during training; the receiver calibrates its digital scale afterwards.
class ReceiveAnalogGainControl : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) restart_in;
    _PORT(bool) training_in;
    _PORT(bool) advance_in;
    _PORT(bool) adc_fault_in;
    _PORT(u<8>) sample_in;
    _PORT(u<8>) minimum_span_in;
    _PORT(u<8>) maximum_span_in;
    _PORT(u<5>) select_n_out;
    _PORT(u<3>) level_out;
    _PORT(bool) ready_out;
    _PORT(bool) changed_out;
    _PORT(bool) weak_out;
    _PORT(bool) overload_out;
    _PORT(bool) fault_out;
    _PORT(u<8>) span_out;
    _PORT(u<8>) centre_out;
    _PORT(bool) settled_out;

private:
    reg<u<2>> state{};
    reg<u<3>> level{};
    reg<u<5>> selection{};
    reg<u<12>> count{};
    reg<u<8>> minimum{}, maximum{}, measured_span{}, centre{}, window_minimum{}, window_maximum{};
    reg<u1> evaluate_span{}, evaluate_gain{};
    reg<u1> ready{}, changed{}, weak{}, overload{}, fault{};

public:
    void _assign() {
        select_n_out = _ASSIGN_REG(selection);
        level_out = _ASSIGN_REG(level);
        ready_out = _ASSIGN_REG(ready);
        changed_out = _ASSIGN_REG(changed);
        weak_out = _ASSIGN_REG(weak);
        overload_out = _ASSIGN_REG(overload);
        fault_out = _ASSIGN_REG(fault);
        span_out = _ASSIGN_REG(measured_span);
        centre_out = _ASSIGN_REG(centre);
        settled_out = _ASSIGN(state == 1 && !fault);
    }
    void _work(bool reset) {
        unsigned low, high, span, next_level, sample;
        bool invalid_config;
        sample = unsigned(sample_in());
        low = sample < unsigned(minimum) ? sample : unsigned(minimum);
        high = sample > unsigned(maximum) ? sample : unsigned(maximum);
        span = high - low;
        next_level = unsigned(level);
        // The widest gain step is 8 / 3.5. A narrower amplitude band can
        // oscillate between adjacent gains even with an ideal analog path.
        invalid_config = minimum_span_in() == 0 || minimum_span_in() >= maximum_span_in() ||
            unsigned(maximum_span_in()) * 7 < unsigned(minimum_span_in()) * 16;
        changed._next = 0;
        if (state == 0) {
            count._next = count + 1;
            if (count == 4095) {
                state._next = 1;
                count._next = 0;
                minimum._next = 255;
                maximum._next = 0;
            }
        } else if (state == 1 && training_in()) {
            // Capture extrema, compute their range, then decide. The sample
            // comparison and gain-selection arithmetic cross separate edges.
            evaluate_span._next = 0;
            evaluate_gain._next = evaluate_span;
            if (evaluate_span) {
                measured_span._next = unsigned(window_maximum) - unsigned(window_minimum);
                centre._next = (unsigned(window_minimum) + unsigned(window_maximum) + 1) >> 1;
            }
            if (advance_in()) {
                minimum._next = low;
                maximum._next = high;
                count._next = count + 1;
                if (count == 1023) {
                    window_minimum._next = low;
                    window_maximum._next = high;
                    evaluate_span._next = 1;
                    count._next = 0;
                    minimum._next = 255;
                    maximum._next = 0;
                }
            }
            if (evaluate_gain) {
                span = unsigned(measured_span);
                if (span < unsigned(minimum_span_in()) && level < 4)
                    next_level = unsigned(level) + 1;
                else if (span > unsigned(maximum_span_in()) && level > 0)
                    next_level = unsigned(level) - 1;
                level._next = next_level;
                selection._next = 31 ^ (1u << next_level);
                ready._next = span >= unsigned(minimum_span_in()) && span <= unsigned(maximum_span_in());
                weak._next = span < unsigned(minimum_span_in()) && level == 4;
                overload._next = span > unsigned(maximum_span_in()) && level == 0;
                if (next_level != unsigned(level)) {
                    state._next = 0;
                    count._next = 0;
                    minimum._next = 255;
                    maximum._next = 0;
                    evaluate_span._next = 0;
                    evaluate_gain._next = 0;
                    ready._next = 0;
                    changed._next = 1;
                }
            }
        }
        if (invalid_config || adc_fault_in() || fault) {
            state._next = 2;
            level._next = 0;
            selection._next = 30;
            count._next = 0;
            minimum._next = 255;
            maximum._next = 0;
            measured_span._next = 0;
            window_minimum._next = 255;
            window_maximum._next = 0;
            evaluate_span._next = 0;
            evaluate_gain._next = 0;
            centre._next = 128;
            ready._next = 0;
            changed._next = 0;
            weak._next = 0;
            overload._next = 0;
            fault._next = 1;
        }
        if (reset || restart_in() || !enable_in()) {
            state._next = 0;
            level._next = 0;
            selection._next = 30;
            count._next = 0;
            minimum._next = 255;
            maximum._next = 0;
            measured_span._next = 0;
            window_minimum._next = 255;
            window_maximum._next = 0;
            evaluate_span._next = 0;
            evaluate_gain._next = 0;
            centre._next = 128;
            ready._next = 0;
            changed._next = 0;
            weak._next = 0;
            overload._next = 0;
            fault._next = 0;
        }
    }
    void _strobe() {
        state.strobe();
        level.strobe();
        selection.strobe();
        count.strobe();
        minimum.strobe();
        maximum.strobe();
        measured_span.strobe();
        centre.strobe();
        window_minimum.strobe();
        window_maximum.strobe();
        evaluate_span.strobe();
        evaluate_gain.strobe();
        ready.strobe();
        changed.strobe();
        weak.strobe();
        overload.strobe();
        fault.strobe();
    }
};
