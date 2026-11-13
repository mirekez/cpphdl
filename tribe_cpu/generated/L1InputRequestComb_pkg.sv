package L1InputRequestComb_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic issue;
    logic[7-1:0] _align2;
    logic start;
    logic[7-1:0] _align1;
    logic cacheable;
    logic[32-1:0] set;
} L1InputRequestComb;


endpackage
