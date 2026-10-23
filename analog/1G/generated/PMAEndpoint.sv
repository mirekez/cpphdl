`default_nettype none

import Predef_pkg::*;


module PMAEndpoint (
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
);


    // regs and combs

    // members
    wire control__enable_in;
    wire control__restart_in;
    wire control__master_in;
    wire control__scrambler_ok_in;
    wire control__local_receiver_ok_in;
    wire control__remote_receiver_ok_in;
    wire control__transmit_active_in;
    wire control__tick_ms_in;
    wire[2-1:0] control__mode_out;
    wire control__link_out;
    wire control__timeout_out;
    wire control__master_out;
    wire[3-1:0] control__state_out;
    PMAControl      control (
        .clk(clk)
,       .reset(reset)
,       .enable_in(control__enable_in)
,       .restart_in(control__restart_in)
,       .master_in(control__master_in)
,       .scrambler_ok_in(control__scrambler_ok_in)
,       .local_receiver_ok_in(control__local_receiver_ok_in)
,       .remote_receiver_ok_in(control__remote_receiver_ok_in)
,       .transmit_active_in(control__transmit_active_in)
,       .tick_ms_in(control__tick_ms_in)
,       .mode_out(control__mode_out)
,       .link_out(control__link_out)
,       .timeout_out(control__timeout_out)
,       .master_out(control__master_out)
,       .state_out(control__state_out)
    );
    wire[8-1:0] tx__data_in;
    wire tx__enable_in;
    wire tx__error_in;
    wire tx__master_in;
    wire tx__link_in;
    wire tx__local_receiver_ok_in;
    wire[2-1:0] tx__mode_in;
    wire tx__seed_load_in;
    wire[33-1:0] tx__seed_in;
    wire[12-1:0] tx__symbols_out;
    wire tx__fault_out;
    PCSTransmitter      tx (
        .clk(clk)
,       .reset((reset || restart_in) || !enable_in)
,       .data_in(tx__data_in)
,       .enable_in(tx__enable_in)
,       .error_in(tx__error_in)
,       .master_in(tx__master_in)
,       .link_in(tx__link_in)
,       .local_receiver_ok_in(tx__local_receiver_ok_in)
,       .mode_in(tx__mode_in)
,       .seed_load_in(tx__seed_load_in)
,       .seed_in(tx__seed_in)
,       .symbols_out(tx__symbols_out)
,       .fault_out(tx__fault_out)
    );
    wire[32-1:0] rx__samples_in;
    wire rx__remote_master_in;
    wire rx__pma_ok_in;
    wire[10-1:0] rx__receive_out;
    wire rx__locked_out;
    wire rx__remote_receiver_ok_out;
    PCSReceiver      rx (
        .clk(clk)
,       .reset((reset || restart_in) || !enable_in)
,       .samples_in(rx__samples_in)
,       .remote_master_in(rx__remote_master_in)
,       .pma_ok_in(rx__pma_ok_in)
,       .receive_out(rx__receive_out)
,       .locked_out(rx__locked_out)
,       .remote_receiver_ok_out(rx__remote_receiver_ok_out)
    );

    // tmp variables


    generate  // _assign
        assign control__enable_in = ((enable_in) != '0);
        assign control__restart_in = ((restart_in) != '0);
        assign control__master_in = ((master_in) != '0);
        assign control__tick_ms_in = ((tick_ms_in) != '0);
        assign control__scrambler_ok_in = ((rx__locked_out) != '0);
        assign control__local_receiver_ok_in = (((rx__locked_out && samples_valid_in) && receiver_quality_in) != '0);
        assign control__remote_receiver_ok_in = ((rx__remote_receiver_ok_out) != '0);
        assign control__transmit_active_in = ((transmit_enable_in) != '0);
        assign tx__data_in = transmit_data_in;
        assign tx__enable_in = ((transmit_enable_in) != '0);
        assign tx__error_in = ((transmit_error_in) != '0);
        assign tx__master_in = ((control__master_out) != '0);
        assign tx__mode_in = control__mode_out;
        assign tx__link_in = ((control__mode_out == 64'h2) != '0);
        assign tx__local_receiver_ok_in = (((rx__locked_out && samples_valid_in) && receiver_quality_in) != '0);
        assign tx__seed_load_in = (((control__state_out == 64'h0) && enable_in) != '0);
        assign tx__seed_in = seed_in;
        assign rx__samples_in = samples_in;
        assign rx__remote_master_in = ((!control__master_out) != '0);
        assign rx__pma_ok_in = (((enable_in && !restart_in) && samples_valid_in) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end

    assign symbols_out = tx__symbols_out;

    assign receive_out = rx__receive_out;

    assign link_out = ((control__link_out) != '0);

    assign timeout_out = ((control__timeout_out) != '0);

    assign locked_out = ((rx__locked_out) != '0);

    assign mode_out = control__mode_out;


endmodule
