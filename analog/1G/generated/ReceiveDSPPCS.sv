`default_nettype none

import Predef_pkg::*;


module ReceiveDSPPCS (
    input wire clk
,   input wire reset
,   input wire[32-1:0] samples_in
,   input wire[12-1:0] local_in
,   input wire[12-1:0] desired_in
,   input wire[16-1:0] tag_in
,   input wire clipped_in
,   input wire train_in
,   input wire valid_in
,   input wire enable_in
,   input wire restart_in
,   input wire signal_ready_in
,   input wire transport_fault_in
,   input wire remote_master_in
,   output wire[10-1:0] receive_out
,   output wire receive_valid_out
,   output wire locked_out
,   output wire quality_out
,   output wire remote_receiver_ok_out
,   output wire fault_out
,   output wire pairs_locked_out
,   output wire[8-1:0] pair_mapping_out
,   output wire[20-1:0] pair_delays_out
,   output wire training_busy_out
);


    // regs and combs
    logic frontend_available_comb;
    logic acquisition_available_comb;
    logic receiver_available_comb;

    // members
    wire[32-1:0] dsp__samples_in;
    wire[12-1:0] dsp__local_in;
    wire[12-1:0] dsp__desired_in;
    wire[16-1:0] dsp__tag_in;
    wire dsp__clipped_in;
    wire dsp__train_in;
    wire dsp__valid_in;
    wire dsp__restart_in;
    wire[32-1:0] dsp__samples_out;
    wire[16-1:0] dsp__tag_out;
    wire dsp__valid_out;
    wire dsp__fault_out;
    wire dsp__clipped_out;
    wire dsp__training_busy_out;
    ReceiveDSP      dsp (
        .clk(clk)
,       .reset(((reset || restart_in) || !enable_in) || transport_fault_in)
,       .samples_in(dsp__samples_in)
,       .local_in(dsp__local_in)
,       .desired_in(dsp__desired_in)
,       .tag_in(dsp__tag_in)
,       .clipped_in(dsp__clipped_in)
,       .train_in(dsp__train_in)
,       .valid_in(dsp__valid_in)
,       .restart_in(dsp__restart_in)
,       .samples_out(dsp__samples_out)
,       .tag_out(dsp__tag_out)
,       .valid_out(dsp__valid_out)
,       .fault_out(dsp__fault_out)
,       .clipped_out(dsp__clipped_out)
,       .training_busy_out(dsp__training_busy_out)
    );
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
,       .reset(((reset || restart_in) || !enable_in) || transport_fault_in)
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
,       .reset(reset || !receiver_available_comb)
,       .samples_in(pcs__samples_in)
,       .symbol_enable_in(pcs__symbol_enable_in)
,       .receive_valid_out(pcs__receive_valid_out)
,       .remote_master_in(pcs__remote_master_in)
,       .pma_ok_in(pcs__pma_ok_in)
,       .receive_out(pcs__receive_out)
,       .locked_out(pcs__locked_out)
,       .remote_receiver_ok_out(pcs__remote_receiver_ok_out)
    );
    wire[10-1:0] quality__receive_in;
    wire quality__valid_in;
    wire quality__locked_in;
    wire quality__fault_in;
    wire quality__clipped_in;
    wire quality__enable_in;
    wire quality__ready_out;
    wire[6-1:0] quality__flushed_out;
    wire[7-1:0] quality__idles_out;
    ReceiveQuality      quality (
        .clk(clk)
,       .reset(((reset || restart_in) || !enable_in) || transport_fault_in)
,       .receive_in(quality__receive_in)
,       .valid_in(quality__valid_in)
,       .locked_in(quality__locked_in)
,       .fault_in(quality__fault_in)
,       .clipped_in(quality__clipped_in)
,       .enable_in(quality__enable_in)
,       .ready_out(quality__ready_out)
,       .flushed_out(quality__flushed_out)
,       .idles_out(quality__idles_out)
    );

    // tmp variables


    always_comb begin : frontend_available_comb_func  // frontend_available_comb_func
        frontend_available_comb=(((enable_in && !restart_in) && signal_ready_in) && !transport_fault_in) && !dsp__fault_out;
    end

    always_comb begin : acquisition_available_comb_func  // acquisition_available_comb_func
        acquisition_available_comb=(frontend_available_comb && !train_in) && !dsp__training_busy_out;
    end

    always_comb begin : receiver_available_comb_func  // receiver_available_comb_func
        receiver_available_comb=acquisition_available_comb && pairs__locked_out;
    end

    generate  // _assign
        assign dsp__samples_in = samples_in;
        assign dsp__local_in = local_in;
        assign dsp__desired_in = desired_in;
        assign dsp__tag_in = tag_in;
        assign dsp__clipped_in = ((clipped_in) != '0);
        assign dsp__train_in = ((train_in) != '0);
        assign dsp__valid_in = ((((valid_in && enable_in) && signal_ready_in) && !transport_fault_in) != '0);
        assign dsp__restart_in = ((((restart_in || !enable_in) || !signal_ready_in) || transport_fault_in) != '0);
        assign pairs__samples_in = dsp__samples_out;
        assign pairs__enable_in = ((enable_in) != '0);
        assign pairs__restart_in = ((restart_in) != '0);
        assign pairs__signal_ok_in = ((acquisition_available_comb) != '0);
        assign pairs__advance_in = ((dsp__valid_out) != '0);
        assign pairs__remote_master_in = ((remote_master_in) != '0);
        assign pcs__samples_in = pairs__samples_out;
        assign pcs__symbol_enable_in = ((pairs__valid_out && receiver_available_comb) != '0);
        assign pcs__remote_master_in = ((remote_master_in) != '0);
        assign pcs__pma_ok_in = ((receiver_available_comb) != '0);
        assign quality__receive_in = pcs__receive_out;
        assign quality__valid_in = ((pcs__receive_valid_out) != '0);
        assign quality__locked_in = ((pcs__locked_out) != '0);
        assign quality__fault_in = ((transport_fault_in || dsp__fault_out) != '0);
        assign quality__clipped_in = ((dsp__valid_out && dsp__clipped_out) != '0);
        assign quality__enable_in = (((receiver_available_comb && !train_in) && !dsp__training_busy_out) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end

    assign receive_out = pcs__receive_out;

    assign receive_valid_out = ((pcs__receive_valid_out && receiver_available_comb) != '0);

    assign locked_out = ((pcs__locked_out && receiver_available_comb) != '0);

    assign quality_out = ((quality__ready_out && receiver_available_comb) != '0);

    assign remote_receiver_ok_out = ((pcs__remote_receiver_ok_out && receiver_available_comb) != '0);

    assign fault_out = ((transport_fault_in || dsp__fault_out) != '0);

    assign pairs_locked_out = ((pairs__locked_out && acquisition_available_comb) != '0);

    assign pair_mapping_out = pairs__mapping_out;

    assign pair_delays_out = pairs__delays_out;

    assign training_busy_out = ((dsp__training_busy_out) != '0);


endmodule
