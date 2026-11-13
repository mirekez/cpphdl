package TribeSbiDebug_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic handled;
    logic[7-1:0] _align3;
    logic noop;
    logic[7-1:0] _align2;
    logic base;
    logic[31:0] a0;
    logic[31:0] a6;
    logic[31:0] a7;
    logic[7-1:0] _align1;
    logic ecall;
} TribeSbiDebug;


endpackage
