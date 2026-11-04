package FP64_11_pkg;

parameter MANT_WIDTH = 64'h34;
parameter WIDTH = 64'h40;
parameter EXP_WIDTH = 64'hB;
parameter EXP_MAX = 64'(((64'h1 <<< 11)) - 64'h1);
parameter MANT_MAX = 64'(((64'h1 <<< (((64 - 11) - 64'h1)))) - 64'h1);
parameter EXP_BIAS = (('h1 <<< ((11 - 64'h1)))) - 'h1;
typedef struct packed {
    union packed {
        struct packed {
            logic[1-1:0] sign;
            logic[11-1:0] exponent;
            logic[52-1:0] mantissa;
        } _;
        logic[64-1:0] raw;
    } _;
} FP64_11;


endpackage
