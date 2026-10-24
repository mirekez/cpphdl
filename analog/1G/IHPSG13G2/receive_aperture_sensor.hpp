#pragma once
#include <basic/phase_frequency_detector.hpp>

namespace analog::one_g::ihp_sg13g2 {

// Open-loop prerequisite for a delivered-edge PLL. All ports are electrical;
// no edge detector, delay or reset is supplied by the simulation host.
class receive_aperture_sensor : public IC {
public:
    _PORT(electrical) REF;
    _PORT(electrical) SAMPLE_N;
    _PORT(electrical) CLEAR;
    _PORT(electrical) REF_INPUT;
    _PORT(electrical) SAMPLE_INPUT;
    _PORT(electrical) REF_CLOCK;
    _PORT(electrical) SAMPLE_CLOCK;
    _PORT(electrical) REF_EDGE;
    _PORT(electrical) SAMPLE_EDGE;
    _PORT(electrical) REF_CLEAR;
    _PORT(electrical) CLEAR_DELAY;
    _PORT(electrical) UP;
    _PORT(electrical) DOWN;
    std::array<cmos<channel_model>, 2> reference_buffers, sample_buffers;
    cmos<channel_model> delay_inverter, clear_inverter;
    cmos_nand fall_pulse, combine_clear;
    cmos_dff reference_retimer;
    cmos_divide2 feedback_divider;
    cmos_phase_frequency_detector detector;
    mos_model channel{channel_strength(.01), voltage(.35)};

    void _assign() override {
        REF_INPUT = resistor(REF, resistance(28000.0 / 11.0));
        SAMPLE_INPUT = resistor(SAMPLE_N, resistance(7000));
        SAMPLE_INPUT = resistor(Vss, resistance(4000));
        REF_INPUT = capacitor(Vss, capacitance_value(5e-15));
        SAMPLE_INPUT = capacitor(Vss, capacitance_value(5e-15));
        reference_buffers[0].A = REF_INPUT;
        sample_buffers[0].A = SAMPLE_INPUT;
        reference_buffers[1].A = reference_buffers[0].Y;
        sample_buffers[1].A = sample_buffers[0].Y;
        reference_buffers[1].Y = REF_CLOCK;
        sample_buffers[1].Y = SAMPLE_CLOCK;
        for (auto* bank : {&reference_buffers, &sample_buffers}) {
            for (auto& stage : *bank) {
                stage.Vdd = Vdd;
                stage.Vss = Vss;
                stage.model.transistor_parameters = channel;
                stage.elaborate();
                stage.Y = capacitor(Vss, capacitance_value(5e-15));
            }
        }
        // A short physical clear pulse follows REF falling. Its release leaves
        // the low-transparent DFF master time to capture one before REF rises.
        CLEAR_DELAY = resistor(reference_buffers[0].Y, resistance(1000));
        CLEAR_DELAY = capacitor(Vss, capacitance_value(.5e-12));
        delay_inverter.A = CLEAR_DELAY;
        clear_inverter.A = CLEAR;
        for (auto* inverter : {&delay_inverter, &clear_inverter}) {
            inverter->Vdd = Vdd;
            inverter->Vss = Vss;
            inverter->model.transistor_parameters = channel;
            inverter->elaborate();
            inverter->Y = capacitor(Vss, capacitance_value(5e-15));
        }
        fall_pulse.A = reference_buffers[0].Y;
        fall_pulse.B = delay_inverter.Y;
        combine_clear.A = fall_pulse.Y;
        combine_clear.B = clear_inverter.Y;
        combine_clear.Y = REF_CLEAR;
        for (auto* gate : {&fall_pulse, &combine_clear}) {
            gate->Vdd = Vdd;
            gate->Vss = Vss;
            gate->parameters = channel;
            gate->output_capacitance = capacitance_value(5e-15);
            gate->weak_returns = false;
            gate->elaborate();
        }
        reference_retimer.D = Vdd;
        reference_retimer.CLOCK = REF_CLOCK;
        reference_retimer.CLEAR = REF_CLEAR;
        reference_retimer.Q = REF_EDGE;
        reference_retimer.Vdd = Vdd;
        reference_retimer.Vss = Vss;
        reference_retimer.elaborate();
        feedback_divider.CLOCK = SAMPLE_CLOCK;
        feedback_divider.CLEAR = CLEAR;
        feedback_divider.Q = SAMPLE_EDGE;
        feedback_divider.Vdd = Vdd;
        feedback_divider.Vss = Vss;
        feedback_divider.elaborate();
        detector.REF = REF_EDGE;
        detector.FEEDBACK = SAMPLE_EDGE;
        detector.CLEAR = CLEAR;
        detector.UP = UP;
        detector.DOWN = DOWN;
        detector.Vdd = Vdd;
        detector.Vss = Vss;
        detector.elaborate();
    }
};
} // namespace analog::one_g::ihp_sg13g2
