package L1CachePerf_pkg;

typedef struct packed {
    logic[5-1:0] _align0;
    logic[3-1:0] state;
    logic[3-1:0] _align1;
    logic[1-1:0] issue_wait;
    logic[1-1:0] init_wait;
    logic[1-1:0] refill_wait;
    logic[1-1:0] lookup_wait;
    logic[1-1:0] hit;
} L1CachePerf;


endpackage
