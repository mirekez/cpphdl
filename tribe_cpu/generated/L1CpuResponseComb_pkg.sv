package L1CpuResponseComb_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic busy;
    logic[7-1:0] _align1;
    logic valid;
    logic[32-1:0] addr;
    logic[32-1:0] data;
} L1CpuResponseComb;


endpackage
