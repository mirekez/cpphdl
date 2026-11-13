package Axi4WriteData256_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic last;
    logic[32-1:0] strb;
    logic[256-1:0] data;
    logic[7-1:0] _align1;
    logic valid;
} Axi4WriteData256;


endpackage
