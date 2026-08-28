package JsonPayload_pkg;

typedef struct packed {
    logic[1-1:0] _align0;
    logic[7-1:0] data;
    logic[8-1:0] tag;
    logic[7-1:0] _align1;
    logic valid;
} JsonPayload;


endpackage
