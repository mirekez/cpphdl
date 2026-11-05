`default_nettype none

import Predef_pkg::*;


module PipelinedReceiveFilter (
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
,   output wire[18-1:0] coefficient_out[24]
);
    localparam  OUTPUT_LATENCY = 'h6;
    localparam  UPDATE_LATENCY = 'hA;


    // regs and combs

    // members
    wire[8-1:0] core__sample_in;
    wire[12-1:0] core__local_in;
    wire[3-1:0] core__desired_in;
    wire[2-1:0] core__acquisition_in;
    wire core__train_in;
    wire core__advance_in;
    wire[8-1:0] core__sample_out;
    wire core__valid_out;
    wire core__fault_out;
    wire[18-1:0] core__coefficient_out[24];
    receive_filter_pipeline__ReceiveFilterPipelineCore      core (
        .clk(clk)
,       .reset(reset)
,       .sample_in(core__sample_in)
,       .local_in(core__local_in)
,       .desired_in(core__desired_in)
,       .acquisition_in(core__acquisition_in)
,       .train_in(core__train_in)
,       .advance_in(core__advance_in)
,       .sample_out(core__sample_out)
,       .valid_out(core__valid_out)
,       .fault_out(core__fault_out)
,       .coefficient_out(core__coefficient_out)
    );

    // tmp variables


    generate  // _assign
        genvar gi;
        assign core__sample_in = sample_in;
        assign core__local_in = local_in;
        assign core__desired_in = desired_in;
        assign core__train_in = ((train_in) != '0);
        assign core__acquisition_in = 'h0;
        assign core__advance_in = ((advance_in) != '0);
        for (gi = 32'h0;gi < 32'h18;gi=gi+1) begin
            assign coefficient_out[gi] = core__coefficient_out[gi];
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end

    assign sample_out = core__sample_out;

    assign valid_out = ((core__valid_out) != '0);

    assign fault_out = ((core__fault_out) != '0);


endmodule
