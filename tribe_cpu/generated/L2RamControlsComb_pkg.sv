package L2RamControlsComb_pkg;

typedef struct packed {
    logic[32-1:0] tag_data;
    logic[4-1:0] _align2;
    logic[4-1:0] tag_write;
    logic[32-1:0][32-1:0] data;
    logic[32-1:0] data_write;
    logic[7-1:0] _align1;
    logic read;
    logic[32-1:0] addr;
} L2RamControlsComb;


endpackage
