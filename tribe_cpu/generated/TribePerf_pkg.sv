package TribePerf_pkg;
import L1CachePerf_pkg::*;

typedef struct packed {
    L1CachePerf dcache;
    L1CachePerf icache;
    logic[4-1:0] _align1;
    logic[1-1:0] icache_wait;
    logic[1-1:0] dcache_wait;
    logic[1-1:0] branch_stall;
    logic[1-1:0] hazard_stall;
} TribePerf;


endpackage
