package TemplateMemberNative16_pkg;

parameter MANT_WIDTH = 'hA;
parameter EXP_WIDTH = 'h5;
typedef struct packed {
    union packed {
        struct packed {
            logic[1-1:0] sign;
            logic[5-1:0] exponent;
            logic[10-1:0] mantissa;
        } data;
        logic[15:0] raw;
    } _;
} TemplateMemberNative16;


endpackage
