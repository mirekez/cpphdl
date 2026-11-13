package L2AxiRouteComb_pkg;

typedef struct packed {
    logic[8-1:0] aw_sel;
    logic[32-1:0] aw_local_addr;
    logic[32-1:0] aw_full_addr;
    logic[8-1:0] ar_sel;
    logic[32-1:0] ar_local_addr;
    logic[32-1:0] ar_full_addr;
} L2AxiRouteComb;


endpackage
