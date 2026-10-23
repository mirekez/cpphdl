`default_nettype none

import Predef_pkg::*;


module ReceiveFilter (
    input wire clk
,   input wire reset
,   input wire[8-1:0] sample_in
,   input wire[12-1:0] local_in
,   input wire[3-1:0] desired_in
,   input wire train_in
,   input wire advance_in
,   output wire[8-1:0] sample_out
,   output wire valid_out
,   output wire fault_out
);


    // regs and combs

    // members
    wire[8-1:0] filter__sample_in;
    wire[12-1:0] filter__local_in;
    wire[3-1:0] filter__desired_in;
    wire filter__train_in;
    wire filter__advance_in;
    wire[8-1:0] filter__sample_out;
    wire filter__valid_out;
    wire filter__fault_out;
    wire[18-1:0] filter__coefficient_out[24];
    PipelinedReceiveFilter      filter (
        .clk(clk)
,       .reset(reset)
,       .sample_in(filter__sample_in)
,       .local_in(filter__local_in)
,       .desired_in(filter__desired_in)
,       .train_in(filter__train_in)
,       .advance_in(filter__advance_in)
,       .sample_out(filter__sample_out)
,       .valid_out(filter__valid_out)
,       .fault_out(filter__fault_out)
,       .coefficient_out(filter__coefficient_out)
    );

    // tmp variables


    generate  // _assign
        assign filter__sample_in = sample_in;
        assign filter__local_in = local_in;
        assign filter__desired_in = desired_in;
        assign filter__train_in = ((train_in) != '0);
        assign filter__advance_in = ((advance_in) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end

    assign sample_out = filter__sample_out;

    assign valid_out = ((filter__valid_out) != '0);

    assign fault_out = ((filter__fault_out) != '0);


endmodule
