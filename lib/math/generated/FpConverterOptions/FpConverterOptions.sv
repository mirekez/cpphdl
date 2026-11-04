`default_nettype none

import Predef_pkg::*;
import FP16_5_pkg::*;
import FP32_8_pkg::*;
import FP64_11_pkg::*;
import FpFormatConversion_pkg::*;


module FpConverterOptions (
    input wire clk
,   input wire reset
,   input wire[15:0] half_in
,   input wire[31:0] single_in
,   input wire signed[31:0] integer_in
,   input wire[63:0] double_in
,   output wire[31:0] object_out
,   output wire[31:0] plain_out
,   output wire[31:0] subnormal_out
,   output wire[31:0] payload_out
,   output wire[31:0] exact_out
,   output wire[15:0] narrow_out
,   output wire[31:0] integer_out
,   output wire[31:0] registered_integer_out
,   output wire[15:0] half_integer_out
,   output wire[15:0] wide_out
);


    // regs and combs
    logic[31:0] object_comb;
    FP16_5[1-1:0] half_words;
    FP32_8[1-1:0] single_words;
    logic signed[1-1:0][31:0] integer_words;
    FP64_11[1-1:0] double_words;

    // members
    wire FP16_5[1-1:0] plain__data_in;
    wire FP32_8[1-1:0] plain__data_out;
    wire plain__debugen_in;
    FpConverterFP16_5_FP32_8 #(
        1
,       0
,       0
,       0
    ) plain (
        .clk(clk)
,       .reset(reset)
,       .data_in(plain__data_in)
,       .data_out(plain__data_out)
,       .debugen_in(plain__debugen_in)
    );
    wire FP16_5[1-1:0] subnormal__data_in;
    wire FP32_8[1-1:0] subnormal__data_out;
    wire subnormal__debugen_in;
    FpConverterFP16_5_FP32_8 #(
        1
,       0
,       1
,       0
    ) subnormal (
        .clk(clk)
,       .reset(reset)
,       .data_in(subnormal__data_in)
,       .data_out(subnormal__data_out)
,       .debugen_in(subnormal__debugen_in)
    );
    wire FP16_5[1-1:0] payload__data_in;
    wire FP32_8[1-1:0] payload__data_out;
    wire payload__debugen_in;
    FpConverterFP16_5_FP32_8 #(
        1
,       0
,       0
,       1
    ) payload (
        .clk(clk)
,       .reset(reset)
,       .data_in(payload__data_in)
,       .data_out(payload__data_out)
,       .debugen_in(payload__debugen_in)
    );
    wire FP16_5[1-1:0] exact__data_in;
    wire FP32_8[1-1:0] exact__data_out;
    wire exact__debugen_in;
    FpConverterFP16_5_FP32_8 #(
        1
,       0
,       1
,       1
    ) exact (
        .clk(clk)
,       .reset(reset)
,       .data_in(exact__data_in)
,       .data_out(exact__data_out)
,       .debugen_in(exact__debugen_in)
    );
    wire FP32_8[1-1:0] narrow__data_in;
    wire FP16_5[1-1:0] narrow__data_out;
    wire narrow__debugen_in;
    FpConverterFP32_8_FP16_5 #(
        1
,       0
,       1
,       1
    ) narrow (
        .clk(clk)
,       .reset(reset)
,       .data_in(narrow__data_in)
,       .data_out(narrow__data_out)
,       .debugen_in(narrow__debugen_in)
    );
    wire signed[1-1:0][31:0] int_converter__data_in;
    wire FP32_8[1-1:0] int_converter__data_out;
    wire int_converter__debugen_in;
    FpConverterlogicsigned31_0_FP32_8 #(
        1
,       0
,       0
,       0
    ) int_converter (
        .clk(clk)
,       .reset(reset)
,       .data_in(int_converter__data_in)
,       .data_out(int_converter__data_out)
,       .debugen_in(int_converter__debugen_in)
    );
    wire signed[1-1:0][31:0] registered_integer__data_in;
    wire FP32_8[1-1:0] registered_integer__data_out;
    wire registered_integer__debugen_in;
    FpConverterlogicsigned31_0_FP32_8 #(
        1
,       1
,       0
,       0
    ) registered_integer (
        .clk(clk)
,       .reset(reset)
,       .data_in(registered_integer__data_in)
,       .data_out(registered_integer__data_out)
,       .debugen_in(registered_integer__debugen_in)
    );
    wire signed[1-1:0][31:0] half_integer__data_in;
    wire FP16_5[1-1:0] half_integer__data_out;
    wire half_integer__debugen_in;
    FpConverterlogicsigned31_0_FP16_5 #(
        1
,       0
,       0
,       0
    ) half_integer (
        .clk(clk)
,       .reset(reset)
,       .data_in(half_integer__data_in)
,       .data_out(half_integer__data_out)
,       .debugen_in(half_integer__debugen_in)
    );
    wire FP64_11[1-1:0] wide__data_in;
    wire FP16_5[1-1:0] wide__data_out;
    wire wide__debugen_in;
    FpConverterFP64_11_FP16_5 #(
        1
,       0
,       1
,       1
    ) wide (
        .clk(clk)
,       .reset(reset)
,       .data_in(wide__data_in)
,       .data_out(wide__data_out)
,       .debugen_in(wide__debugen_in)
    );

    // tmp variables


    function logic[63:0] FpFormatConversion___convert (
        input logic[63:0] value
,       input logic[31:0] width
,       input logic[31:0] exp_width
,       input logic[31:0] out_width
,       input logic[31:0] out_exp_width
,       input logic preserve_subnormals
,       input logic preserve_nan_payload
    );
        logic[31:0] mant_width;
        logic[31:0] out_mant_width;
        logic[63:0] exp_max;
        logic[63:0] mant_max;
        logic[63:0] out_exp_max;
        logic[63:0] out_mant_max;
        logic signed[31:0] exp_bias;
        logic signed[31:0] out_exp_bias;
        logic[63:0] significand;
        logic[63:0] retained;
        logic[63:0] tail;
        logic[63:0] halfway;
        logic[63:0] leading;
        logic[63:0] output_sign;
        logic[63:0] payload;
        logic[31:0] input_exponent;
        logic[31:0] normalize;
        logic signed[31:0] output_exponent;
        logic signed[31:0] shift;
        mant_width=(width - exp_width) - 32'h1;
        out_mant_width=(out_width - out_exp_width) - 32'h1;
        exp_max=((64'h1 <<< exp_width)) - 64'h1;
        mant_max=((64'h1 <<< mant_width)) - 64'h1;
        out_exp_max=((64'h1 <<< out_exp_width)) - 64'h1;
        out_mant_max=((64'h1 <<< out_mant_width)) - 64'h1;
        exp_bias=(('h1 <<< ((exp_width - 32'h1)))) - 'h1;
        out_exp_bias=(('h1 <<< ((out_exp_width - 32'h1)))) - 'h1;
        output_sign=((((value >>> ((width - 32'h1)))) & 64'h1)) <<< ((out_width - 32'h1));
        input_exponent=((value >>> mant_width)) & exp_max;
        significand=value & mant_max;
        if (64'(input_exponent) == exp_max) begin
            payload=signed'(32'((((significand) != '0)) ? ('h1) : ('h0)));
            if (preserve_nan_payload && ((significand) != '0)) begin
                if (out_mant_width>=mant_width) begin
                    payload=significand <<< ((out_mant_width - mant_width));
                end
                else begin
                    payload=significand >>> ((mant_width - out_mant_width));
                end
                if (!((payload) != '0)) begin
                    payload=64'h1;
                end
            end
            return 64'((output_sign | ((out_exp_max <<< out_mant_width))) | payload);
        end
        if (!((input_exponent) != '0) && ((!((significand) != '0) || !preserve_subnormals))) begin
            return output_sign;
        end
        output_exponent=(signed'(32'((((input_exponent) != '0)) ? (input_exponent) : (32'h1))) - exp_bias) + out_exp_bias;
        if (((input_exponent) != '0)) begin
            significand|=64'h1 <<< mant_width;
        end
        else begin
            leading=significand;
            leading|=leading >>> 32'sh1;
            leading|=leading >>> 32'sh2;
            leading|=leading >>> 32'sh4;
            leading|=leading >>> 32'sh8;
            leading|=leading >>> 32'sh10;
            if (($bits(logic[63:0])/8) > 64'h4) begin
                leading|=leading >>> 32'sh20;
            end
            leading^=leading >>> 32'sh1;
            normalize=mant_width - ((((((((32'((((64'(leading) & 64'hFFFFFFFF00000000)) != 64'h0)) <<< 'h5)) | ((32'((((64'(leading) & 64'hFFFF0000FFFF0000)) != 64'h0)) <<< 'h4))) | ((32'((((64'(leading) & 64'hFF00FF00FF00FF00)) != 64'h0)) <<< 'h3))) | ((32'((((64'(leading) & 64'hF0F0F0F0F0F0F0F0)) != 64'h0)) <<< 'h2))) | ((32'((((64'(leading) & 64'hCCCCCCCCCCCCCCCC)) != 64'h0)) <<< 'h1))) | 32'((((64'(leading) & 64'hAAAAAAAAAAAAAAAA)) != 64'h0))));
            significand<<=normalize;
            output_exponent-=signed'(32'(normalize));
        end
        if (output_exponent<=32'sh0 && !preserve_subnormals) begin
            return output_sign;
        end
        if (output_exponent>=signed'(32'(out_exp_max))) begin
            return 64'(output_sign | ((out_exp_max <<< out_mant_width)));
        end
        shift=signed'(32'(mant_width)) - signed'(32'(out_mant_width));
        if (output_exponent<=32'sh0) begin
            shift+='h1 - output_exponent;
        end
        retained=64'h0;
        if (shift<=32'sh0) begin
            retained=significand <<< unsigned'(32'(-shift));
        end
        else begin
            if (shift < signed'(32'((($bits(logic[63:0])/8)*64'h8)))) begin
                retained=significand >>> unsigned'(32'(shift));
                tail=significand & ((((64'h1 <<< unsigned'(32'(shift)))) - 64'h1));
                halfway=64'h1 <<< 32'((shift - 'h1));
                if ((tail > halfway) || (((tail == halfway) && ((((retained & 64'h1))) != '0)))) begin
                    retained=retained+1;
                end
            end
        end
        if (output_exponent<=32'sh0) begin
            output_exponent=(retained>=(64'h1 <<< out_mant_width)) ? ('h1) : ('h0);
        end
        else begin
            if (retained>=(64'h1 <<< ((out_mant_width + 32'h1)))) begin
                retained>>='h1;
                output_exponent=output_exponent+1;
            end
        end
        if (output_exponent>=signed'(32'(out_exp_max))) begin
            return 64'(output_sign | ((out_exp_max <<< out_mant_width)));
        end
        return 64'((output_sign | ((unsigned'(64'(output_exponent)) <<< out_mant_width))) | ((retained & out_mant_max)));
    endfunction

    task FP16_5___convert (
        input FP16_5 _this
,       output FP32_8 to_out
,       input logic preserve_subnormals
,       input logic preserve_nan_payload
    );
    begin: FP16_5___convert
        to_out._.raw=FpFormatConversion___convert(64'(_this._.raw), unsigned'(32'(64'h10)), unsigned'(32'(64'h5)), 32'(unsigned'(64'(64'h20))), 32'(unsigned'(64'(64'h8))), preserve_subnormals, preserve_nan_payload);
    end
    endtask

    always_comb begin : object_comb_func  // object_comb_func
        FP16_5 source;
        FP32_8 result;
        source._.raw=half_in;
        FP16_5___convert(source, result, 1, 1);
        object_comb=result._.raw;
    end

    function FP16_5[1-1:0] half_words_func ();
        half_words[64'h0]._.raw=half_in;
        return half_words;
    endfunction

    function FP32_8[1-1:0] single_words_func ();
        single_words[64'h0]._.raw=single_in;
        return single_words;
    endfunction

    function logic signed[1-1:0][31:0] integer_words_func ();
        integer_words[64'h0]=integer_in;
        return integer_words;
    endfunction

    function FP64_11[1-1:0] double_words_func ();
        double_words[64'h0]._.raw=double_in;
        return double_words;
    endfunction

    generate  // _assign
        assign plain__data_in = half_words_func();
        assign plain__debugen_in=0;
        assign subnormal__data_in = half_words_func();
        assign subnormal__debugen_in=0;
        assign payload__data_in = half_words_func();
        assign payload__debugen_in=0;
        assign exact__data_in = half_words_func();
        assign exact__debugen_in=0;
        assign narrow__data_in = single_words_func();
        assign narrow__debugen_in=0;
        assign int_converter__data_in = integer_words_func();
        assign int_converter__debugen_in=0;
        assign registered_integer__data_in = integer_words_func();
        assign registered_integer__debugen_in=0;
        assign half_integer__data_in = integer_words_func();
        assign half_integer__debugen_in=0;
        assign wide__data_in = double_words_func();
        assign wide__debugen_in=0;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end

    assign object_out = object_comb;

    assign plain_out = plain__data_out[64'h0]._.raw;

    assign subnormal_out = subnormal__data_out[64'h0]._.raw;

    assign payload_out = payload__data_out[64'h0]._.raw;

    assign exact_out = exact__data_out[64'h0]._.raw;

    assign narrow_out = narrow__data_out[64'h0]._.raw;

    assign integer_out = int_converter__data_out[64'h0]._.raw;

    assign registered_integer_out = registered_integer__data_out[64'h0]._.raw;

    assign half_integer_out = half_integer__data_out[64'h0]._.raw;

    assign wide_out = wide__data_out[64'h0]._.raw;


endmodule
