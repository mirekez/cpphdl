`default_nettype none

import Predef_pkg::*;


module BoolConversion (
    input wire clk
,   input wire reset
,   input wire[7:0] value_in
,   output wire implicit_out
,   output wire explicit_out
,   output wire assigned_out
,   output wire comb_out
,   output wire registered_out
);


    // regs and combs
    logic converted_comb;
    logic registered_value;

    // members

    // tmp variables


    always_comb begin : converted_comb_func  // converted_comb_func
        converted_comb=((value_in) != '0);
    end

    task _work (input logic reset);
    begin: _work
        registered_value=((value_in) != '0);
        if (reset) begin
            registered_value=0;
        end
    end
    endtask

    generate  // _assign
        assign assigned_out = ((value_in) != '0);
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign implicit_out = ((value_in) != '0);

    assign explicit_out = ((value_in) != '0);

    assign comb_out = converted_comb;

    assign registered_out = registered_value;


endmodule
