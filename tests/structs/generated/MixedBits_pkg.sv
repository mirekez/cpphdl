package MixedBits_pkg;

typedef struct packed {
    logic[6-1:0] _align0;
    logic[2-1:0] tail;
    logic[5-1:0] _align2;
    logic[3-1:0] state;
    logic[3-1:0] _align1;
    logic[4-1:0] code;
    logic[1-1:0] flag;
} MixedBits;


endpackage
