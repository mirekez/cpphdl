`default_nettype none

import Predef_pkg::*;
import Conv16_pkg::*;
import Native16_pkg::*;
import ArithmeticMiniConv16_pkg::*;


module EncoderMiniConv16 (
    input wire clk
,   input wire reset
,   input wire[16-1:0] value_in
,   output wire[16-1:0] value_out
);


    // regs and combs
    logic[16-1:0] out_comb;

    // members

    // tmp variables


    function Conv16 ArithmeticMiniConv16___convert (input Native16 val);
        Conv16 res;
        res._.raw='h0;
        res._.data.sign=val._.data.sign;
        res._.data.exponent=val._.data.exponent;
        res._.data.mantissa=val._.data.mantissa;
        return res;
    endfunction

    always_comb begin : out_comb_func  // out_comb_func
        Native16 native;
        Conv16 conv;
        native._.raw=unsigned'(16'(value_in));
        conv = ArithmeticMiniConv16___convert(native);
        out_comb = conv._.raw;
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

    assign value_out = out_comb;


endmodule
