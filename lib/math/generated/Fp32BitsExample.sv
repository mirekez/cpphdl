`default_nettype none

import Predef_pkg::*;
import cpphdl_examples_math_Fp32BitsMethods_pkg::*;


module Fp32BitsExample (
    input wire clk
,   input wire reset
,   input wire[31:0] a_in
,   input wire[31:0] b_in
,   output wire[31:0] sum_out
,   output wire[31:0] product_out
,   output wire[31:0] half_out
);


    // regs and combs
    logic[31:0] sum_comb;
    logic[31:0] product_comb;
    logic[31:0] half_comb;

    // members

    // tmp variables


    function logic[63:0] cpphdl_examples_math_Fp32BitsMethods___shift_jam (
        input logic[63:0] x
,       input logic[31:0] shift
    );
        if (shift == 32'h0) begin
            return x;
        end
        if (shift>=32'h40) begin
            return 64'(x != 64'h0);
        end
        return ((x >>> shift)) | 64'(((((x & ((((64'h1 <<< shift)) - 64'h1)))) != 64'h0)));
    endfunction

    function logic[31:0] cpphdl_examples_math_Fp32BitsMethods___leading_zeros (input logic[31:0] x);
        logic[31:0] count;
        count=32'h0;
        if (!((x) != '0)) begin
            return 32'h20;
        end
        if (!(((x >>> 32'sh10)) != '0)) begin
            count+=32'h10;
            x<<='h10;
        end
        if (!(((x >>> 32'sh18)) != '0)) begin
            count+=32'h8;
            x<<='h8;
        end
        if (!(((x >>> 32'sh1C)) != '0)) begin
            count+=32'h4;
            x<<='h4;
        end
        if (!(((x >>> 32'sh1E)) != '0)) begin
            count+=32'h2;
            x<<='h2;
        end
        if (!(((x >>> 32'sh1F)) != '0)) begin
            count+=32'h1;
        end
        return count;
    endfunction

    function logic[31:0] cpphdl_examples_math_Fp32BitsMethods___fp32_round (
        input logic[31:0] sign
,       input logic signed[31:0] exp
,       input logic[63:0] mant
    );
        logic[63:0] retained;
        logic[63:0] tail;
        retained=mant >>> 32'sh3;
        tail=mant & 64'h7;
        if ((tail > 64'h4) || (((tail == 64'h4) && ((((retained & 64'h1))) != '0)))) begin
            retained=retained+1;
        end
        if (retained>=(64'h1 <<< 'h18)) begin
            retained>>='h1;
            exp=exp+1;
        end
        if (exp>=32'shFF) begin
            return sign | 'h7F800000;
        end
        if (exp<=32'sh0) begin
            return sign;
        end
        return (sign | ((unsigned'(32'(exp)) <<< 'h17))) | ((32'(retained) & 'h7FFFFF));
    endfunction

    function logic[31:0] cpphdl_examples_math_Fp32BitsMethods___fp32_add (
        input logic[31:0] a
,       input logic[31:0] b
    );
        logic signed[31:0] exponent;
        logic[31:0] ea;
        logic[31:0] eb;
        logic[31:0] sa;
        logic[31:0] sb;
        logic[31:0] ma;
        logic[31:0] mb;
        logic[31:0] swap;
        logic[31:0] shift;
        logic[31:0] sign;
        logic[63:0] aa;
        logic[63:0] bb;
        logic[63:0] sum;
        ea=((a >>> 32'sh17)) & 32'hFF;
        eb=((b >>> 32'sh17)) & 32'hFF;
        sa=a & 'h80000000;
        sb=b & 'h80000000;
        ma=a & 'h7FFFFF;
        mb=b & 'h7FFFFF;
        if (((((ea == 32'hFF) && ((ma) != '0))) || (((eb == 32'hFF) && ((mb) != '0)))) || ((((ea == 32'hFF) && (eb == 32'hFF)) && (sa != sb)))) begin
            return 'h7FC00000;
        end
        if (ea == 32'hFF) begin
            return a;
        end
        if (eb == 32'hFF) begin
            return b;
        end
        if (!((ea) != '0) && !((eb) != '0)) begin
            return sa & sb;
        end
        if (!((ea) != '0)) begin
            return b;
        end
        if (!((eb) != '0)) begin
            return a;
        end
        if (((a & 'h7FFFFFFF)) < ((b & 'h7FFFFFFF))) begin
            swap=a;
            a=b;
            b=swap;
        end
        ea=((a >>> 32'sh17)) & 32'hFF;
        eb=((b >>> 32'sh17)) & 32'hFF;
        sa=a & 'h80000000;
        sb=b & 'h80000000;
        aa=64'(32'((((a & 'h7FFFFF)) | 'h800000))) <<< 'h3;
        bb=cpphdl_examples_math_Fp32BitsMethods___shift_jam(64'(32'((((b & 'h7FFFFF)) | 'h800000))) <<< 'h3, ea - eb);
        exponent=ea;
        sign=sa;
        sum=(sa == sb) ? (aa + bb) : (aa - bb);
        if (sum == 64'h0) begin
            return 32'h0;
        end
        if (((sum & ((64'h1 <<< 'h1B))) != '0)) begin
            sum=cpphdl_examples_math_Fp32BitsMethods___shift_jam(sum, 32'h1);
            exponent=exponent+1;
        end
        else begin
            shift=cpphdl_examples_math_Fp32BitsMethods___leading_zeros(32'(sum)) - 32'h5;
            sum<<=shift;
            exponent-=signed'(32'(shift));
        end
        return cpphdl_examples_math_Fp32BitsMethods___fp32_round(sign, exponent, sum);
    endfunction

    always_comb begin : sum_comb_func  // sum_comb_func
        sum_comb=cpphdl_examples_math_Fp32BitsMethods___fp32_add(a_in, b_in);
    end

    function logic[31:0] cpphdl_examples_math_Fp32BitsMethods___fp32_multiply (
        input logic[31:0] a
,       input logic[31:0] b
    );
        logic[63:0] product;
        logic signed[31:0] exponent;
        logic[31:0] ea;
        logic[31:0] eb;
        logic[31:0] ma;
        logic[31:0] mb;
        logic[31:0] sign;
        logic[31:0] shift;
        ea=((a >>> 32'sh17)) & 32'hFF;
        eb=((b >>> 32'sh17)) & 32'hFF;
        ma=a & 'h7FFFFF;
        mb=b & 'h7FFFFF;
        sign=((a ^ b)) & 'h80000000;
        if ((((((ea == 32'hFF) && ((ma) != '0))) || (((eb == 32'hFF) && ((mb) != '0)))) || (((ea == 32'hFF) && !((eb) != '0)))) || (((eb == 32'hFF) && !((ea) != '0)))) begin
            return 'h7FC00000;
        end
        if ((ea == 32'hFF) || (eb == 32'hFF)) begin
            return sign | 'h7F800000;
        end
        if (!((ea) != '0) || !((eb) != '0)) begin
            return sign;
        end
        product=128'(64'(32'((ma | 'h800000))))*128'(64'(32'((mb | 'h800000))));
        exponent=(signed'(32'(ea)) + signed'(32'(eb))) - 'h7F;
        shift=32'h14;
        if (((product & ((64'h1 <<< 'h2F))) != '0)) begin
            shift=32'h15;
            exponent=exponent+1;
        end
        return cpphdl_examples_math_Fp32BitsMethods___fp32_round(sign, exponent, cpphdl_examples_math_Fp32BitsMethods___shift_jam(product, shift));
    endfunction

    always_comb begin : product_comb_func  // product_comb_func
        product_comb=cpphdl_examples_math_Fp32BitsMethods___fp32_multiply(a_in, b_in);
    end

    function logic[31:0] cpphdl_examples_math_Fp32BitsMethods___fp16_to_fp32 (input logic[31:0] raw);
        logic[31:0] sign;
        logic[31:0] exp;
        logic[31:0] mant;
        logic[31:0] shift;
        sign=((raw & 'h8000)) <<< 'h10;
        exp=((raw >>> 32'shA)) & 32'h1F;
        mant=raw & 32'h3FF;
        if (exp == 32'h1F) begin
            return (sign | 'h7F800000) | ((mant <<< 'hD));
        end
        if (exp != 32'h0) begin
            return (sign | ((((exp + 32'h70)) <<< 'h17))) | ((mant <<< 'hD));
        end
        if (!((mant) != '0)) begin
            return sign;
        end
        shift=cpphdl_examples_math_Fp32BitsMethods___leading_zeros(mant) - 32'h15;
        return (sign | ((((32'h71 - shift)) <<< 'h17))) | ((((mant <<< ((shift + 32'hD)))) & 'h7FFFFF));
    endfunction

    always_comb begin : half_comb_func  // half_comb_func
        half_comb=cpphdl_examples_math_Fp32BitsMethods___fp16_to_fp32(a_in);
    end

    generate  // _assign
        assign sum_out = sum_comb;
        assign product_out = product_comb;
        assign half_out = half_comb;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end


endmodule
