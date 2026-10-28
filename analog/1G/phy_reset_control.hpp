#pragma once
#include "phy_reset_release.hpp"

// Keep the clock generators and their acquisition controllers outside this
// reset tree. A failed qualification clears the receive data path even when
// its delivered clocks have stopped. Release requires two destination edges.
class PHYResetControl : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) power_good_in;
    _PORT(bool) adc_ready_in;
    _PORT(bool) pcs_ready_in;
    _PORT(bool) reset_ref_out = _ASSIGN(resets.reset_ref_out());
    _PORT(bool) reset_adc_out = _ASSIGN(resets.reset_adc_out());
    _PORT(bool) reset_pcs_out = _ASSIGN(resets.reset_pcs_out());

    PHYResetRelease resets;

private:
    _LAZY_COMB(withdraw_comb, bool)
        return withdraw_comb = !enable_in() || !power_good_in() ||
                               !adc_ready_in() || !pcs_ready_in();
    }

public:
    void _assign() { resets._assign(); }
    void _work_ref_clk(bool reset) { resets._work_ref_clk(reset || withdraw_comb_func()); }
    void _work_adc_clk(bool reset) { resets._work_adc_clk(reset || withdraw_comb_func()); }
    void _work_pcs_clk(bool reset) { resets._work_pcs_clk(reset || withdraw_comb_func()); }
    void _strobe_ref_clk() { resets._strobe_ref_clk(); }
    void _strobe_adc_clk() { resets._strobe_adc_clk(); }
    void _strobe_pcs_clk() { resets._strobe_pcs_clk(); }
    void _work_neg_ref_clk(bool) {}
    void _work_neg_adc_clk(bool) {}
    void _work_neg_pcs_clk(bool) {}
    void _strobe_neg_ref_clk() {}
    void _strobe_neg_adc_clk() {}
    void _strobe_neg_pcs_clk() {}
#ifndef SYNTHESIS
    // Native simulators dispatch asynchronous events even with clocks stopped.
    // RTL performs this dispatch in the child's asynchronous clock blocks.
    void sim_assert_ref_reset() { resets._reset_pos_ref_clk(); }
    void sim_assert_adc_reset() { resets._reset_pos_adc_clk(); }
    void sim_assert_pcs_reset() { resets._reset_pos_pcs_clk(); }
#endif
};
