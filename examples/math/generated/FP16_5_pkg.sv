package FP16_5_pkg;

parameter WIDTH = 16;
parameter EXP_WIDTH = 5;
parameter MANT_WIDTH = 16 - 5 - 1;
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
