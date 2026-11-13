package TribeSbiDecodeState_pkg;

typedef struct packed {
    logic[32-1:0] ret_a1;
    logic[32-1:0] a7;
    logic[32-1:0] a6;
    logic[32-1:0] a1;
    logic[32-1:0] a0;
    logic[7-1:0] _align10;
    logic remote_sfence_vma;
    logic[7-1:0] _align9;
    logic remote_fence_i;
    logic[7-1:0] _align8;
    logic send_ipi;
    logic[7-1:0] _align7;
    logic writes_a1;
    logic[7-1:0] _align6;
    logic noop;
    logic[7-1:0] _align5;
    logic base;
    logic[7-1:0] _align4;
    logic set_timer;
    logic[7-1:0] _align3;
    logic handled;
    logic[7-1:0] _align2;
    logic valid;
    logic[7-1:0] _align1;
    logic args_valid;
} TribeSbiDecodeState;


endpackage
