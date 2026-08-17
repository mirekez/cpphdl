`default_nettype none

import Predef_pkg::*;
import FP16_5_pkg::*;
import FP32_8_pkg::*;


module FpConverterFP16_5_FP32_8 #(
    parameter LENGTH = 16
,   parameter USE_REG = 0
 )
 (
    input wire clk
,   input wire reset
,   input FP16_5[LENGTH-1:0] data_in
,   output FP32_8[LENGTH-1:0] data_out
,   input wire debugen_in
);


    // regs and combs
    FP32_8[16-1:0] out_reg;
    FP32_8[LENGTH-1:0] conv_comb;

    // members

    // tmp variables


    task FP16_5___convert (
        input FP16_5 _this
,       output FP32_8 to_out
    );
    begin: FP16_5___convert
        logic[63:0] mant_work;
        logic[63:0] mant_keep;
        logic[63:0] round_bits;
        logic[63:0] round_half;
        logic[63:0] dst_mant_max;
        logic[63:0] exponent_work;
        logic[31:0] shift;
        to_out._.raw='h0;
        to_out._._.sign=_this._._.sign;
        if (_this._._.exponent == 'h0) begin
            to_out._._.exponent='h0;
            to_out._._.mantissa='h0;
        end
        else begin
            if (_this._._.exponent == FP16_5_pkg::EXP_MAX) begin
                to_out._._.exponent=FP32_8_pkg::EXP_MAX;
                to_out._._.mantissa=(_this._._.mantissa) ? ('h1) : ('h0);
            end
            else begin
                if (FP16_5_pkg::EXP_BIAS>=FP32_8_pkg::EXP_BIAS) begin
                end
                else begin
                    exponent_work=unsigned'(64'(_this._._.exponent)) + unsigned'(64'(((FP32_8_pkg::EXP_BIAS - FP16_5_pkg::EXP_BIAS))));
                end
                if (exponent_work == 'h0) begin
                    to_out._._.exponent='h0;
                    to_out._._.mantissa='h0;
                end
                else begin
                    if (exponent_work>=FP32_8_pkg::EXP_MAX) begin
                        to_out._._.exponent=FP32_8_pkg::EXP_MAX;
                        to_out._._.mantissa='h0;
                    end
                    else begin
                        dst_mant_max=FP32_8_pkg::MANT_MAX;
                        if (FP32_8_pkg::MANT_WIDTH>=FP16_5_pkg::MANT_WIDTH) begin
                            mant_work=unsigned'(64'(_this._._.mantissa)) <<< ((FP32_8_pkg::MANT_WIDTH - FP16_5_pkg::MANT_WIDTH));
                        end
                        else begin
                        end
                        if (exponent_work>=FP32_8_pkg::EXP_MAX) begin
                            to_out._._.exponent=FP32_8_pkg::EXP_MAX;
                            to_out._._.mantissa='h0;
                        end
                        else begin
                            to_out._._.exponent=exponent_work;
                            to_out._._.mantissa=mant_work;
                        end
                    end
                end
            end
        end
    end
    endtask

    always_comb begin : conv_comb_func  // conv_comb_func
        FP32_8 converted;
        if (LENGTH > 'h0) begin
            FP16_5___convert(data_in['h0], converted);
            conv_comb['h0] = converted;
        end
        if (LENGTH > 'h1) begin
            FP16_5___convert(data_in['h1], converted);
            conv_comb['h1] = converted;
        end
        if (LENGTH > 'h2) begin
            FP16_5___convert(data_in['h2], converted);
            conv_comb['h2] = converted;
        end
        if (LENGTH > 'h3) begin
            FP16_5___convert(data_in['h3], converted);
            conv_comb['h3] = converted;
        end
        if (LENGTH > 'h4) begin
            FP16_5___convert(data_in['h4], converted);
            conv_comb['h4] = converted;
        end
        if (LENGTH > 'h5) begin
            FP16_5___convert(data_in['h5], converted);
            conv_comb['h5] = converted;
        end
        if (LENGTH > 'h6) begin
            FP16_5___convert(data_in['h6], converted);
            conv_comb['h6] = converted;
        end
        if (LENGTH > 'h7) begin
            FP16_5___convert(data_in['h7], converted);
            conv_comb['h7] = converted;
        end
        if (LENGTH > 'h8) begin
            FP16_5___convert(data_in['h8], converted);
            conv_comb['h8] = converted;
        end
        if (LENGTH > 'h9) begin
            FP16_5___convert(data_in['h9], converted);
            conv_comb['h9] = converted;
        end
        if (LENGTH > 'hA) begin
            FP16_5___convert(data_in['hA], converted);
            conv_comb['hA] = converted;
        end
        if (LENGTH > 'hB) begin
            FP16_5___convert(data_in['hB], converted);
            conv_comb['hB] = converted;
        end
        if (LENGTH > 'hC) begin
            FP16_5___convert(data_in['hC], converted);
            conv_comb['hC] = converted;
        end
        if (LENGTH > 'hD) begin
            FP16_5___convert(data_in['hD], converted);
            conv_comb['hD] = converted;
        end
        if (LENGTH > 'hE) begin
            FP16_5___convert(data_in['hE], converted);
            conv_comb['hE] = converted;
        end
        if (LENGTH > 'hF) begin
            FP16_5___convert(data_in['hF], converted);
            conv_comb['hF] = converted;
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

    always @(posedge clk) begin

        _work(reset);

    end

    assign data_out = (USE_REG) ? (out_reg) : (conv_comb);


endmodule
