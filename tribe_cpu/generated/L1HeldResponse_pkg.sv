package L1HeldResponse_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic valid;
    logic[32-1:0] data;
    logic[32-1:0] addr;
} L1HeldResponse;


endpackage
