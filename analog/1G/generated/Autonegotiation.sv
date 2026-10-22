`default_nettype none

import Predef_pkg::*;


module Autonegotiation (
    input wire clk
,   input wire reset
,   input wire pulse_in
,   input wire enable_in
,   input wire restart_in
,   input wire pma_link_in
,   input wire[11-1:0] local_up1_in
,   input wire[11-1:0] entropy_seed_in
,   output wire pulse_out
,   output wire pma_enable_out
,   output wire link_out
,   output wire master_out
,   output wire fault_out
,   output wire[3-1:0] reason_out
);


    // regs and combs

    // members
    wire timebase__tick_out;
    ReferenceMillisecond      timebase (
        .clk(clk)
,       .reset((reset || restart_in) || !enable_in)
,       .tick_out(timebase__tick_out)
    );
    wire[16-1:0] tx__page_in;
    wire tx__enable_in;
    wire tx__restart_in;
    wire tx__pulse_out;
    wire tx__burst_begin_out;
    wire tx__burst_done_out;
    FLPTransmitter      tx (
        .clk(clk)
,       .reset(reset)
,       .page_in(tx__page_in)
,       .enable_in(tx__enable_in)
,       .restart_in(tx__restart_in)
,       .pulse_out(tx__pulse_out)
,       .burst_begin_out(tx__burst_begin_out)
,       .burst_done_out(tx__burst_done_out)
    );
    wire rx__pulse_in;
    wire rx__enable_in;
    wire[16-1:0] rx__page_out;
    wire rx__valid_out;
    wire rx__fault_out;
    FLPReceiver      rx (
        .clk(clk)
,       .reset(reset)
,       .pulse_in(rx__pulse_in)
,       .enable_in(rx__enable_in)
,       .page_out(rx__page_out)
,       .valid_out(rx__valid_out)
,       .fault_out(rx__fault_out)
    );
    wire[16-1:0] control__receive_page_in;
    wire control__receive_valid_in;
    wire control__receive_fault_in;
    wire control__transmit_burst_in;
    wire control__tick_ms_in;
    wire control__enable_in;
    wire control__restart_in;
    wire control__pma_link_in;
    wire[11-1:0] control__local_up1_in;
    wire[11-1:0] control__entropy_seed_in;
    wire[16-1:0] control__transmit_page_out;
    wire control__transmit_enable_out;
    wire control__pma_enable_out;
    wire control__link_out;
    wire control__master_out;
    wire control__fault_out;
    wire[3-1:0] control__reason_out;
    wire control__retry_out;
    Autonegotiator      control (
        .clk(clk)
,       .reset(reset)
,       .receive_page_in(control__receive_page_in)
,       .receive_valid_in(control__receive_valid_in)
,       .receive_fault_in(control__receive_fault_in)
,       .transmit_burst_in(control__transmit_burst_in)
,       .tick_ms_in(control__tick_ms_in)
,       .enable_in(control__enable_in)
,       .restart_in(control__restart_in)
,       .pma_link_in(control__pma_link_in)
,       .local_up1_in(control__local_up1_in)
,       .entropy_seed_in(control__entropy_seed_in)
,       .transmit_page_out(control__transmit_page_out)
,       .transmit_enable_out(control__transmit_enable_out)
,       .pma_enable_out(control__pma_enable_out)
,       .link_out(control__link_out)
,       .master_out(control__master_out)
,       .fault_out(control__fault_out)
,       .reason_out(control__reason_out)
,       .retry_out(control__retry_out)
    );

    // tmp variables


    generate  // _assign
        assign tx__page_in = control__transmit_page_out;
        assign tx__enable_in = ((control__transmit_enable_out) != '0);
        assign tx__restart_in = ((restart_in) != '0);
        assign rx__pulse_in = ((pulse_in) != '0);
        assign rx__enable_in = ((control__transmit_enable_out) != '0);
        assign control__receive_page_in = rx__page_out;
        assign control__receive_valid_in = ((rx__valid_out) != '0);
        assign control__receive_fault_in = ((rx__fault_out) != '0);
        assign control__transmit_burst_in = ((tx__burst_done_out) != '0);
        assign control__tick_ms_in = ((timebase__tick_out) != '0);
        assign control__enable_in = ((enable_in) != '0);
        assign control__restart_in = ((restart_in) != '0);
        assign control__pma_link_in = ((pma_link_in) != '0);
        assign control__local_up1_in = local_up1_in;
        assign control__entropy_seed_in = entropy_seed_in;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end

    assign pulse_out = ((tx__pulse_out) != '0);

    assign pma_enable_out = ((control__pma_enable_out) != '0);

    assign link_out = ((control__link_out) != '0);

    assign master_out = ((control__master_out) != '0);

    assign fault_out = ((control__fault_out) != '0);

    assign reason_out = control__reason_out;


endmodule
