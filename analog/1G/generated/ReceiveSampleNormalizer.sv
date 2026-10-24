`default_nettype none

import Predef_pkg::*;


module ReceiveSampleNormalizer (
    input wire clk
,   input wire reset
,   input wire[8-1:0] sample_in
,   input wire[8-1:0] offset_in
,   input wire[16-1:0] gain_in
,   input wire valid_in
,   input wire enable_in
,   output wire[8-1:0] sample_out
,   output wire valid_out
,   output wire clipped_out
);


    // regs and combs
    reg[8-1:0] raw_sample;
    reg[8-1:0] raw_offset;
    reg[16-1:0] raw_gain;
    reg[9-1:0] centered;
    reg[16-1:0] calibration_gain;
    reg[11-1:0] half_products[8];
    reg[13-1:0] partial[4];
    reg[17-1:0] low;
    reg[17-1:0] high;
    reg[17-1:0] integral;
    reg[8-1:0] fraction;
    reg[8-1:0] rounded;
    reg rounded_low;
    reg rounded_high;
    reg[7-1:0] accepted;
    reg[8-1:0] sample;
    reg valid;
    reg clipped;

    // members

    // tmp variables
    logic[8-1:0] raw_sample_tmp;
    logic[8-1:0] raw_offset_tmp;
    logic[16-1:0] raw_gain_tmp;
    logic[9-1:0] centered_tmp;
    logic[16-1:0] calibration_gain_tmp;
    logic[11-1:0] half_products_tmp[8];
    logic[13-1:0] partial_tmp[4];
    logic[17-1:0] low_tmp;
    logic[17-1:0] high_tmp;
    logic[17-1:0] integral_tmp;
    logic[8-1:0] fraction_tmp;
    logic[8-1:0] rounded_tmp;
    logic rounded_low_tmp;
    logic rounded_high_tmp;
    logic[7-1:0] accepted_tmp;
    logic[8-1:0] sample_tmp;
    logic valid_tmp;
    logic clipped_tmp;


    function logic signed[31:0] signed_value (
        input logic[31:0] value
,       input logic[31:0] sign
    );
        return signed'(32'((value ^ sign))) - signed'(32'(sign));
    endfunction

    function logic[31:0] combine_digits (
        input logic[31:0] a
,       input logic[31:0] b
    );
        logic[31:0] middle;
        logic[31:0] upper;
        logic[31:0] upper_plus;
        logic[31:0] upper_minus;
        logic[31:0] upper_word;
        middle=((b & 32'hFF)) + ((((a >>> 32'sh4)) & 32'hFF));
        upper=((b >>> 32'sh8)) & 32'h1F;
        upper_plus=((upper + 32'h1)) & 32'h1F;
        upper_minus=((upper - 32'h1)) & 32'h1F;
        upper_word=((((((a >>> 32'shC)) & 32'h1)) != '0)) ? ((((((middle >>> 32'sh8)) != '0)) ? (upper) : (upper_minus))) : ((((((middle >>> 32'sh8)) != '0)) ? (upper_plus) : (upper)));
        return (((upper_word <<< 'hC)) | ((((middle & 32'hFF)) <<< 'h4))) | ((a & 32'hF));
    endfunction

    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] remainder;
        logic[31:0] carry;
        logic[31:0] rounding_sum;
        logic[31:0] integral_low;
        logic[31:0] integral_high;
        logic[31:0] upper_plus;
        logic[31:0] upper_minus;
        logic[31:0] upper_word;
        logic[31:0] low_negative;
        logic[31:0] quotient_upper;
        logic[31:0] rounding_carry;
        raw_sample_tmp = sample_in;
        raw_offset_tmp = offset_in;
        raw_gain_tmp = gain_in;
        centered_tmp = signed'(32'(signed'(32'(unsigned'(32'(raw_sample)))) - signed'(32'(unsigned'(32'(raw_offset))))));
        calibration_gain_tmp = raw_gain;
        for (logic[31:0] i = 32'h0;i < 32'h8;i=i+1) begin
            half_products_tmp[i] = signed'(32'(signed_value(unsigned'(32'(centered)), 32'h100)*signed'(32'((((unsigned'(32'(calibration_gain)) >>> ((32'h2*i)))) & 32'h3)))));
        end
        for (logic[31:0] i = 32'h0;i < 32'h4;i=i+1) begin
            partial_tmp[i] = signed'(32'(signed_value(unsigned'(32'(half_products[(32'h2*i)])), 32'h400) + (signed_value(unsigned'(32'(half_products[((32'h2*i) + 32'h1)])), 32'h400)*'h4)));
        end
        low_tmp = combine_digits(unsigned'(32'(partial['h0])), unsigned'(32'(partial['h1])));
        high_tmp = combine_digits(unsigned'(32'(partial['h2])), unsigned'(32'(partial['h3])));
        integral_low=((unsigned'(32'(high)) & 32'hFF)) + ((((unsigned'(32'(low)) >>> 32'sh8)) & 32'hFF));
        integral_high=unsigned'(32'(high)) >>> 32'sh8;
        upper_plus=((integral_high + 32'h1)) & 32'h1FF;
        upper_minus=((integral_high - 32'h1)) & 32'h1FF;
        low_negative=((unsigned'(32'(low)) >>> 32'sh10)) & 32'h1;
        upper_word=(((low_negative) != '0)) ? ((((((integral_low >>> 32'sh8)) != '0)) ? (integral_high) : (upper_minus))) : ((((((integral_low >>> 32'sh8)) != '0)) ? (upper_plus) : (integral_high)));
        integral_tmp = 32'(((upper_word <<< 'h8)) | ((integral_low & 32'hFF)));
        fraction_tmp = 32'(unsigned'(32'(low)) & 32'hFF);
        remainder=fraction;
        carry=(remainder > 32'h80) || (((remainder == 32'h80) && (((unsigned'(32'(integral)) >>> 32'sh10)) == 32'h0)));
        rounding_sum=((unsigned'(32'(integral)) & 32'hFF)) + ((32'h80 | carry));
        quotient_upper=unsigned'(32'(integral)) >>> 32'sh8;
        rounding_carry=((((((unsigned'(32'(integral)) >>> 32'sh7)) & 32'h1))) != '0) || (((((unsigned'(32'(integral)) & 32'h7F)) == 32'h7F) && ((carry) != '0)));
        rounded_tmp = 32'(rounding_sum & 32'hFF);
        rounded_low_tmp = ((((quotient_upper & 32'h100))) != '0) && (((quotient_upper != 32'h1FF) || !((rounding_carry) != '0)));
        rounded_high_tmp = !((((quotient_upper & 32'h100))) != '0) && (((quotient_upper != 32'h0) || ((rounding_carry) != '0)));
        accepted_tmp = ((accepted <<< 'h1)) | 64'(unsigned'(32'(valid_in)));
        valid_tmp = ((((accepted >>> 32'sh6)) & 64'h1) != '0);
        clipped_tmp = (('h0) != '0);
        if (((((accepted >>> 32'sh6)) & 64'h1) != '0)) begin
            sample_tmp = unsigned'(32'((((rounded_low) != '0)) ? (32'h0) : ((((rounded_high) != '0)) ? (32'hFF) : (unsigned'(32'(rounded))))));
            clipped_tmp = ((rounded_low) != '0) || ((rounded_high) != '0);
        end
        if (reset || !enable_in) begin
            accepted_tmp = 64'h0;
            sample_tmp = 64'h80;
            valid_tmp = (('h0) != '0);
            clipped_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        raw_sample_tmp = raw_sample;
        raw_offset_tmp = raw_offset;
        raw_gain_tmp = raw_gain;
        centered_tmp = centered;
        calibration_gain_tmp = calibration_gain;
        half_products_tmp = half_products;
        partial_tmp = partial;
        low_tmp = low;
        high_tmp = high;
        integral_tmp = integral;
        fraction_tmp = fraction;
        rounded_tmp = rounded;
        rounded_low_tmp = rounded_low;
        rounded_high_tmp = rounded_high;
        accepted_tmp = accepted;
        sample_tmp = sample;
        valid_tmp = valid;
        clipped_tmp = clipped;

            _work(reset);

        raw_sample <= raw_sample_tmp;
        raw_offset <= raw_offset_tmp;
        raw_gain <= raw_gain_tmp;
        centered <= centered_tmp;
        calibration_gain <= calibration_gain_tmp;
        half_products <= half_products_tmp;
        partial <= partial_tmp;
        low <= low_tmp;
        high <= high_tmp;
        integral <= integral_tmp;
        fraction <= fraction_tmp;
        rounded <= rounded_tmp;
        rounded_low <= rounded_low_tmp;
        rounded_high <= rounded_high_tmp;
        accepted <= accepted_tmp;
        sample <= sample_tmp;
        valid <= valid_tmp;
        clipped <= clipped_tmp;
    end

    assign sample_out = sample;

    assign valid_out = valid;

    assign clipped_out = clipped;


endmodule
