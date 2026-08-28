package TemplateStructPayload3_TemplateStructSpecSmall_alpha_pkg;

parameter WIDTH_VALUE = 64'h3;
parameter FORMAT_BITS = 64'h5;
parameter TAG_FIRST = 'h61;
typedef struct packed {
    logic[5-1:0] formatted;
    logic[3-1:0] selected;
    logic[15:0] raw;
} TemplateStructPayload3_TemplateStructSpecSmall_alpha;


endpackage
