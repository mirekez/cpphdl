package FP32_8_pkg;

parameter MANT_WIDTH = 64'h17;
parameter WIDTH = 64'h20;
parameter EXP_WIDTH = 64'h8;
parameter EXP_MAX = 64'hFF;
parameter MANT_MAX = 64'h7FFFFF;
parameter EXP_BIAS = 'h7F;
typedef struct packed {
    union packed {
        struct packed {
            logic[1-1:0] sign;
            logic[8-1:0] exponent;
            logic[23-1:0] mantissa;
        } _;
        logic[32-1:0] raw;
    } _;
} FP32_8;


endpackage
