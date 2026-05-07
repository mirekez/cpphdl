package ArrayPayload_pkg;

typedef struct packed {
    logic[3-1:0] _align0;
    logic[5-1:0] tail;
    logic[1-1:0][16-1:0] halfs;
    logic[5-1:0] _align2;
    logic[3-1:0] mid;
    logic[3-1:0][8-1:0] bytes;
    logic[4-1:0] _align1;
    logic[4-1:0] prefix;
} ArrayPayload;


endpackage
