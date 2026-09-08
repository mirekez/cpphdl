`default_nettype none

import Predef_pkg::*;


module FpAdd #(
    parameter W = 32
,   parameter EW = 8
 )
 (
    input wire sum_clock
,   input wire reset
,   input wire[W-1:0] a_in
,   input wire[W-1:0] b_in
,   input wire en_in
,   output wire[W-1:0] data_out
);
    localparam  MANT_WIDTH = (W - EW) - 'h1;
    localparam  SUM_WIDTH = (((((MANT_WIDTH + 'h6) + 'h7))/'h8))*'h8;
    localparam  MANT_MASK = ((unsigned'(64'('h1)) <<< MANT_WIDTH)) - 'h1;
    localparam  EXP_MAX = ((unsigned'(64'('h1)) <<< EW)) - 'h1;
    localparam  SIGN_MASK = unsigned'(64'('h1)) <<< ((W - 'h1));
    localparam  SUM_MASK = ((unsigned'(64'('h1)) <<< SUM_WIDTH)) - 'h1;


    // regs and combs
    reg[SUM_WIDTH-1:0] sum_reg;
    reg[EW-1:0] exponent_reg;
    reg[W-1:0] special_result_reg;
    reg special_reg;
    logic[SUM_WIDTH-1:0] add_lhs_comb;
    logic[SUM_WIDTH-1:0] add_rhs_comb;
    logic[EW-1:0] exponent_comb;
    logic[W-1:0] special_result_comb;
    logic special_comb;
    logic[W-1:0] result_comb;

    // members

    // tmp variables
    logic[SUM_WIDTH-1:0] sum_reg_tmp;
    logic[EW-1:0] exponent_reg_tmp;
    logic[W-1:0] special_result_reg_tmp;
    logic special_reg_tmp;


    always_comb begin : add_lhs_comb_func  // add_lhs_comb_func
        logic[63:0] raw_a;
        logic[63:0] raw_b;
        logic[63:0] exponent_a;
        logic[63:0] exponent_b;
        logic[63:0] mantissa_a;
        logic[63:0] significand_a;
        logic[63:0] aligned_a;
        logic[63:0] discarded;
        logic[63:0] shift_mask;
        logic[64-1:0] encoded;
        logic[31:0] shift;
        logic sign_a;
        logic special;
        raw_a=unsigned'(64'(a_in));
        raw_b=unsigned'(64'(b_in));
        exponent_a=((raw_a >>> MANT_WIDTH)) & EXP_MAX;
        exponent_b=((raw_b >>> MANT_WIDTH)) & EXP_MAX;
        mantissa_a=raw_a & MANT_MASK;
        sign_a=((raw_a & SIGN_MASK)) != 'h0;
        special=(((exponent_a == 'h0) || (exponent_b == 'h0)) || (exponent_a == EXP_MAX)) || (exponent_b == EXP_MAX);
        significand_a='h0;
        aligned_a='h0;
        discarded='h0;
        shift_mask='h0;
        encoded = 'h0;
        shift='h0;
        add_lhs_comb = 'h0;
        if (!special) begin
            significand_a=((((unsigned'(64'('h1)) <<< MANT_WIDTH)) | mantissa_a)) <<< 'h3;
            aligned_a=significand_a;
            if (exponent_a < exponent_b) begin
                shift=unsigned'(32'((exponent_b - exponent_a)));
                if (shift>=SUM_WIDTH) begin
                    aligned_a=(significand_a != 'h0) ? ('h1) : ('h0);
                end
                else begin
                    if (shift != 'h0) begin
                        shift_mask=((unsigned'(64'('h1)) <<< shift)) - 'h1;
                        discarded=significand_a & shift_mask;
                        aligned_a=significand_a >>> shift;
                        if (discarded != 'h0) begin
                            aligned_a|='h1;
                        end
                    end
                end
            end
            encoded = (sign_a) ? ((((unsigned'(64'('h0)) - aligned_a)) & SUM_MASK)) : (aligned_a);
            add_lhs_comb = encoded['h0 +:SUM_WIDTH - 'h1 - 'h0 + 1];
        end
    end

    always_comb begin : add_rhs_comb_func  // add_rhs_comb_func
        logic[63:0] raw_a;
        logic[63:0] raw_b;
        logic[63:0] exponent_a;
        logic[63:0] exponent_b;
        logic[63:0] mantissa_b;
        logic[63:0] significand_b;
        logic[63:0] aligned_b;
        logic[63:0] discarded;
        logic[63:0] shift_mask;
        logic[64-1:0] encoded;
        logic[31:0] shift;
        logic sign_b;
        logic special;
        raw_a=unsigned'(64'(a_in));
        raw_b=unsigned'(64'(b_in));
        exponent_a=((raw_a >>> MANT_WIDTH)) & EXP_MAX;
        exponent_b=((raw_b >>> MANT_WIDTH)) & EXP_MAX;
        mantissa_b=raw_b & MANT_MASK;
        sign_b=((raw_b & SIGN_MASK)) != 'h0;
        special=(((exponent_a == 'h0) || (exponent_b == 'h0)) || (exponent_a == EXP_MAX)) || (exponent_b == EXP_MAX);
        significand_b='h0;
        aligned_b='h0;
        discarded='h0;
        shift_mask='h0;
        encoded = 'h0;
        shift='h0;
        add_rhs_comb = 'h0;
        if (!special) begin
            significand_b=((((unsigned'(64'('h1)) <<< MANT_WIDTH)) | mantissa_b)) <<< 'h3;
            aligned_b=significand_b;
            if (exponent_b < exponent_a) begin
                shift=unsigned'(32'((exponent_a - exponent_b)));
                if (shift>=SUM_WIDTH) begin
                    aligned_b=(significand_b != 'h0) ? ('h1) : ('h0);
                end
                else begin
                    if (shift != 'h0) begin
                        shift_mask=((unsigned'(64'('h1)) <<< shift)) - 'h1;
                        discarded=significand_b & shift_mask;
                        aligned_b=significand_b >>> shift;
                        if (discarded != 'h0) begin
                            aligned_b|='h1;
                        end
                    end
                end
            end
            encoded = (sign_b) ? ((((unsigned'(64'('h0)) - aligned_b)) & SUM_MASK)) : (aligned_b);
            add_rhs_comb = encoded['h0 +:SUM_WIDTH - 'h1 - 'h0 + 1];
        end
    end

    always_comb begin : exponent_comb_func  // exponent_comb_func
        logic[63:0] exponent_a;
        logic[63:0] exponent_b;
        logic[64-1:0] exponent_wide;
        exponent_a=((unsigned'(64'(a_in)) >>> MANT_WIDTH)) & EXP_MAX;
        exponent_b=((unsigned'(64'(b_in)) >>> MANT_WIDTH)) & EXP_MAX;
        exponent_wide = 'h0;
        exponent_comb = 'h0;
        if ((((exponent_a != 'h0) && (exponent_b != 'h0)) && (exponent_a != EXP_MAX)) && (exponent_b != EXP_MAX)) begin
            exponent_wide = (exponent_a>=exponent_b) ? (exponent_a) : (exponent_b);
            exponent_comb = exponent_wide['h0 +:EW - 'h1 - 'h0 + 1];
        end
    end

    always_comb begin : special_comb_func  // special_comb_func
        logic[63:0] exponent_a;
        logic[63:0] exponent_b;
        exponent_a=((unsigned'(64'(a_in)) >>> MANT_WIDTH)) & EXP_MAX;
        exponent_b=((unsigned'(64'(b_in)) >>> MANT_WIDTH)) & EXP_MAX;
        special_comb=(((exponent_a == 'h0) || (exponent_b == 'h0)) || (exponent_a == EXP_MAX)) || (exponent_b == EXP_MAX);
    end

    always_comb begin : special_result_comb_func  // special_result_comb_func
        logic[63:0] raw_a;
        logic[63:0] raw_b;
        logic[63:0] exponent_a;
        logic[63:0] exponent_b;
        logic[63:0] mantissa_a;
        logic[63:0] mantissa_b;
        logic[64-1:0] result_wide;
        logic sign_a;
        logic sign_b;
        logic zero_a;
        logic zero_b;
        logic infinity_a;
        logic infinity_b;
        logic nan_a;
        logic nan_b;
        raw_a=unsigned'(64'(a_in));
        raw_b=unsigned'(64'(b_in));
        exponent_a=((raw_a >>> MANT_WIDTH)) & EXP_MAX;
        exponent_b=((raw_b >>> MANT_WIDTH)) & EXP_MAX;
        mantissa_a=raw_a & MANT_MASK;
        mantissa_b=raw_b & MANT_MASK;
        sign_a=((raw_a & SIGN_MASK)) != 'h0;
        sign_b=((raw_b & SIGN_MASK)) != 'h0;
        zero_a=exponent_a == 'h0;
        zero_b=exponent_b == 'h0;
        infinity_a=(exponent_a == EXP_MAX) && (mantissa_a == 'h0);
        infinity_b=(exponent_b == EXP_MAX) && (mantissa_b == 'h0);
        nan_a=(exponent_a == EXP_MAX) && (mantissa_a != 'h0);
        nan_b=(exponent_b == EXP_MAX) && (mantissa_b != 'h0);
        result_wide = 'h0;
        special_result_comb = 'h0;
        if ((nan_a || nan_b) || (((infinity_a && infinity_b) && (sign_a != sign_b)))) begin
            result_wide = ((EXP_MAX <<< MANT_WIDTH)) | 'h1;
        end
        else begin
            if (infinity_a) begin
                result_wide = ((sign_a) ? (SIGN_MASK) : ('h0)) | ((EXP_MAX <<< MANT_WIDTH));
            end
            else begin
                if (infinity_b) begin
                    result_wide = ((sign_b) ? (SIGN_MASK) : ('h0)) | ((EXP_MAX <<< MANT_WIDTH));
                end
                else begin
                    if (zero_a && zero_b) begin
                        result_wide = (sign_a && sign_b) ? (SIGN_MASK) : ('h0);
                    end
                    else begin
                        if (zero_a) begin
                            result_wide = raw_b;
                        end
                        else begin
                            if (zero_b) begin
                                result_wide = raw_a;
                            end
                        end
                    end
                end
            end
        end
        special_result_comb = result_wide['h0 +:W - 'h1 - 'h0 + 1];
    end

    always_comb begin : result_comb_func  // result_comb_func
        logic[63:0] sum;
        logic[63:0] magnitude;
        logic[63:0] retained;
        logic[64-1:0] result_raw;
        logic[63:0] discarded;
        logic signed[31:0] exponent;
        logic negative;
        logic guard;
        logic round;
        logic sticky;
        logic[7:0] i;
        sum=unsigned'(64'(sum_reg));
        magnitude='h0;
        retained='h0;
        result_raw = 'h0;
        discarded='h0;
        exponent=signed'(32'(unsigned'(64'(exponent_reg))));
        negative=((sum & ((unsigned'(64'('h1)) <<< ((SUM_WIDTH - 'h1)))))) != 'h0;
        guard=0;
        round=0;
        sticky=0;
        result_comb = 'h0;
        if (special_reg) begin
            result_raw = unsigned'(64'(special_result_reg));
        end
        else begin
            magnitude=(negative) ? ((((unsigned'(64'('h0)) - sum)) & SUM_MASK)) : (sum);
            if (magnitude == 'h0) begin
                result_raw = 'h0;
            end
            else begin
                if (((magnitude & ((unsigned'(64'('h1)) <<< ((MANT_WIDTH + 'h4)))))) != 'h0) begin
                    discarded=magnitude & 'h1;
                    magnitude>>='h1;
                    if (discarded != 'h0) begin
                        magnitude|='h1;
                    end
                    exponent=exponent+1;
                end
                for (i='h0;i < 'h20;i=i+1) begin
                    if (((((magnitude & ((unsigned'(64'('h1)) <<< ((MANT_WIDTH + 'h3)))))) == 'h0) && (magnitude != 'h0)) && (exponent > 'h0)) begin
                        magnitude<<='h1;
                        --exponent;
                    end
                end
                if (exponent<='h0) begin
                    result_raw = (negative) ? (SIGN_MASK) : ('h0);
                end
                else begin
                    if (exponent>=signed'(32'(EXP_MAX))) begin
                        result_raw = ((negative) ? (SIGN_MASK) : ('h0)) | ((EXP_MAX <<< MANT_WIDTH));
                    end
                    else begin
                        retained=magnitude >>> 'h3;
                        guard=((((magnitude >>> 'h2)) & 'h1)) != 'h0;
                        round=((((magnitude >>> 'h1)) & 'h1)) != 'h0;
                        sticky=((magnitude & 'h1)) != 'h0;
                        if (guard && (((round || sticky) || (((retained & 'h1)) != 'h0)))) begin
                            retained=retained+1;
                        end
                        if (retained>=(unsigned'(64'('h1)) <<< ((MANT_WIDTH + 'h1)))) begin
                            retained>>='h1;
                            exponent=exponent+1;
                        end
                        if (exponent>=signed'(32'(EXP_MAX))) begin
                            result_raw = ((negative) ? (SIGN_MASK) : ('h0)) | ((EXP_MAX <<< MANT_WIDTH));
                        end
                        else begin
                            result_raw = (((negative) ? (SIGN_MASK) : ('h0)) | ((unsigned'(64'(exponent)) <<< MANT_WIDTH))) | ((retained & MANT_MASK));
                        end
                    end
                end
            end
        end
        result_comb = result_raw['h0 +:W - 'h1 - 'h0 + 1];
    end

    task _work (input logic reset);
    begin: _work
        if (en_in) begin
            sum_reg_tmp = add_lhs_comb + add_rhs_comb;
            exponent_reg_tmp = exponent_comb;
            special_result_reg_tmp = special_result_comb;
            special_reg_tmp = unsigned'(1'(special_comb));
        end
        if (reset) begin
            sum_reg_tmp = '0;
            exponent_reg_tmp = '0;
            special_result_reg_tmp = '0;
            special_reg_tmp = '0;
        end
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge sum_clock) begin
        sum_reg_tmp = sum_reg;
        exponent_reg_tmp = exponent_reg;
        special_result_reg_tmp = special_result_reg;
        special_reg_tmp = special_reg;

        _work(reset);

        sum_reg <= sum_reg_tmp;
        exponent_reg <= exponent_reg_tmp;
        special_result_reg <= special_result_reg_tmp;
        special_reg <= special_reg_tmp;
    end

    assign data_out = result_comb;


endmodule
