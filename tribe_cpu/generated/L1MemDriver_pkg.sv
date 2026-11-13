package L1MemDriver_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic cache_disable;
    logic[8-1:0] write_mask;
    logic[32-1:0] write_data;
    logic[32-1:0] addr;
    logic[7-1:0] _align2;
    logic write;
    logic[7-1:0] _align1;
    logic read;
} L1MemDriver;


endpackage
