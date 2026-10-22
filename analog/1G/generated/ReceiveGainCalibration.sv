`default_nettype none

import Predef_pkg::*;


module ReceiveGainCalibration (
    input wire clk
,   input wire reset
,   input wire enable_in
,   input wire start_in
,   input wire valid_in
,   input wire finish_in
,   input wire[8-1:0] sample_in
,   input wire[3-1:0] level_in
,   output wire[8-1:0] offset_out
,   output wire[16-1:0] gain_out
,   output wire ready_out
,   output wire fault_out
,   output wire busy_out
,   output wire collecting_out
);


    // regs and combs
    reg[24-1:0] sums[5];
    reg[24-1:0] span;
    reg[16-1:0] counts[5];
    reg[16-1:0] population;
    reg[16-1:0] gain;
    reg[8-1:0] offset;
    reg[8-1:0] pending_offset;
    reg[32-1:0] numerator;
    reg[32-1:0] quotient;
    reg[25-1:0] remainder;
    reg[25-1:0] denominator;
    reg[6-1:0] remaining;
    reg[2-1:0] phase;
    reg ready;
    reg fault;

    // members

    // tmp variables
    logic[24-1:0] sums_tmp[5];
    logic[24-1:0] span_tmp;
    logic[16-1:0] counts_tmp[5];
    logic[16-1:0] population_tmp;
    logic[16-1:0] gain_tmp;
    logic[8-1:0] offset_tmp;
    logic[8-1:0] pending_offset_tmp;
    logic[32-1:0] numerator_tmp;
    logic[32-1:0] quotient_tmp;
    logic[25-1:0] remainder_tmp;
    logic[25-1:0] denominator_tmp;
    logic[6-1:0] remaining_tmp;
    logic[2-1:0] phase_tmp;
    logic ready_tmp;
    logic fault_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic balanced;
        logic[31:0] index;
        logic[31:0] next_remainder;
        logic[31:0] next_quotient;
        index=level_in;
        next_remainder=32'h0;
        next_quotient=32'h0;
        balanced=counts['h0] != 64'h0;
        if (phase == 64'h1) begin
            if (finish_in) begin
                for (logic[31:0] i = 32'h1;i < 32'h5;i=i+1) begin
                    balanced=(balanced && (counts[i] == counts['h0])) && (sums[i] > sums[(i - 32'h1)]);
                end
                if (!balanced || valid_in) begin
                    fault_tmp = (('h1) != '0);
                    phase_tmp = 64'h0;
                end
                else begin
                    population_tmp = counts['h0];
                    span_tmp = sums['h4] - sums['h0];
                    numerator_tmp = 32'(unsigned'(32'(sums['h2])) + ((unsigned'(32'(counts['h0])) >>> 32'sh1)));
                    denominator_tmp = counts['h0];
                    quotient_tmp = 64'h0;
                    remainder_tmp = 64'h0;
                    remaining_tmp = 64'h20;
                    phase_tmp = 64'h2;
                end
            end
            else begin
                if (valid_in) begin
                    if ((index > 32'h4) || ((index<=32'h4 && (counts[index] == 64'hFFFF)))) begin
                        fault_tmp = (('h1) != '0);
                        phase_tmp = 64'h0;
                    end
                    else begin
                        counts_tmp[index] = counts[index] + 64'h1;
                        sums_tmp[index] = 32'(unsigned'(32'(sums[index])) + unsigned'(32'(sample_in)));
                    end
                end
            end
        end
        else begin
            if ((phase == 64'h2) || (phase == 64'h3)) begin
                next_remainder=((unsigned'(32'(remainder)) <<< 'h1)) | ((unsigned'(32'(numerator)) >>> 32'sh1F));
                next_quotient=unsigned'(32'(quotient)) <<< 'h1;
                if (next_remainder>=unsigned'(32'(denominator))) begin
                    next_remainder-=unsigned'(32'(denominator));
                    next_quotient|=32'h1;
                end
                numerator_tmp = numerator <<< 'h1;
                remainder_tmp = next_remainder;
                quotient_tmp = next_quotient;
                remaining_tmp = remaining - 64'h1;
                if (remaining == 64'h1) begin
                    if (phase == 64'h2) begin
                        pending_offset_tmp = next_quotient;
                        numerator_tmp = 32'(((unsigned'(32'(population)) <<< 'hF)) + ((unsigned'(32'(span)) >>> 32'sh1)));
                        denominator_tmp = span;
                        quotient_tmp = 64'h0;
                        remainder_tmp = 64'h0;
                        remaining_tmp = 64'h20;
                        phase_tmp = 64'h3;
                    end
                    else begin
                        phase_tmp = 64'h0;
                        if ((next_quotient == 32'h0) || (next_quotient > 32'hFFFF)) begin
                            fault_tmp = (('h1) != '0);
                        end
                        else begin
                            offset_tmp = pending_offset;
                            gain_tmp = next_quotient;
                            ready_tmp = (('h1) != '0);
                        end
                    end
                end
            end
        end
        if (start_in) begin
            for (logic[31:0] i = 32'h0;i < 32'h5;i=i+1) begin
                sums_tmp[i] = 64'h0;
                counts_tmp[i] = 64'h0;
            end
            phase_tmp = 64'h1;
            ready_tmp = (('h0) != '0);
            fault_tmp = (('h0) != '0);
        end
        if (reset || !enable_in) begin
            phase_tmp = 64'h0;
            ready_tmp = (('h0) != '0);
            fault_tmp = (('h0) != '0);
            offset_tmp = 64'h80;
            gain_tmp = 64'h100;
        end
    end
    endtask

    always @(posedge clk) begin
        sums_tmp = sums;
        span_tmp = span;
        counts_tmp = counts;
        population_tmp = population;
        gain_tmp = gain;
        offset_tmp = offset;
        pending_offset_tmp = pending_offset;
        numerator_tmp = numerator;
        quotient_tmp = quotient;
        remainder_tmp = remainder;
        denominator_tmp = denominator;
        remaining_tmp = remaining;
        phase_tmp = phase;
        ready_tmp = ready;
        fault_tmp = fault;

            _work(reset);

        sums <= sums_tmp;
        span <= span_tmp;
        counts <= counts_tmp;
        population <= population_tmp;
        gain <= gain_tmp;
        offset <= offset_tmp;
        pending_offset <= pending_offset_tmp;
        numerator <= numerator_tmp;
        quotient <= quotient_tmp;
        remainder <= remainder_tmp;
        denominator <= denominator_tmp;
        remaining <= remaining_tmp;
        phase <= phase_tmp;
        ready <= ready_tmp;
        fault <= fault_tmp;
    end

    assign offset_out = offset;

    assign gain_out = gain;

    assign ready_out = ready;

    assign fault_out = fault;

    assign busy_out = ((phase != 64'h0) != '0);

    assign collecting_out = ((phase == 64'h1) != '0);


endmodule
