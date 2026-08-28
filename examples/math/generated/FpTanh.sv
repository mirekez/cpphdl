`default_nettype none

import Predef_pkg::*;


module FpTanh #(
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
    localparam  FIXED_BITS = ((MANT_WIDTH + 'h4) < 'h1A) ? (MANT_WIDTH + 'h4) : ('h1A);
    localparam  MANT_MASK = ((unsigned'(64'('h1)) <<< MANT_WIDTH)) - 'h1;
    localparam  EXP_MAX = ((unsigned'(64'('h1)) <<< EW)) - 'h1;
    localparam  SIGN_MASK = unsigned'(64'('h1)) <<< ((W - 'h1));
    localparam  ONE_FIXED = unsigned'(64'('h1)) <<< FIXED_BITS;
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
        logic[63:0] x_fixed;
        logic[63:0] x_squared;
        logic[63:0] numerator;
        logic[63:0] denominator;
        logic[63:0] y_fixed;
        logic[63:0] normalized;
        logic[63:0] discarded;
        logic[63:0] halfway;
        logic[64-1:0] result_raw;
        logic signed[31:0] exponent_unbiased;
        logic signed[31:0] fixed_shift;
        logic signed[31:0] result_exponent;
        logic signed[31:0] small_exponent_limit;
        logic signed[31:0] saturation_exponent_limit;
        logic signed[31:0] zero_shift;
        logic[7:0] shift_amount;
        logic[7:0] msb;
        logic[7:0] i;
        raw=unsigned'(64'(data_in));
        sign=raw & SIGN_MASK;
        exponent=((raw >>> MANT_WIDTH)) & EXP_MAX;
        mantissa=raw & MANT_MASK;
        significand='h0;
        x_fixed='h0;
        x_squared='h0;
        numerator='h0;
        denominator='h1;
        y_fixed='h0;
        normalized='h0;
        discarded='h0;
        halfway='h0;
        result_raw = 'h0;
        exponent_unbiased='h0;
        fixed_shift='h0;
        result_exponent='h0;
        small_exponent_limit=-'h5;
        saturation_exponent_limit='h2;
        zero_shift='h0;
        shift_amount='h0;
        msb='h0;
        result_comb = 'h0;
        if (exponent == 'h0) begin
            result_raw = sign;
        end
        else begin
            if (exponent == EXP_MAX) begin
                if (mantissa != 'h0) begin
                    result_raw = ((EXP_MAX <<< MANT_WIDTH)) | 'h1;
                end
                else begin
                    result_raw = sign | ((unsigned'(64'(EXP_BIAS)) <<< MANT_WIDTH));
                end
            end
            else begin
                exponent_unbiased=signed'(32'(exponent)) - EXP_BIAS;
                if (exponent_unbiased<=small_exponent_limit) begin
                    result_raw = raw;
                end
                else begin
                    if (exponent_unbiased>=saturation_exponent_limit) begin
                        result_raw = sign | ((unsigned'(64'(EXP_BIAS)) <<< MANT_WIDTH));
                    end
                    else begin
                        significand=((unsigned'(64'('h1)) <<< MANT_WIDTH)) | mantissa;
                        fixed_shift=(signed'(32'(FIXED_BITS)) + exponent_unbiased) - signed'(32'(MANT_WIDTH));
                        if (fixed_shift>=zero_shift) begin
                            x_fixed=significand <<< fixed_shift;
                        end
                        else begin
                            shift_amount=unsigned'(8'((-fixed_shift)));
                            x_fixed=significand >>> shift_amount;
                            discarded=significand & ((((unsigned'(64'('h1)) <<< shift_amount)) - 'h1));
                            halfway=unsigned'(64'('h1)) <<< ((shift_amount - 'h1));
                            if ((discarded > halfway) || (((discarded == halfway) && (((x_fixed & 'h1)) != 'h0)))) begin
                                x_fixed=x_fixed+1;
                            end
                        end
                        if (x_fixed>='h3*ONE_FIXED) begin
                            y_fixed=ONE_FIXED;
                        end
                        else begin
                            x_squared=((x_fixed*x_fixed)) >>> FIXED_BITS;
                            numerator=x_fixed*((('h1B*ONE_FIXED) + x_squared));
                            denominator=('h1B*ONE_FIXED) + ('h9*x_squared);
                            y_fixed=((numerator + (denominator/'h2)))/denominator;
                            if (y_fixed > ONE_FIXED) begin
                                y_fixed=ONE_FIXED;
                            end
                        end
                        if (y_fixed == 'h0) begin
                            result_raw = sign;
                        end
                        else begin
                            if (y_fixed>=ONE_FIXED) begin
                                result_raw = sign | ((unsigned'(64'(EXP_BIAS)) <<< MANT_WIDTH));
                            end
                            else begin
                                msb='h0;
                                for (i='h0;i < 'h20;i=i+1) begin
                                    if (((y_fixed >>> i)) != 'h0) begin
                                        msb=i;
                                    end
                                end
                                result_exponent=(signed'(32'(msb)) - signed'(32'(FIXED_BITS))) + EXP_BIAS;
                                if (result_exponent<=zero_shift) begin
                                    result_raw = sign;
                                end
                                else begin
                                    if (msb > MANT_WIDTH) begin
                                        shift_amount=msb - MANT_WIDTH;
                                        normalized=y_fixed >>> shift_amount;
                                        discarded=y_fixed & ((((unsigned'(64'('h1)) <<< shift_amount)) - 'h1));
                                        halfway=unsigned'(64'('h1)) <<< ((shift_amount - 'h1));
                                        if ((discarded > halfway) || (((discarded == halfway) && (((normalized & 'h1)) != 'h0)))) begin
                                            normalized=normalized+1;
                                        end
                                    end
                                    else begin
                                        normalized=y_fixed <<< ((MANT_WIDTH - msb));
                                    end
                                    if (normalized>=(unsigned'(64'('h1)) <<< ((MANT_WIDTH + 'h1)))) begin
                                        normalized>>='h1;
                                        result_exponent=result_exponent+1;
                                    end
                                    result_raw = (sign | ((unsigned'(64'(result_exponent)) <<< MANT_WIDTH))) | ((normalized & MANT_MASK));
                                end
                            end
                        end
                    end
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
