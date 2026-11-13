package TribeCacheDebug_pkg;

typedef struct packed {
    logic[7:0] dcache_cpu_wmask;
    logic[31:0] dcache_cpu_wdata;
    logic[31:0] dcache_cpu_addr;
    logic[7-1:0] _align6;
    logic dcache_cpu_write;
    logic[7-1:0] _align5;
    logic dcache_cpu_read;
    logic[31:0] dcache_read_data;
    logic[31:0] dcache_read_addr;
    logic[7-1:0] _align4;
    logic dcache_read_valid;
    logic[7-1:0] _align3;
    logic icache_stall_in;
    logic[7-1:0] _align2;
    logic icache_read_in;
    logic[31:0] icache_read_addr;
    logic[7-1:0] _align1;
    logic icache_read_valid;
} TribeCacheDebug;


endpackage
