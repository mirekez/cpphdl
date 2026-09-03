`default_nettype none

import Predef_pkg::*;


module LifecycleDelegationChild (
    input wire fast_clk
,   input wire slow_clk
,   input wire reset
,   output wire[8-1:0] count_out
);


    // regs and combs
    reg[8-1:0] count_reg;

    // members

    // tmp variables
    logic[8-1:0] count_reg_tmp;


    task _work (input logic reset);
    begin: _work
        count_reg_tmp = count_reg + 'h1;
        if (reset) begin
            count_reg_tmp = '0;
        end
    end
    endtask

    generate  // _assign
    endgenerate

    task _work_slow_clk (input logic reset);
    begin: _work_slow_clk
    end
    endtask

    always_ff @(posedge fast_clk) begin
        count_reg_tmp = count_reg;

        _work(reset);

        count_reg <= count_reg_tmp;
    end

    always_ff @(posedge slow_clk) begin

        _work_slow_clk(reset);

    end

    assign count_out = count_reg;


endmodule
