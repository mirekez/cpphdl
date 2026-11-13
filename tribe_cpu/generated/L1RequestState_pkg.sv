package L1RequestState_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic cache_disable;
    logic[7-1:0] _align2;
    logic cacheable;
    logic[7-1:0] _align1;
    logic read;
    logic[32-1:0] addr;
} L1RequestState;


endpackage
