`default_nettype none

import Predef_pkg::*;
import TemplateMemberConv16_pkg::*;
import TemplateMemberArithmeticHelperTemplateMemberConv16_pkg::*;
import TemplateMemberNative16_pkg::*;


module TemplateMemberArithmeticTemplateMemberConv16 (
    input wire clk
,   input wire reset
,   input wire[16-1:0] value_in
,   output wire[16-1:0] value_out
);


    // regs and combs
    TemplateMemberArithmeticHelperTemplateMemberConv16 helper;
    logic[16-1:0] value_comb;

    // members

    // tmp variables


    task _work (input logic unused);
    begin: _work
    end
    endtask

    generate  // _assign
    endgenerate

    function TemplateMemberConv16 TemplateMemberArithmeticHelperTemplateMemberConv16___convert_default_to_conv (
        input TemplateMemberArithmeticHelperTemplateMemberConv16 _this
,       input TemplateMemberNative16 val
    );
        TemplateMemberConv16 res;
        res._.raw='h0;
        res._.data.sign=val._.data.sign;
        if (val._.data.exponent == TemplateMemberArithmeticHelperTemplateMemberConv16_pkg::DEFAULT_EXP_MAX) begin
            res._.data.exponent=TemplateMemberArithmeticHelperTemplateMemberConv16_pkg::CONV_EXP_MAX;
            res._.data.mantissa=(val._.data.mantissa) ? ('h1) : ('h0);
        end
        else begin
            res._.data.exponent=val._.data.exponent & TemplateMemberArithmeticHelperTemplateMemberConv16_pkg::CONV_EXP_MAX;
            res._.data.mantissa=val._.data.mantissa & TemplateMemberArithmeticHelperTemplateMemberConv16_pkg::CONV_MANT_MAX;
        end
        return res;
    endfunction

    always_comb begin : value_comb_func  // value_comb_func
        TemplateMemberNative16 native;
        TemplateMemberConv16 conv;
        native._.raw=unsigned'(16'(value_in));
        conv=TemplateMemberArithmeticHelperTemplateMemberConv16___convert_default_to_conv(helper, native);
        value_comb=conv._.raw;
    end

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = value_comb;


endmodule
