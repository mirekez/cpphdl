`default_nettype none

import Predef_pkg::*;


module DelayedRbMapTop (
    input wire clk
,   input wire reset
,   input wire command_valid_in
,   input wire[31:0] operation_in
,   input wire[31:0] index_in
,   input wire[31:0] value_in
,   output wire command_ready_out
,   input wire response_ready_in
,   output wire response_valid_out
,   output wire[63:0] result_out
,   output wire[31:0] fault_out
);


    // regs and combs

    // members
    wire worker__command_valid_in;
    wire[31:0] worker__operation_in;
    wire[31:0] worker__index_in;
    wire[31:0] worker__value_in;
    wire worker__command_ready_out;
    wire worker__response_ready_in;
    wire worker__response_valid_out;
    wire[63:0] worker__result_out;
    wire[31:0] worker__fault_out;
    cpphdl_hls_ClockedDelayerRbMapMethods_A16_M1      worker (
        .clk(clk)
,       .reset(reset)
,       .command_valid_in(worker__command_valid_in)
,       .operation_in(worker__operation_in)
,       .index_in(worker__index_in)
,       .value_in(worker__value_in)
,       .command_ready_out(worker__command_ready_out)
,       .response_ready_in(worker__response_ready_in)
,       .response_valid_out(worker__response_valid_out)
,       .result_out(worker__result_out)
,       .fault_out(worker__fault_out)
    );

    // tmp variables


    generate  // _assign
        assign worker__command_valid_in = ((command_valid_in) != '0);
        assign worker__operation_in = operation_in;
        assign worker__index_in = index_in;
        assign worker__value_in = value_in;
        assign worker__response_ready_in = ((response_ready_in) != '0);
        assign command_ready_out = ((worker__command_ready_out) != '0);
        assign response_valid_out = ((worker__response_valid_out) != '0);
        assign result_out = worker__result_out;
        assign fault_out = worker__fault_out;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule
