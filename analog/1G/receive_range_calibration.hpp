#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Calibrate a measured ADC range without requiring five labeled PAM levels.
// The caller supplies the normalized span represented by that training range.
class ReceiveRangeCalibration : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) start_in;
    _PORT(u<8>) centre_in;
    _PORT(u<8>) span_in;
    _PORT(u<8>) target_span_in;
    _PORT(u<8>) offset_out;
    _PORT(u<16>) gain_out;
    _PORT(bool) ready_out;
    _PORT(bool) busy_out;
    _PORT(bool) fault_out;
private:
    reg<u<16>> numerator{}, quotient{}, gain{};
    reg<u<9>> remainder{};
    reg<u<8>> denominator{}, pending_offset{}, offset{};
    reg<u<5>> remaining{};
    reg<u1> busy{}, ready{}, fault{};
public:
    void _assign() {
        offset_out = _ASSIGN_REG(offset);
        gain_out = _ASSIGN_REG(gain);
        ready_out = _ASSIGN_REG(ready);
        busy_out = _ASSIGN_REG(busy);
        fault_out = _ASSIGN_REG(fault);
    }
    void _work(bool reset) {
        unsigned next_remainder, next_quotient;
        next_remainder = (unsigned(remainder) << 1) | (unsigned(numerator) >> 15);
        next_quotient = unsigned(quotient) << 1;
        if (busy && !start_in()) {
            // One restoring division bit per edge bounds the arithmetic depth.
            if (next_remainder >= unsigned(denominator)) {
                next_remainder -= unsigned(denominator);
                next_quotient |= 1;
            }
            numerator._next = numerator << 1;
            remainder._next = next_remainder;
            quotient._next = next_quotient;
            remaining._next = remaining - 1;
            if (remaining == 1) {
                offset._next = pending_offset;
                gain._next = next_quotient;
                busy._next = 0;
                ready._next = 1;
            }
        }
        if (start_in()) {
            pending_offset._next = centre_in();
            denominator._next = span_in();
            // Q8.8 gain, rounded to nearest with half cases rounded upward.
            numerator._next = (unsigned(target_span_in()) << 8) + (unsigned(span_in()) >> 1);
            quotient._next = 0;
            remainder._next = 0;
            remaining._next = 16;
            busy._next = 1;
            ready._next = 0;
            fault._next = 0;
            if (span_in() == 0 || target_span_in() == 0) {
                busy._next = 0;
                remaining._next = 0;
                fault._next = 1;
                offset._next = 128;
                gain._next = 256;
            }
        }
        if (reset || !enable_in()) {
            numerator._next = 0;
            quotient._next = 0;
            remainder._next = 0;
            denominator._next = 0;
            pending_offset._next = 128;
            offset._next = 128;
            gain._next = 256;
            remaining._next = 0;
            busy._next = 0;
            ready._next = 0;
            fault._next = 0;
        }
    }
    void _strobe() {
        numerator.strobe();
        quotient.strobe();
        gain.strobe();
        remainder.strobe();
        denominator.strobe();
        pending_offset.strobe();
        offset.strobe();
        remaining.strobe();
        busy.strobe();
        ready.strobe();
        fault.strobe();
    }
};
