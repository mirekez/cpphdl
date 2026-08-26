package TemplateStructPayload6_TemplateStructSpecLarge_beta_pkg;

parameter WIDTH_VALUE = 64'h6;
parameter FORMAT_BITS = 64'h9;
parameter TAG_FIRST = 'h62;
typedef struct packed {
    logic[1-1:0] _align0;
    logic[9-1:0] formatted;
    logic[6-1:0] selected;
    logic[15:0] raw;
} TemplateStructPayload6_TemplateStructSpecLarge_beta;


endpackage
