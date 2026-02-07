package FP32_8_pkg;

parameter WIDTH = 32;
parameter EXP_WIDTH = 8;
parameter MANT_WIDTH = 32 - 8 - 1;
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
