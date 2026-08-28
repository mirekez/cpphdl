`default_nettype none

import Predef_pkg::*;


module LazyCycleOrderRoot (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[1-1:0] _output;
    logic[1-1:0] inactive_input;
    logic[1-1:0] work_value;
    logic[1-1:0] work_output;
    logic[1-1:0] use_inactive_input;
    logic[31:0] evaluations;
    logic[1-1:0] producer_comb;

    // members
    LazyCycleChild      child (
        .clk(clk)
,       .reset(reset)
    );

    // tmp variables


    always_comb begin : producer_comb_func  // producer_comb_func
        evaluations=evaluations+1;
        if (use_inactive_input) begin
            producer_comb = inactive_input;
        end
        else begin
            producer_comb = 'h1 & ~(child___output);
        end
    end

    generate  // _assign
        assign child___input = producer_comb;
        assign _output = child___output;
    endgenerate

    task _work (input logic unused);
    begin: _work
        work_value = producer_comb;
        work_output = child___output;
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule
