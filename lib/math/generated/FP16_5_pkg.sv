package FP16_5_pkg;

parameter MANT_WIDTH = 64'hA;
parameter WIDTH = 64'h10;
parameter EXP_WIDTH = 64'h5;
parameter EXP_MAX = 64'h1F;
parameter MANT_MAX = 64'h3FF;
parameter EXP_BIAS = 'hF;
typedef struct packed {
    union packed {
        struct packed {
            logic[1-1:0] sign;
            logic[5-1:0] exponent;
            logic[10-1:0] mantissa;
        } _;
        logic[16-1:0] raw;
    } _;
} FP16_5;


endpackage
