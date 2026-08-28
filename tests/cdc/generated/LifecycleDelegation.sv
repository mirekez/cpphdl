`default_nettype none

import Predef_pkg::*;


module LifecycleDelegation (
    input wire fast_clk
,   input wire slow_clk
,   input wire reset
,   output wire[8-1:0] count_out
,   output wire[8-1:0] child_count_out
);


    // regs and combs
    reg[8-1:0] count_reg;

    // members
    wire[8-1:0] child__count_out;
    LifecycleDelegationChild      child (
        .fast_clk(fast_clk)
,       .slow_clk(slow_clk)
,       .reset(reset)
,       .count_out(child__count_out)
    );

    // tmp variables
    logic[8-1:0] count_reg_tmp;


    generate  // _assign
        assign child_count_out = unsigned'(8'(child__count_out));
    endgenerate

    task _work (input logic reset);
    begin: _work
        count_reg_tmp = count_reg + 'h1;
        if (reset) begin
            count_reg_tmp = '0;
        end
    end
    endtask

    task _work_fast_clk (input logic reset);
    begin: _work_fast_clk
        _work(reset);
    end
    endtask

    task _work_slow_clk (input logic unused);
    begin: _work_slow_clk
    end
    endtask

    always_ff @(posedge fast_clk) begin
        count_reg_tmp = count_reg;

        _work_fast_clk(reset);

        count_reg <= count_reg_tmp;
    end

    always_ff @(posedge slow_clk) begin

        _work_slow_clk(reset);

    end

    assign count_out = count_reg;


endmodule
