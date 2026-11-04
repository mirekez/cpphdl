`default_nettype none

import Predef_pkg::*;
import cpphdl_examples_math_Fp32AddMethods_pkg::*;
import cpphdl_examples_math_Fp32BitMethods_pkg::*;


module Fp32AddExample (
    input wire clk
,   input wire reset
,   input wire[31:0] a_in
,   input wire[31:0] b_in
,   output wire[31:0] result_out
);


    // regs and combs
    logic[31:0] result_comb;

    // members

    // tmp variables


    function logic[31:0] cpphdl_examples_math_Fp32BitMethods___leading_bit (input logic[31:0] value);
        logic[31:0] one;
        value|=value >>> 32'sh1;
        value|=value >>> 32'sh2;
        value|=value >>> 32'sh4;
        value|=value >>> 32'sh8;
        value|=value >>> 32'sh10;
        one=value ^ ((value >>> 32'sh1));
        return (((((32'((((one & 'hFFFF0000)) != 32'h0)) <<< 'h4)) | ((32'((((one & 'hFF00FF00)) != 32'h0)) <<< 'h3))) | ((32'((((one & 'hF0F0F0F0)) != 32'h0)) <<< 'h2))) | ((32'((((one & 'hCCCCCCCC)) != 32'h0)) <<< 'h1))) | 32'((((one & 'hAAAAAAAA)) != 32'h0));
    endfunction

    function logic[31:0] cpphdl_examples_math_Fp32AddMethods___fp32_leading_bit (input logic[31:0] value);
        return cpphdl_examples_math_Fp32BitMethods___leading_bit(value);
    endfunction

    function logic[31:0] cpphdl_examples_math_Fp32AddMethods___fp32_add_narrow (
        input logic[31:0] a
,       input logic[31:0] b
    );
        logic signed[15:0] normalized_exponent;
        logic exchange;
        logic[31:0] ea;
        logic[31:0] eb;
        logic[31:0] sa;
        logic[31:0] sb;
        logic[31:0] ma;
        logic[31:0] mb;
        logic[31:0] larger;
        logic[31:0] smaller;
        logic[31:0] sign;
        logic[31:0] exponent;
        logic[31:0] distance;
        logic[31:0] lhs;
        logic[31:0] rhs;
        logic[31:0] shift;
        logic[31:0] aligned;
        logic[31:0] sum;
        logic[31:0] zeros;
        logic[31:0] retained;
        logic[31:0] tail;
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
        exchange=((a & 'h7FFFFFFF)) < ((b & 'h7FFFFFFF));
        larger=(exchange) ? (b) : (a);
        smaller=(exchange) ? (a) : (b);
        sign=larger & 'h80000000;
        exponent=((larger >>> 32'sh17)) & 32'hFF;
        distance=exponent - ((((smaller >>> 32'sh17)) & 32'hFF));
        lhs=((((larger & 'h7FFFFF)) | 'h800000)) <<< 'h3;
        rhs=((((smaller & 'h7FFFFF)) | 'h800000)) <<< 'h3;
        shift=distance & 32'h1F;
        aligned=(distance>=32'h1B) ? ('h1) : (((distance == 32'h0) ? (rhs) : ((((rhs >>> shift)) | 32'((((rhs <<< ((((32'h20 - shift)) & 32'h1F)))) != 32'h0))))));
        sum=((((((larger ^ smaller)) & 'h80000000)) != '0)) ? (lhs - aligned) : (lhs + aligned);
        if (sum == 32'h0) begin
            return 32'h0;
        end
        normalized_exponent=exponent;
        if (((sum & 'h8000000) != '0)) begin
            sum=((sum >>> 32'sh1)) | ((sum & 32'h1));
            normalized_exponent=normalized_exponent+1;
        end
        else begin
            zeros=((32'h1A - cpphdl_examples_math_Fp32AddMethods___fp32_leading_bit(sum))) & 32'h1F;
            sum<<=zeros;
            normalized_exponent-=signed'(32'(signed'(16'(zeros))));
        end
        retained=((sum >>> 32'sh3)) & 'hFFFFFF;
        tail=sum & 32'h7;
        if ((tail > 32'h4) || (((tail == 32'h4) && ((((retained & 32'h1))) != '0)))) begin
            retained=retained+1;
        end
        if (retained>='h1000000) begin
            retained>>='h1;
            normalized_exponent=normalized_exponent+1;
        end
        if (normalized_exponent>=32'shFF) begin
            return sign | 'h7F800000;
        end
        if (normalized_exponent<=32'sh0) begin
            return sign;
        end
        return (sign | ((unsigned'(32'(normalized_exponent)) <<< 'h17))) | ((retained & 'h7FFFFF));
    endfunction

    always_comb begin : result_comb_func  // result_comb_func
        result_comb=cpphdl_examples_math_Fp32AddMethods___fp32_add_narrow(a_in, b_in);
    end

    generate  // _assign
        assign result_out = result_comb;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end


endmodule
