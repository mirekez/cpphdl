`default_nettype none

import Predef_pkg::*;


module AsyncReset (
    input wire fast_clk
,   input wire slow_clk
,   input wire reset
,   input wire fast_enable_in
,   input wire fast_neg_enable_in
,   input wire slow_enable_in
,   output wire[8-1:0] fast_count_out
,   output wire[8-1:0] fast_neg_count_out
,   output wire[8-1:0] slow_count_out
);


    // regs and combs
    reg[8-1:0] fast_count_reg;
    reg[8-1:0] fast_neg_count_reg;
    reg[8-1:0] slow_count_reg;

    // members

    // tmp variables
    logic[8-1:0] fast_count_reg_tmp;
    logic[8-1:0] fast_neg_count_reg_tmp;
    logic[8-1:0] slow_count_reg_tmp;


    task _work_fast_clk (input logic unused);
    begin: _work_fast_clk
        if (fast_enable_in) begin
            fast_count_reg_tmp = fast_count_reg + 'h1;
        end
    end
    endtask

    task _reset_pos_fast_clk ();
    begin: _reset_pos_fast_clk
        fast_count_reg_tmp = '0;
    end
    endtask

    task _work_neg_fast_clk (input logic unused);
    begin: _work_neg_fast_clk
        if (fast_neg_enable_in) begin
            fast_neg_count_reg_tmp = fast_neg_count_reg + 'h1;
        end
    end
    endtask

    task _reset_neg_fast_clk ();
    begin: _reset_neg_fast_clk
        fast_neg_count_reg_tmp = '0;
    end
    endtask

    task _work_slow_clk (input logic unused);
    begin: _work_slow_clk
        if (slow_enable_in) begin
            slow_count_reg_tmp = slow_count_reg + 'h1;
        end
    end
    endtask

    task _reset_pos_slow_clk ();
    begin: _reset_pos_slow_clk
        slow_count_reg_tmp = '0;
    end
    endtask

    generate  // _assign
    endgenerate

    always_ff @(posedge fast_clk or posedge reset) begin
        fast_count_reg_tmp = fast_count_reg;

        if (reset) begin
            _reset_pos_fast_clk();
        end
        else begin
            _work_fast_clk(reset);
        end

        fast_count_reg <= fast_count_reg_tmp;
    end

    always_ff @(negedge fast_clk or posedge reset) begin
        fast_neg_count_reg_tmp = fast_neg_count_reg;

        if (reset) begin
            _reset_neg_fast_clk();
        end
        else begin
            _work_neg_fast_clk(reset);
        end

        fast_neg_count_reg <= fast_neg_count_reg_tmp;
    end

    always_ff @(posedge slow_clk or posedge reset) begin
        slow_count_reg_tmp = slow_count_reg;

        if (reset) begin
            _reset_pos_slow_clk();
        end
        else begin
            _work_slow_clk(reset);
        end

        slow_count_reg <= slow_count_reg_tmp;
    end

    assign fast_count_out = fast_count_reg;

    assign fast_neg_count_out = fast_neg_count_reg;

    assign slow_count_out = slow_count_reg;


endmodule
