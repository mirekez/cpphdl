package TribeRegsDebug_pkg;

typedef struct packed {
    logic[31:0] data;
    logic[7:0] wr_id;
    logic[7-1:0] _align2;
    logic write_actual;
    logic[7-1:0] _align1;
    logic write;
    logic[31:0] ra;
} TribeRegsDebug;


endpackage
