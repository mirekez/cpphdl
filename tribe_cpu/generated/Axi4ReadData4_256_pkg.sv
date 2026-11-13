package Axi4ReadData4_256_pkg;

typedef struct packed {
    logic[4-1:0] _align0;
    logic[4-1:0] id;
    logic[7-1:0] _align2;
    logic last;
    logic[256-1:0] data;
    logic[7-1:0] _align1;
    logic valid;
} Axi4ReadData4_256;


endpackage
