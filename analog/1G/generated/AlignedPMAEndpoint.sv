`default_nettype none

import Predef_pkg::*;


module AlignedPMAEndpoint (
    input wire clk
,   input wire reset
,   input wire enable_in
,   input wire restart_in
,   input wire master_in
,   input wire tick_ms_in
,   input wire samples_valid_in
,   input wire receiver_quality_in
,   input wire[32-1:0] samples_in
,   input wire[33-1:0] seed_in
,   input wire[8-1:0] transmit_data_in
,   input wire transmit_enable_in
,   input wire transmit_error_in
,   output wire[12-1:0] symbols_out
,   output wire[10-1:0] receive_out
,   output wire link_out
,   output wire timeout_out
,   output wire locked_out
,   output wire[2-1:0] mode_out
,   output wire pairs_locked_out
,   output wire[8-1:0] pair_mapping_out
,   output wire[20-1:0] pair_delays_out
);


    // regs and combs

    // members
    wire[32-1:0] pairs__samples_in;
    wire pairs__enable_in;
    wire pairs__restart_in;
    wire pairs__signal_ok_in;
    wire pairs__advance_in;
    wire pairs__remote_master_in;
    wire[32-1:0] pairs__samples_out;
    wire pairs__valid_out;
    wire pairs__locked_out;
    wire pairs__ambiguous_out;
    wire[2-1:0] pairs__state_out;
    wire[8-1:0] pairs__mapping_out;
    wire[20-1:0] pairs__delays_out;
    ReceivePairAcquirer      pairs (
        .clk(clk)
,       .reset(reset)
,       .samples_in(pairs__samples_in)
,       .enable_in(pairs__enable_in)
,       .restart_in(pairs__restart_in)
,       .signal_ok_in(pairs__signal_ok_in)
,       .advance_in(pairs__advance_in)
,       .remote_master_in(pairs__remote_master_in)
,       .samples_out(pairs__samples_out)
,       .valid_out(pairs__valid_out)
,       .locked_out(pairs__locked_out)
,       .ambiguous_out(pairs__ambiguous_out)
,       .state_out(pairs__state_out)
,       .mapping_out(pairs__mapping_out)
,       .delays_out(pairs__delays_out)
    );
    wire endpoint__enable_in;
    wire endpoint__restart_in;
    wire endpoint__master_in;
    wire endpoint__tick_ms_in;
    wire endpoint__samples_valid_in;
    wire endpoint__receiver_quality_in;
    wire[32-1:0] endpoint__samples_in;
    wire[33-1:0] endpoint__seed_in;
    wire[8-1:0] endpoint__transmit_data_in;
    wire endpoint__transmit_enable_in;
    wire endpoint__transmit_error_in;
    wire[12-1:0] endpoint__symbols_out;
    wire[10-1:0] endpoint__receive_out;
    wire endpoint__link_out;
    wire endpoint__timeout_out;
    wire endpoint__locked_out;
    wire[2-1:0] endpoint__mode_out;
    PMAEndpoint      endpoint (
        .clk(clk)
,       .reset(reset)
,       .enable_in(endpoint__enable_in)
,       .restart_in(endpoint__restart_in)
,       .master_in(endpoint__master_in)
,       .tick_ms_in(endpoint__tick_ms_in)
,       .samples_valid_in(endpoint__samples_valid_in)
,       .receiver_quality_in(endpoint__receiver_quality_in)
,       .samples_in(endpoint__samples_in)
,       .seed_in(endpoint__seed_in)
,       .transmit_data_in(endpoint__transmit_data_in)
,       .transmit_enable_in(endpoint__transmit_enable_in)
,       .transmit_error_in(endpoint__transmit_error_in)
,       .symbols_out(endpoint__symbols_out)
,       .receive_out(endpoint__receive_out)
,       .link_out(endpoint__link_out)
,       .timeout_out(endpoint__timeout_out)
,       .locked_out(endpoint__locked_out)
,       .mode_out(endpoint__mode_out)
    );

    // tmp variables


    generate  // _assign
        assign pairs__samples_in = samples_in;
        assign pairs__enable_in = ((enable_in) != '0);
        assign pairs__restart_in = ((restart_in) != '0);
        assign pairs__signal_ok_in = ((receiver_quality_in && samples_valid_in) != '0);
        assign pairs__advance_in = ((samples_valid_in) != '0);
        assign pairs__remote_master_in = ((!master_in) != '0);
        assign endpoint__enable_in = ((enable_in) != '0);
        assign endpoint__restart_in = ((restart_in) != '0);
        assign endpoint__master_in = ((master_in) != '0);
        assign endpoint__tick_ms_in = ((tick_ms_in) != '0);
        assign endpoint__samples_valid_in = ((pairs__valid_out) != '0);
        assign endpoint__receiver_quality_in = ((receiver_quality_in && pairs__locked_out) != '0);
        assign endpoint__samples_in = pairs__samples_out;
        assign endpoint__seed_in = seed_in;
        assign endpoint__transmit_data_in = transmit_data_in;
        assign endpoint__transmit_enable_in = ((transmit_enable_in) != '0);
        assign endpoint__transmit_error_in = ((transmit_error_in) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end

    assign symbols_out = endpoint__symbols_out;

    assign receive_out = endpoint__receive_out;

    assign link_out = ((endpoint__link_out) != '0);

    assign timeout_out = ((endpoint__timeout_out) != '0);

    assign locked_out = ((endpoint__locked_out) != '0);

    assign mode_out = endpoint__mode_out;

    assign pairs_locked_out = ((pairs__locked_out) != '0);

    assign pair_mapping_out = pairs__mapping_out;

    assign pair_delays_out = pairs__delays_out;


endmodule
