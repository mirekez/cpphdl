`default_nettype none

import Predef_pkg::*;
import FP32_8_pkg::*;
import FpFormatConversion_pkg::*;


module FpConverterlogicsigned31_0_FP32_8 #(
    parameter LENGTH = 1
,   parameter USE_REG = 0
,   parameter PRESERVE_SUBNORMALS = 0
,   parameter PRESERVE_NAN_PAYLOAD = 0
 )
 (
    input wire clk
,   input wire reset
,   input wire signed[LENGTH-1:0][31:0] data_in
,   output wire FP32_8[LENGTH-1:0] data_out
,   input wire debugen_in
);


    // regs and combs
    FP32_8[LENGTH-1:0] out_reg;
    FP32_8[LENGTH-1:0] conv_comb;

    // members

    // tmp variables


    function logic[31:0] FP32_8___from_int_raw (input logic signed[31:0] value);
        logic[63:0] output_sign;
        logic[31:0] magnitude;
        logic[31:0] leading;
        logic[31:0] retained;
        logic[31:0] tail;
        logic[31:0] halfway;
        logic[31:0] msb;
        logic[31:0] exponent_work;
        logic[31:0] shift;
        if (!((value) != '0)) begin
            return 32'h0;
        end
        output_sign=(value < 32'sh0) ? (64'h1 <<< ((unsigned'(64'(64'h20)) - 64'h1))) : (64'h0);
        magnitude=(value < 32'sh0) ? ('h0 - unsigned'(32'(value))) : (unsigned'(32'(value)));
        leading=magnitude;
        leading|=leading >>> 32'sh1;
        leading|=leading >>> 32'sh2;
        leading|=leading >>> 32'sh4;
        leading|=leading >>> 32'sh8;
        leading|=leading >>> 32'sh10;
        leading^=leading >>> 32'sh1;
        msb=(((((32'((((leading & 'hFFFF0000)) != 32'h0)) <<< 'h4)) | ((32'((((leading & 'hFF00FF00)) != 32'h0)) <<< 'h3))) | ((32'((((leading & 'hF0F0F0F0)) != 32'h0)) <<< 'h2))) | ((32'((((leading & 'hCCCCCCCC)) != 32'h0)) <<< 'h1))) | 32'((((leading & 'hAAAAAAAA)) != 32'h0));
        exponent_work=msb + unsigned'(32'(signed'(32'h7F)));
        if (msb<=unsigned'(64'(64'h17))) begin
            retained=magnitude <<< ((unsigned'(64'(64'h17)) - 64'(msb)));
        end
        else begin
            shift=64'(msb) - unsigned'(64'(64'h17));
            retained=magnitude >>> shift;
            tail=magnitude & ((((32'h1 <<< shift)) - 32'h1));
            halfway=32'h1 <<< ((shift - 32'h1));
            if ((tail > halfway) || (((tail == halfway) && ((((retained & 32'h1))) != '0)))) begin
                retained=retained+1;
            end
        end
        if (retained>=(32'h1 <<< ((unsigned'(64'(64'h17)) + 64'h1)))) begin
            retained>>='h1;
            exponent_work=exponent_work+1;
        end
        if (exponent_work>=unsigned'(64'(64'hFF))) begin
            return 32'(output_sign | ((unsigned'(64'(64'hFF)) <<< unsigned'(64'(64'h17)))));
        end
        return 32'((output_sign | ((64'(exponent_work) <<< unsigned'(64'(64'h17))))) | ((64'(retained) & unsigned'(64'(64'h7FFFFF)))));
    endfunction

    function FP32_8 convert_value (input logic signed[31:0] value);
        FP32_8 converted;
        if ((('h1) != '0)) begin
            converted._.raw=FP32_8___from_int_raw(value);
        end
        else begin
        end
        return converted;
    endfunction

    always_comb begin : conv_comb_func  // conv_comb_func
        if (LENGTH > 64'h0) begin
            conv_comb[64'h0] = convert_value(data_in[64'h0]);
        end
        if (LENGTH > 64'h1) begin
        end
        if (LENGTH > 64'h2) begin
        end
        if (LENGTH > 64'h3) begin
        end
        if (LENGTH > 64'h4) begin
        end
        if (LENGTH > 64'h5) begin
        end
        if (LENGTH > 64'h6) begin
        end
        if (LENGTH > 64'h7) begin
        end
        if (LENGTH > 64'h8) begin
        end
        if (LENGTH > 64'h9) begin
        end
        if (LENGTH > 64'hA) begin
        end
        if (LENGTH > 64'hB) begin
        end
        if (LENGTH > 64'hC) begin
        end
        if (LENGTH > 64'hD) begin
        end
        if (LENGTH > 64'hE) begin
        end
        if (LENGTH > 64'hF) begin
        end
    end

    task _work (input logic reset);
    begin: _work
        if (USE_REG) begin
            out_reg <= conv_comb;
        end
        if (reset) begin
            disable _work;
        end
        if (debugen_in) begin
            $write("%m: input: %x, output: %x\n", data_in, data_out);
        end
    end
    endtask

    generate  // _assign
    endgenerate

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

    always @(posedge clk) begin

            _work(reset);

    end

    assign data_out = (USE_REG) ? (out_reg) : (conv_comb);


endmodule
