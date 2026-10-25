`default_nettype none

import Predef_pkg::*;


module ReceiveSamplePCS (
    input wire clk
,   input wire reset
,   input wire[32-1:0] samples_in
,   input wire[16-1:0] tag_in
,   input wire valid_in
,   input wire restart_in
,   input wire[25-1:0] initial_phase_in
,   input wire[16-1:0] origin_in
,   input wire remote_master_in
,   input wire pma_ok_in
,   output wire[10-1:0] receive_out
,   output wire receive_valid_out
,   output wire locked_out
,   output wire fault_out
,   output wire symbol_valid_out
,   output wire[16-1:0] symbol_tag_out
,   output wire[24-1:0] symbol_fraction_out
);


    // regs and combs

    // members
    wire[32-1:0] recovery__samples_in;
    wire[16-1:0] recovery__tag_in;
    wire recovery__valid_in;
    wire recovery__track_in;
    wire recovery__restart_in;
    wire[25-1:0] recovery__initial_phase_in;
    wire[16-1:0] recovery__origin_in;
    wire[32-1:0] recovery__samples_out;
    wire[16-1:0] recovery__tag_out;
    wire[24-1:0] recovery__fraction_out;
    wire recovery__valid_out;
    wire recovery__fault_out;
    wire recovery__clipped_out;
    wire[25-1:0] recovery__interval_out;
    wire[25-1:0] recovery__period_out;
    wire[14-1:0] recovery__error_out;
    wire recovery__correction_valid_out;
    ReceiveSampleRecovery      recovery (
        .clk(clk)
,       .reset(reset)
,       .samples_in(recovery__samples_in)
,       .tag_in(recovery__tag_in)
,       .valid_in(recovery__valid_in)
,       .track_in(recovery__track_in)
,       .restart_in(recovery__restart_in)
,       .initial_phase_in(recovery__initial_phase_in)
,       .origin_in(recovery__origin_in)
,       .samples_out(recovery__samples_out)
,       .tag_out(recovery__tag_out)
,       .fraction_out(recovery__fraction_out)
,       .valid_out(recovery__valid_out)
,       .fault_out(recovery__fault_out)
,       .clipped_out(recovery__clipped_out)
,       .interval_out(recovery__interval_out)
,       .period_out(recovery__period_out)
,       .error_out(recovery__error_out)
,       .correction_valid_out(recovery__correction_valid_out)
    );
    wire[32-1:0] pcs__samples_in;
    wire pcs__symbol_enable_in;
    wire pcs__receive_valid_out;
    wire pcs__remote_master_in;
    wire pcs__pma_ok_in;
    wire[10-1:0] pcs__receive_out;
    wire pcs__locked_out;
    wire pcs__remote_receiver_ok_out;
    PCSReceiver      pcs (
        .clk(clk)
,       .reset(reset || restart_in)
,       .samples_in(pcs__samples_in)
,       .symbol_enable_in(pcs__symbol_enable_in)
,       .receive_valid_out(pcs__receive_valid_out)
,       .remote_master_in(pcs__remote_master_in)
,       .pma_ok_in(pcs__pma_ok_in)
,       .receive_out(pcs__receive_out)
,       .locked_out(pcs__locked_out)
,       .remote_receiver_ok_out(pcs__remote_receiver_ok_out)
    );

    // tmp variables


    generate  // _assign
        assign recovery__samples_in = samples_in;
        assign recovery__tag_in = tag_in;
        assign recovery__valid_in = ((valid_in) != '0);
        assign recovery__track_in = ((1) != '0);
        assign recovery__restart_in = ((restart_in) != '0);
        assign recovery__initial_phase_in = initial_phase_in;
        assign recovery__origin_in = origin_in;
        assign pcs__samples_in = recovery__samples_out;
        assign pcs__symbol_enable_in = ((recovery__valid_out && !recovery__fault_out) != '0);
        assign pcs__remote_master_in = ((remote_master_in) != '0);
        assign pcs__pma_ok_in = ((pma_ok_in && !recovery__fault_out) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end

    assign receive_out = pcs__receive_out;

    assign receive_valid_out = ((pcs__receive_valid_out) != '0);

    assign locked_out = ((pcs__locked_out) != '0);

    assign fault_out = ((recovery__fault_out) != '0);

    assign symbol_valid_out = ((recovery__valid_out) != '0);

    assign symbol_tag_out = recovery__tag_out;

    assign symbol_fraction_out = recovery__fraction_out;


endmodule
