#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Reset the receive data path when external reset or clock qualification is
// withdrawn. The clock-generation controllers remain outside this reset tree.
// Each destination releases only after two edges of its own delivered clock.
class PHYResetRelease : public Module {
public:
    _PORT(bool) reset_ref_out = _ASSIGN(!released_ref);
    _PORT(bool) reset_adc_out = _ASSIGN(!released_adc);
    _PORT(bool) reset_pcs_out = _ASSIGN(!released_pcs);

private:
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> first_ref{}, released_ref{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> first_adc{}, released_adc{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> first_pcs{}, released_pcs{};

public:
    void _assign() {}
    void _work_ref_clk(bool) {
        first_ref._next = 1;
        released_ref._next = first_ref;
    }
    void _strobe_ref_clk() {
        first_ref.strobe();
        released_ref.strobe();
    }
    void _reset_pos_ref_clk() {
        first_ref._next = 0;
        released_ref._next = 0;
    }
    void _work_neg_ref_clk(bool) {}
    void _strobe_neg_ref_clk() {}
    void _work_adc_clk(bool) {
        first_adc._next = 1;
        released_adc._next = first_adc;
    }
    void _strobe_adc_clk() {
        first_adc.strobe();
        released_adc.strobe();
    }
    void _reset_pos_adc_clk() {
        first_adc._next = 0;
        released_adc._next = 0;
    }
    void _work_neg_adc_clk(bool) {}
    void _strobe_neg_adc_clk() {}
    void _work_pcs_clk(bool) {
        first_pcs._next = 1;
        released_pcs._next = first_pcs;
    }
    void _strobe_pcs_clk() {
        first_pcs.strobe();
        released_pcs.strobe();
    }
    void _reset_pos_pcs_clk() {
        first_pcs._next = 0;
        released_pcs._next = 0;
    }
    void _work_neg_pcs_clk(bool) {}
    void _strobe_neg_pcs_clk() {}
};
