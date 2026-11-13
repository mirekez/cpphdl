package L2HitLookupComb_pkg;

typedef struct packed {
    logic[256-1:0] beat;
    logic[32-1:0] read_word;
    logic[32-1:0] aligned_next_word;
    logic[32-1:0] aligned_word;
    logic[32-1:0] way;
    logic[7-1:0] _align1;
    logic hit;
} L2HitLookupComb;


endpackage
