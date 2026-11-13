package TribeIrqDebug_pkg;

typedef struct packed {
    logic[31:0] mideleg;
    logic[31:0] mie;
    logic[31:0] mip;
    logic[7-1:0] _align2;
    logic to_supervisor;
    logic[31:0] cause;
    logic[7-1:0] _align1;
    logic valid;
} TribeIrqDebug;


endpackage
