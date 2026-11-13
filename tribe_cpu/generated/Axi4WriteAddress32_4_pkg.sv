package Axi4WriteAddress32_4_pkg;

typedef struct packed {
    logic[4-1:0] _align0;
    logic[4-1:0] id;
    logic[32-1:0] addr;
    logic[7-1:0] _align1;
    logic valid;
} Axi4WriteAddress32_4;


endpackage
