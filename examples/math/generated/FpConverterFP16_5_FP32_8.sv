`default_nettype none

import Predef_pkg::*;
import FP16_5_pkg::*;
import FP32_8_pkg::*;


module FpConverterFP16_5_FP32_8 #(
    parameter LENGTH = 8
,   parameter USE_REG = 0
 )
 (
    input wire clk
,   input wire reset
,   input FP16_5[LENGTH-1:0] data_in
,   output FP32_8[LENGTH-1:0] data_out
,   input wire debugen_in
);

    FP32_8[LENGTH-1:0] out_reg;
    FP32_8[LENGTH-1:0] conv_comb;


    FP32_8[LENGTH-1:0] out_reg_next;


    task FP___16_5convert (
        input FP16_5 _this
,       output FP32_8 to_out
    );
    begin: FP___16_5convert
        to_out._._.sign = _this._._.sign;
        to_out._._.exponent = _this._._.exponent - ((1 <<< (FP16_5_pkg::EXP_WIDTH - 1)) - 1) + ((1 <<< (FP32_8_pkg::EXP_WIDTH - 1)) - 1);
        if (FP32_8_pkg::MANT_WIDTH >= FP16_5_pkg::MANT_WIDTH) begin
            to_out._._.mantissa = _this._._.mantissa <<< (FP32_8_pkg::MANT_WIDTH - FP16_5_pkg::MANT_WIDTH);
        end
        else begin
            if (FP16_5_pkg::MANT_WIDTH - FP32_8_pkg::MANT_WIDTH < FP16_5_pkg::MANT_WIDTH) begin
                to_out._._.mantissa = _this._._.mantissa >>> (FP16_5_pkg::MANT_WIDTH - FP32_8_pkg::MANT_WIDTH);
            end
            else begin
                to_out._._.mantissa = 0;
            end
        end
        if (_this._._.exponent == ((1 <<< FP16_5_pkg::EXP_WIDTH) - 1) || (_this._._.exponent > ((1 <<< (FP16_5_pkg::EXP_WIDTH - 1)) - 1) && _this._._.exponent - ((1 <<< (FP16_5_pkg::EXP_WIDTH - 1)) - 1) > (1 <<< (FP32_8_pkg::EXP_WIDTH - 1)))) begin
            to_out._._.exponent = ((1 <<< FP32_8_pkg::EXP_WIDTH) - 1);
            to_out._._.mantissa = 0;
        end
        if (_this._._.exponent == 0 || (_this._._.exponent < ((1 <<< (FP16_5_pkg::EXP_WIDTH - 1)) - 1) && ((1 <<< (FP16_5_pkg::EXP_WIDTH - 1)) - 1) - _this._._.exponent >= (1 <<< (FP32_8_pkg::EXP_WIDTH - 1)))) begin
            to_out._._.exponent = 0;
            to_out._._.mantissa = 0;
        end
    end
    endtask

    always @(*) begin  // conv_comb_func
        logic[63:0] i;
        for (i = 0;i < LENGTH;i=i+1) begin
            FP___16_5convert(data_in[i], conv_comb[i]);
        end
    end

    task _work (input logic reset);
    begin: _work
        if (USE_REG) begin
            out_reg_next = conv_comb;
        end
        if (reset) begin
            disable _work;
        end
        if (debugen_in) begin
            $write("%m: input: %x, output: %x\n", data_in, data_out);
        end
    end
    endtask

    generate  // _connect
    endgenerate

    always @(posedge clk) begin
        _work(reset);

        out_reg <= out_reg_next;
    end

    assign data_out = USE_REG ? out_reg : conv_comb;


endmodule
