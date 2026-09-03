`default_nettype none

import Predef_pkg::*;


module FpSqrt #(
    parameter W = 32
,   parameter EW = 8
 )
 (
    input wire clk
,   input wire reset
,   input wire[W-1:0] data_in
,   output wire[W-1:0] data_out
);
    localparam  MANT_WIDTH = (W - EW) - 'h1;
    localparam  MANT_MASK = ((unsigned'(64'('h1)) <<< MANT_WIDTH)) - 'h1;
    localparam  EXP_MAX = ((unsigned'(64'('h1)) <<< EW)) - 'h1;
    localparam  SIGN_MASK = unsigned'(64'('h1)) <<< ((W - 'h1));
    localparam  EXP_BIAS = (('h1 <<< ((EW - 'h1)))) - 'h1;


    // regs and combs
    logic[W-1:0] result_comb;

    // members

    // tmp variables


    always_comb begin : result_comb_func  // result_comb_func
        logic[63:0] raw;
        logic[63:0] sign;
        logic[63:0] exponent;
        logic[63:0] mantissa;
        logic[63:0] significand;
        logic[63:0] radicand;
        logic[63:0] remainder;
        logic[63:0] root;
        logic[63:0] trial;
        logic[63:0] rounded;
        logic[64-1:0] result_raw;
        logic signed[31:0] exponent_unbiased;
        logic signed[31:0] result_exponent;
        logic odd_exponent;
        logic[7:0] i;
        raw=unsigned'(64'(data_in));
        sign=raw & SIGN_MASK;
        exponent=((raw >>> MANT_WIDTH)) & EXP_MAX;
        mantissa=raw & MANT_MASK;
        significand='h0;
        radicand='h0;
        remainder='h0;
        root='h0;
        trial='h0;
        rounded='h0;
        result_raw = 'h0;
        exponent_unbiased='h0;
        result_exponent='h0;
        odd_exponent=0;
        result_comb = 'h0;
        if (exponent == 'h0) begin
            result_raw = sign;
        end
        else begin
            if (exponent == EXP_MAX) begin
                if ((mantissa == 'h0) && (sign == 'h0)) begin
                    result_raw = EXP_MAX <<< MANT_WIDTH;
                end
                else begin
                    result_raw = ((EXP_MAX <<< MANT_WIDTH)) | 'h1;
                end
            end
            else begin
                if (sign != 'h0) begin
                    result_raw = ((EXP_MAX <<< MANT_WIDTH)) | 'h1;
                end
                else begin
                    exponent_unbiased=signed'(32'(exponent)) - EXP_BIAS;
                    odd_exponent=((exponent_unbiased % 'h2)) != 'h0;
                    significand=((unsigned'(64'('h1)) <<< MANT_WIDTH)) | mantissa;
                    if (odd_exponent) begin
                        significand<<='h1;
                        --exponent_unbiased;
                    end
                    radicand=significand <<< ((MANT_WIDTH + 'h2));
                    remainder='h0;
                    root='h0;
                    for (i='h0;i < 'h20;i=i+1) begin
                        remainder=((remainder <<< 'h2)) | ((radicand >>> 'h3E));
                        radicand<<='h2;
                        root<<='h1;
                        trial=((root <<< 'h1)) | 'h1;
                        if (remainder>=trial) begin
                            remainder-=trial;
                            root|='h1;
                        end
                    end
                    rounded=root >>> 'h1;
                    if ((((root & 'h1)) != 'h0) && (((remainder != 'h0) || (((rounded & 'h1)) != 'h0)))) begin
                        rounded=rounded+1;
                    end
                    result_exponent=(exponent_unbiased/'h2) + EXP_BIAS;
                    if (rounded>=(unsigned'(64'('h1)) <<< ((MANT_WIDTH + 'h1)))) begin
                        rounded>>='h1;
                        result_exponent=result_exponent+1;
                    end
                    result_raw = ((unsigned'(64'(result_exponent)) <<< MANT_WIDTH)) | ((rounded & MANT_MASK));
                end
            end
        end
        result_comb = result_raw['h0 +:W - 'h1 - 'h0 + 1];
    end

    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end

    assign data_out = result_comb;


endmodule
