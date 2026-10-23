`default_nettype none

import Predef_pkg::*;


module ReceiveRangeCalibration (
    input wire clk
,   input wire reset
,   input wire enable_in
,   input wire start_in
,   input wire[8-1:0] centre_in
,   input wire[8-1:0] span_in
,   input wire[8-1:0] target_span_in
,   output wire[8-1:0] offset_out
,   output wire[16-1:0] gain_out
,   output wire ready_out
,   output wire busy_out
,   output wire fault_out
);


    // regs and combs
    reg[16-1:0] numerator;
    reg[16-1:0] quotient;
    reg[16-1:0] gain;
    reg[9-1:0] remainder;
    reg[8-1:0] denominator;
    reg[8-1:0] pending_offset;
    reg[8-1:0] offset;
    reg[5-1:0] remaining;
    reg busy;
    reg ready;
    reg fault;

    // members

    // tmp variables
    logic[16-1:0] numerator_tmp;
    logic[16-1:0] quotient_tmp;
    logic[16-1:0] gain_tmp;
    logic[9-1:0] remainder_tmp;
    logic[8-1:0] denominator_tmp;
    logic[8-1:0] pending_offset_tmp;
    logic[8-1:0] offset_tmp;
    logic[5-1:0] remaining_tmp;
    logic busy_tmp;
    logic ready_tmp;
    logic fault_tmp;


    generate  // _assign
        assign offset_out = offset;
        assign gain_out = gain;
        assign ready_out = ready;
        assign busy_out = busy;
        assign fault_out = fault;
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] next_remainder;
        logic[31:0] next_quotient;
        next_remainder=((unsigned'(32'(remainder)) <<< 'h1)) | ((unsigned'(32'(numerator)) >>> 32'shF));
        next_quotient=unsigned'(32'(quotient)) <<< 'h1;
        if (((busy) != '0) && !start_in) begin
            if (next_remainder>=unsigned'(32'(denominator))) begin
                next_remainder-=unsigned'(32'(denominator));
                next_quotient|=32'h1;
            end
            numerator_tmp = numerator <<< 'h1;
            remainder_tmp = next_remainder;
            quotient_tmp = next_quotient;
            remaining_tmp = remaining - 64'h1;
            if (remaining == 64'h1) begin
                offset_tmp = pending_offset;
                gain_tmp = next_quotient;
                busy_tmp = (('h0) != '0);
                ready_tmp = (('h1) != '0);
            end
        end
        if (start_in) begin
            pending_offset_tmp = centre_in;
            denominator_tmp = span_in;
            numerator_tmp = 32'(((unsigned'(32'(target_span_in)) <<< 'h8)) + ((unsigned'(32'(span_in)) >>> 32'sh1)));
            quotient_tmp = 64'h0;
            remainder_tmp = 64'h0;
            remaining_tmp = 64'h10;
            busy_tmp = (('h1) != '0);
            ready_tmp = (('h0) != '0);
            fault_tmp = (('h0) != '0);
            if ((span_in == 64'h0) || (target_span_in == 64'h0)) begin
                busy_tmp = (('h0) != '0);
                remaining_tmp = 64'h0;
                fault_tmp = (('h1) != '0);
                offset_tmp = 64'h80;
                gain_tmp = 64'h100;
            end
        end
        if (reset || !enable_in) begin
            numerator_tmp = 64'h0;
            quotient_tmp = 64'h0;
            remainder_tmp = 64'h0;
            denominator_tmp = 64'h0;
            pending_offset_tmp = 64'h80;
            offset_tmp = 64'h80;
            gain_tmp = 64'h100;
            remaining_tmp = 64'h0;
            busy_tmp = (('h0) != '0);
            ready_tmp = (('h0) != '0);
            fault_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        numerator_tmp = numerator;
        quotient_tmp = quotient;
        gain_tmp = gain;
        remainder_tmp = remainder;
        denominator_tmp = denominator;
        pending_offset_tmp = pending_offset;
        offset_tmp = offset;
        remaining_tmp = remaining;
        busy_tmp = busy;
        ready_tmp = ready;
        fault_tmp = fault;

            _work(reset);

        numerator <= numerator_tmp;
        quotient <= quotient_tmp;
        gain <= gain_tmp;
        remainder <= remainder_tmp;
        denominator <= denominator_tmp;
        pending_offset <= pending_offset_tmp;
        offset <= offset_tmp;
        remaining <= remaining_tmp;
        busy <= busy_tmp;
        ready <= ready_tmp;
        fault <= fault_tmp;
    end


endmodule
