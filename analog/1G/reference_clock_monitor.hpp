#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Qualify the delivered ADC clock against the external reference. Two complete
// 4096-reference-cycle windows must contain 8192 +/- 2 ADC edges. This measures
// frequency/continuity, not phase noise or analog PLL stability. Gray counters
// cross through two flops; implementation must constrain their bus skew.
class ReferenceClockMonitor : public Module {
public:
    _PORT(bool) power_good_in;
    _PORT(bool) ref_ready_out = _ASSIGN(power_good_in() && qualified && adc_silence < 8);
    _PORT(bool) adc_ready_out = _ASSIGN(power_good_in() && ready_sync2 && ref_silence < 16 && !reference_lost);
    _PORT(u<14>) measured_edges_out = _ASSIGN_REG(measured_edges);

private:
    reg<u<14>> adc_count{}, adc_gray{};
    reg<u<8>> ref_count{}, ref_gray{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<14>> adc_sync1{}, adc_sync2{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<8>> ref_sync1{}, ref_sync2{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> ready_sync1{}, ready_sync2{};
    reg<u<12>> window{};
    reg<u<14>> baseline{}, measured_edges{}, last_adc{};
    reg<u<8>> last_ref{};
    reg<u<4>> adc_silence{};
    reg<u<5>> ref_silence{};
    reg<u1> primed{}, good_window{}, qualified{};
    reg<u1> reference_lost{};

    unsigned binary(unsigned gray) {
        unsigned value;
        value = gray ^ (gray >> 1);
        value = value ^ (value >> 2);
        value = value ^ (value >> 4);
        return value ^ (value >> 8);
    }

public:
    void _assign() {}
    void _work_ref_clk(bool reset) {
        u<8> next;
        u<14> delta, count;
        next = ref_count + 1;
        ref_count._next = next;
        ref_gray._next = next ^ (next >> 1);
        adc_sync1._next = adc_gray;
        adc_sync2._next = adc_sync1;
        count = binary(unsigned(adc_sync2));
        delta = count - baseline;
        last_adc._next = adc_sync2;
        adc_silence._next = adc_sync2 == last_adc ? (adc_silence < 8 ? unsigned(adc_silence) + 1 : 8) : 0;
        window._next = window + 1;
        if (window == 4095) {
            baseline._next = count;
            measured_edges._next = delta;
            primed._next = 1;
            good_window._next = primed && delta >= 8190 && delta <= 8194;
            qualified._next = primed && good_window && delta >= 8190 && delta <= 8194;
        }
        if (adc_silence >= 8) {
            qualified._next = 0;
            good_window._next = 0;
            primed._next = 0;
            window._next = 0;
        }
        if (reset || !power_good_in()) {
            ref_count._next = 0; ref_gray._next = 0;
            adc_sync1._next = 0; adc_sync2._next = 0;
            window._next = 0; baseline._next = 0; measured_edges._next = 0;
            last_adc._next = 0; adc_silence._next = 0;
            primed._next = 0; good_window._next = 0; qualified._next = 0;
        }
    }
    void _strobe_ref_clk() {
        ref_count.strobe(); ref_gray.strobe();
        adc_sync1.strobe(); adc_sync2.strobe();
        window.strobe(); baseline.strobe(); measured_edges.strobe();
        last_adc.strobe(); adc_silence.strobe();
        primed.strobe(); good_window.strobe(); qualified.strobe();
    }
    void _work_adc_clk(bool reset) {
        u<14> next;
        next = adc_count + 1;
        adc_count._next = next;
        adc_gray._next = next ^ (next >> 1);
        ref_sync1._next = ref_gray;
        ref_sync2._next = ref_sync1;
        ready_sync1._next = qualified && adc_silence < 8;
        ready_sync2._next = ready_sync1;
        last_ref._next = ref_sync2;
        ref_silence._next = ref_sync2 == last_ref ? (ref_silence < 16 ? unsigned(ref_silence) + 1 : 16) : 0;
        // A stopped reference cannot clock its own qualification registers.
        // Latch its loss in the running ADC domain until a joint restart.
        if (ref_silence >= 16)
            reference_lost._next = 1;
        if (reset || !power_good_in()) {
            adc_count._next = 0; adc_gray._next = 0;
            ref_sync1._next = 0; ref_sync2._next = 0;
            ready_sync1._next = 0; ready_sync2._next = 0;
            last_ref._next = 0; ref_silence._next = 0;
            reference_lost._next = 0;
        }
    }
    void _strobe_adc_clk() {
        adc_count.strobe(); adc_gray.strobe();
        ref_sync1.strobe(); ref_sync2.strobe();
        ready_sync1.strobe(); ready_sync2.strobe();
        last_ref.strobe(); ref_silence.strobe();
        reference_lost.strobe();
    }
    void _work_neg_ref_clk(bool) {}
    void _strobe_neg_ref_clk() {}
    void _work_neg_adc_clk(bool) {}
    void _strobe_neg_adc_clk() {}
};
