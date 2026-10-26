#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Tune a monotonic coarse/fine electrical oscillator from the sole external
// 125 MHz reference. Measurement is independent of the oscillator's clock, so
// a stopped/too-slow candidate can still be increased during acquisition.
// 137..138 delivered edges per 128 reference cycles qualify approximately
// 134 MHz; the one-edge measurement uncertainty fits inside 132..136 MHz.
// Once admitted, measure 512 reference cycles: 543..556 edges retain the
// same operating band with room for the real output gate's loading shift.
class ProcessingClockControl : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(u<8>) coarse_n_out = _ASSIGN(~unsigned(trim >> 8) & 255);
    _PORT(u<8>) fine_n_out = _ASSIGN(~unsigned(trim) & 255);
    _PORT(bool) clear_out = _ASSIGN(!enable_in() || state == 0);
    _PORT(bool) ready_ref_out = _ASSIGN(enable_in() && ready && !fault);
    _PORT(bool) ready_pcs_out = _ASSIGN(enable_in() && ready && qualified_ready);
    _PORT(bool) fault_out = _ASSIGN_REG(fault);
    _PORT(u<14>) measured_edges_out = _ASSIGN_REG(measured);

private:
    reg<u<16>> trim{}, pending_trim{}, lower{}, upper{};
    reg<u<14>> count{}, gray{}, baseline{}, measured{};
    reg<u1> carry2{}, carry4{}, carry6{}, carry8{}, carry10{}, carry12{};
    reg<u1> full2{}, full4{}, full6{}, full8{}, full10{}, early_low{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<14>> count_sync1{}, count_sync2{};
    reg<u<14>> count_binary{};
    reg<u<8>> reference_count{}, reference_gray{}, last_reference{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<8>> reference_sync1{}, reference_sync2{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> ready_sync1{}, ready_sync2{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> measure_sync1{}, measure_sync2{};
    reg<u<15>> silence{};
    reg<u1> reference_same{}, reference_same_low{}, reference_same_high{}, counting{};
    reg<u1> reference_lost{}, ready{}, good{}, fault{}, qualified_ready{}, measurement_active{};
    reg<u<3>> state{};
    reg<u<7>> window{};
    reg<u<2>> monitor_window{};

    unsigned binary(unsigned value) {
        value ^= value >> 1;
        value ^= value >> 2;
        value ^= value >> 4;
        return value ^ (value >> 8);
    }

public:
    void _assign() {}
    void _work_ref_clk(bool reset) {
        u<8> next_reference;
        u<14> current, delta;
        unsigned lo, hi, slow_lo, fast_hi, slow_middle, fast_middle;
        bool too_slow;
        next_reference = reference_count + 1;
        reference_count._next = next_reference;
        reference_gray._next = next_reference ^ (next_reference >> 1);
        // Register the decoded level before crossing domains. Transitions
        // between search states must not create a pulse at the synchronizer.
        measurement_active._next = state == 2 || state == 3 || state == 5;
        count_sync1._next = gray;
        count_sync2._next = count_sync1;
        // Separate Gray decoding from subtraction and the search decision.
        // This fixed reference-domain delay also cancels in measured differences.
        count_binary._next = binary(unsigned(count_sync2));
        current = count_binary;
        delta = measured;
        // The monitor counts four acquisition windows. Its search direction
        // must use that same scale when a previously admitted clock drifts.
        too_slow = ready ? delta < 543 : delta < 137;
        lo = unsigned(lower);
        hi = unsigned(upper);
        if (ready) {
            lo = 0;
            hi = 65535;
        }
        slow_lo = trim == 65535 ? lo : unsigned(trim) + 1;
        fast_hi = trim == 0 ? hi : unsigned(trim) - 1;
        // Form both candidate midpoints before the late measured-frequency
        // decision; select a completed sum instead of selecting its operands.
        slow_middle = (slow_lo + hi) >> 1;
        fast_middle = (lo + fast_hi) >> 1;
        window._next = window + 1;
        if (state == 0) {
            // Hold a real VCO stage during DAC/bias power-up.
            if (window == 31) {
                state._next = 1;
                window._next = 0;
            }
        } else if (state == 4) {
            // Withdraw both readiness outputs before changing the DAC. Keep
            // the old clock running for 32 reference cycles so its domain can
            // observe the withdrawal before the electrical frequency changes.
            if (window == 31) {
                trim._next = pending_trim;
                state._next = 1;
                window._next = 0;
            }
        } else if (state == 5) {
            // The completed window's edge count is already registered. Decide on the
            // following reference edge so subtraction and the search/fault
            // logic do not share one physical timing path.
            state._next = 3;
            // Acquisition uses one window. After admission, four windows
            // resolve the allowed band despite clock-gate supply/loading shift.
            if (ready ? (delta >= 543 && delta <= 556) : (delta >= 137 && delta <= 138)) {
                good._next = 1;
                ready._next = good;
            } else {
                good._next = 0;
                ready._next = 0;
                if (ready) {
                    // Reacquire after PVT/supply drift; old search bounds
                    // describe the previous operating point.
                    lo = 0;
                    hi = 65535;
                }
                if (too_slow) {
                    if (trim == 65535)
                        fault._next = 1;
                    else
                        lo = unsigned(trim) + 1;
                } else {
                    if (trim == 0)
                        fault._next = 1;
                    else
                        hi = unsigned(trim) - 1;
                }
                if (lo > hi) {
                    fault._next = 1;
                } else {
                    lower._next = lo;
                    upper._next = hi;
                    pending_trim._next = too_slow ? slow_middle : fast_middle;
                    state._next = 4;
                    window._next = 0;
                }
            }
        } else if (window == 127 && !fault) {
            if (!ready || monitor_window == 3) baseline._next = current;
            if (state == 1) {
                state._next = 2;
                // Discard a complete settling window.
            } else if (state == 2) {
                // The oscillator can be absent while its trim is acquired.
                // Discard the first synchronized window before measuring a
                // difference from the continuously running edge counter.
                state._next = 3;
            } else if (ready && monitor_window != 3) {
                monitor_window._next = monitor_window + 1;
            } else {
                monitor_window._next = 0;
                measured._next = current - baseline;
                state._next = 5;
            }
        }
        if (reset || !enable_in()) {
            trim._next = 32768;
            pending_trim._next = 32768;
            lower._next = 0;
            upper._next = 65535;
            baseline._next = 0;
            measured._next = 0;
            count_sync1._next = 0;
            count_sync2._next = 0;
            count_binary._next = 0;
            reference_count._next = 0;
            reference_gray._next = 0;
            measurement_active._next = 0;
            state._next = 0;
            window._next = 0;
            monitor_window._next = 0;
            ready._next = 0;
            good._next = 0;
            fault._next = 0;
        }
    }
    void _strobe_ref_clk() {
        trim.strobe();
        pending_trim.strobe();
        lower.strobe();
        upper.strobe();
        baseline.strobe();
        measured.strobe();
        count_sync1.strobe();
        count_sync2.strobe();
        count_binary.strobe();
        reference_count.strobe();
        reference_gray.strobe();
        measurement_active.strobe();
        state.strobe();
        window.strobe();
        monitor_window.strobe();
        ready.strobe();
        good.strobe();
        fault.strobe();
    }
    void _work_pcs_clk(bool reset) {
        u<14> next;
        // Two-bit increments consume predicted carries, avoiding a fourteen-bit
        // ripple path at the acquisition frequency. early_low is sampled at 1;
        // carries are formed at 2 and consumed at 3. Higher segments cannot
        // change between the prefix sample and that carry prediction.
        next = (((unsigned(count) & 3) + 1) & 3) |
               (((((unsigned(count) >> 2) & 3) + unsigned(carry2)) & 3) << 2) |
               (((((unsigned(count) >> 4) & 3) + unsigned(carry4)) & 3) << 4) |
               (((((unsigned(count) >> 6) & 3) + unsigned(carry6)) & 3) << 6) |
               (((((unsigned(count) >> 8) & 3) + unsigned(carry8)) & 3) << 8) |
               (((((unsigned(count) >> 10) & 3) + unsigned(carry10)) & 3) << 10) |
               (((((unsigned(count) >> 12) & 3) + unsigned(carry12)) & 3) << 12);
        full2._next = ((unsigned(count) >> 2) & 3) == 3;
        full4._next = ((unsigned(count) >> 2) & 15) == 15;
        full6._next = ((unsigned(count) >> 6) & 3) == 3;
        full8._next = ((unsigned(count) >> 6) & 15) == 15;
        full10._next = ((unsigned(count) >> 10) & 3) == 3;
        early_low._next = (unsigned(count) & 3) == 1;
        carry2._next = early_low;
        carry4._next = early_low && full2;
        carry6._next = early_low && full4;
        carry8._next = early_low && full4 && full6;
        carry10._next = early_low && full4 && full8;
        carry12._next = early_low && full4 && full8 && full10;
        // Keep counting through acquisition withdrawal: each measurement is
        // a modular difference. Clearing the counter here would add a high
        // fanout control path and a multibit jump at the Gray crossing.
        count._next = next;
        // Register Gray from the previous binary value, separating increment
        // and XOR delay. The extra constant latency cancels in differences.
        gray._next = count ^ (count >> 1);
        reference_sync1._next = reference_gray;
        reference_sync2._next = reference_sync1;
        ready_sync1._next = ready && !fault;
        ready_sync2._next = ready_sync1;
        // Qualify after both synchronizer stages. Register the watchdog
        // decision before the output gate instead of placing it on that path.
        qualified_ready._next = ready_sync2 && !reference_lost;
        // Cross a single acquisition level, never the multibit search state.
        // The reference domain discards a complete window after release.
        measure_sync1._next = measurement_active;
        measure_sync2._next = measure_sync1;
        last_reference._next = reference_sync2;
        counting._next = measure_sync2;
        // Pipeline the eight-bit heartbeat comparison before the sixteen-cycle
        // watchdog. Unary history avoids a comparator/increment/hold path.
        reference_same_low._next = (unsigned(reference_sync2) & 15) == (unsigned(last_reference) & 15);
        reference_same_high._next = (unsigned(reference_sync2) >> 4) == (unsigned(last_reference) >> 4);
        reference_same._next = counting && reference_same_low && reference_same_high;
        silence._next = reference_same ? ((unsigned(silence) << 1) | 1) : 0;
        if ((unsigned(silence) & (1u << 14)) != 0 && reference_same)
            reference_lost._next = 1;
        if (!counting)
            reference_lost._next = 0;
    }
    void _strobe_pcs_clk() {
        count.strobe();
        gray.strobe();
        carry2.strobe();
        carry4.strobe();
        carry6.strobe();
        carry8.strobe();
        carry10.strobe();
        carry12.strobe();
        full2.strobe();
        full4.strobe();
        full6.strobe();
        full8.strobe();
        full10.strobe();
        early_low.strobe();
        reference_sync1.strobe();
        reference_sync2.strobe();
        ready_sync1.strobe();
        ready_sync2.strobe();
        qualified_ready.strobe();
        last_reference.strobe();
        measure_sync1.strobe();
        measure_sync2.strobe();
        counting.strobe();
        silence.strobe();
        reference_same.strobe();
        reference_same_low.strobe();
        reference_same_high.strobe();
        reference_lost.strobe();
    }
    // Assertion initializes both domains even when the oscillator is stopped.
    // Board/chip reset release must satisfy each domain's recovery/removal.
    void _reset_pos_ref_clk() {
        trim._next = 32768;
        pending_trim._next = 32768;
        lower._next = 0;
        upper._next = 65535;
        baseline._next = 0;
        measured._next = 0;
        count_sync1._next = 0;
        count_sync2._next = 0;
        count_binary._next = 0;
        reference_count._next = 0;
        reference_gray._next = 0;
        measurement_active._next = 0;
        state._next = 0;
        window._next = 0;
        ready._next = 0;
        good._next = 0;
        fault._next = 0;
    }
    void _reset_pos_pcs_clk() {
        count._next = 0;
        gray._next = 0;
        carry2._next = 0;
        carry4._next = 0;
        carry6._next = 0;
        carry8._next = 0;
        carry10._next = 0;
        carry12._next = 0;
        full2._next = 0;
        full4._next = 0;
        full6._next = 0;
        full8._next = 0;
        full10._next = 0;
        early_low._next = 0;
        reference_sync1._next = 0;
        reference_sync2._next = 0;
        ready_sync1._next = 0;
        ready_sync2._next = 0;
        qualified_ready._next = 0;
        last_reference._next = 0;
        silence._next = 0;
        reference_same._next = 0;
        reference_same_low._next = 0;
        reference_same_high._next = 0;
        reference_lost._next = 0;
        counting._next = 0;
        measure_sync1._next = 0;
        measure_sync2._next = 0;
    }
    void _work_neg_ref_clk(bool) {}
    void _strobe_neg_ref_clk() {}
    void _work_neg_pcs_clk(bool) {}
    void _strobe_neg_pcs_clk() {}
};
