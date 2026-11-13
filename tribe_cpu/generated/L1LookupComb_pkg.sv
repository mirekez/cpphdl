package L1LookupComb_pkg;

typedef struct packed {
    logic[32-1:0] data;
    logic[8-1:0] way;
    logic[7-1:0] _align1;
    logic hit;
} L1LookupComb;


endpackage
