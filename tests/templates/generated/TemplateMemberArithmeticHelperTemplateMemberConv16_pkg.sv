package TemplateMemberArithmeticHelperTemplateMemberConv16_pkg;

parameter DEFAULT_EXP_MAX = 'h1F;
parameter CONV_EXP_MAX = ('h1 << 'h8) - 'h1;
parameter DEFAULT_MANT_MAX = 'h3FF;
parameter CONV_MANT_MAX = ('h1 << 'h7) - 'h1;
typedef struct packed {
    logic[1-1:0] _pad1;
} TemplateMemberArithmeticHelperTemplateMemberConv16;


endpackage
