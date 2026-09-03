`default_nettype none

import Predef_pkg::*;
import TemplateEmptyStructArgFullState_pkg::*;
import TemplateEmptyStructArgStageint_State_pkg::*;
import TemplateEmptyStructArgBigStateTemplateEmptyStructArgFullState_TemplateEmptyStructArgStageint_State_pkg::*;


module TemplateEmptyStructArg (
    input wire clk
,   input wire reset
,   input wire TemplateEmptyStructArgBigStateTemplateEmptyStructArgFullState_TemplateEmptyStructArgStageint_State state_in
,   output wire[16-1:0] value_out
);

    typedef TemplateEmptyStructArgBigStateTemplateEmptyStructArgFullState_TemplateEmptyStructArgStageint_State BigState;

    // regs and combs
    logic[16-1:0] value_comb;

    // members

    // tmp variables


    always_comb begin : value_comb_func  // value_comb_func
        TemplateEmptyStructArgBigStateTemplateEmptyStructArgFullState_TemplateEmptyStructArgStageint_State state;
        state = state_in;
        value_comb = state.value;
    end

    task _work (input logic unused);
    begin: _work
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = value_comb;


endmodule
