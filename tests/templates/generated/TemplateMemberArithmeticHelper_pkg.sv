package TemplateMemberArithmeticHelper_pkg;

parameter DEFAULT_EXP_MAX = 'h1F;
parameter CONV_EXP_MAX = ('h1 << CONV_TYPE_pkg::EXP_WIDTH) - 'h1;
parameter DEFAULT_MANT_MAX = 'h3FF;
parameter CONV_MANT_MAX = ('h1 << CONV_TYPE_pkg::MANT_WIDTH) - 'h1;
typedef struct packed {
    logic[1-1:0] _pad1;
} TemplateMemberArithmeticHelper;


endpackage
