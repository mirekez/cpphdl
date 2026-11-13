package L1LookupState_pkg;

typedef struct packed {
    logic[8-1:0] way;
    logic[7-1:0] _align1;
    logic hit;
} L1LookupState;


endpackage
