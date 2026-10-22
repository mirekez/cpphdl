#pragma once
#include "receive_gain_control.hpp"
#include "receive_range_calibration.hpp"

// Acquire analog gain, freeze it, then calibrate the ADC's digital scale.
class ReceiveAnalogAcquisition : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) restart_in;
    _PORT(bool) training_in;
    _PORT(bool) advance_in;
    _PORT(bool) adc_fault_in;
    _PORT(u<8>) sample_in;
    _PORT(u<8>) minimum_span_in;
    _PORT(u<8>) maximum_span_in;
    _PORT(u<8>) target_span_in;
    _PORT(u<5>) select_n_out;
    _PORT(u<3>) level_out;
    _PORT(u<8>) offset_out;
    _PORT(u<16>) gain_out;
    _PORT(u<8>) span_out;
    _PORT(u<8>) centre_out;
    _PORT(bool) ready_out;
    _PORT(bool) busy_out;
    _PORT(bool) settled_out;
    _PORT(bool) weak_out;
    _PORT(bool) overload_out;
    _PORT(bool) fault_out;

private:
    ReceiveAnalogGainControl control;
    ReceiveRangeCalibration calibration;
    reg<u1> launched{};
    bool launch() {
        return enable_in() && training_in() && !restart_in() && !adc_fault_in() &&
            !launched && control.ready_out() && !control.fault_out();
    }

public:
    void _assign() {
        control.enable_in = _ASSIGN(enable_in());
        control.restart_in = _ASSIGN(restart_in());
        control.training_in = _ASSIGN(training_in() && !launched);
        control.advance_in = _ASSIGN(advance_in());
        control.adc_fault_in = _ASSIGN(adc_fault_in() || calibration.fault_out());
        control.sample_in = _ASSIGN(sample_in());
        control.minimum_span_in = _ASSIGN(minimum_span_in());
        control.maximum_span_in = _ASSIGN(maximum_span_in());
        calibration.enable_in = _ASSIGN(enable_in() && !restart_in() && !adc_fault_in() &&
            !control.changed_out() && !control.fault_out());
        calibration.start_in = _ASSIGN(launch());
        calibration.centre_in = _ASSIGN(control.centre_out());
        calibration.span_in = _ASSIGN(control.span_out());
        calibration.target_span_in = _ASSIGN(target_span_in());
        select_n_out = _ASSIGN(control.select_n_out());
        level_out = _ASSIGN(control.level_out());
        offset_out = _ASSIGN(calibration.offset_out());
        gain_out = _ASSIGN(calibration.gain_out());
        span_out = _ASSIGN(control.span_out());
        centre_out = _ASSIGN(control.centre_out());
        fault_out = _ASSIGN(control.fault_out() || calibration.fault_out());
        ready_out = _ASSIGN(enable_in() && !restart_in() && !adc_fault_in() &&
            calibration.ready_out() && control.ready_out() && !fault_out());
        busy_out = _ASSIGN(calibration.busy_out());
        settled_out = _ASSIGN(control.settled_out());
        weak_out = _ASSIGN(control.weak_out());
        overload_out = _ASSIGN(control.overload_out());
        control._assign();
        calibration._assign();
    }
    void _work(bool reset) {
        if (launch())
            launched._next = 1;
        if (reset || restart_in() || !enable_in())
            launched._next = 0;
        control._work(reset);
        calibration._work(reset);
    }
    void _strobe() {
        control._strobe();
        calibration._strobe();
        launched.strobe();
    }
};
