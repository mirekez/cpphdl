package L2EvictCandidateComb_pkg;

typedef struct packed {
    logic[256-1:0] line;
    logic[32-1:0] tag;
    logic[7-1:0] _align2;
    logic dirty;
    logic[7-1:0] _align1;
    logic valid;
    logic[32-1:0] way;
} L2EvictCandidateComb;


endpackage
