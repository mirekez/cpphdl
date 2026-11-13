package TribeCoreDebug_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic memory_wait;
    logic[7-1:0] _align1;
    logic fetch_valid;
    logic[31:0] pc;
} TribeCoreDebug;


endpackage
