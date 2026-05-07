package InnerUnion_pkg;

typedef union packed {
    struct packed {
        logic[8-1:0] _pad2;
        logic[1-1:0] _align0;
        logic[7-1:0] _iff;
        logic[3-1:0] _align1;
        logic[1-1:0] ie;
        logic[4-1:0] id;
    } t;
    struct packed {
        logic[5-1:0] _align0;
        logic[3-1:0] ic;
        logic[3-1:0] _align2;
        logic[5-1:0] ib;
        logic[6-1:0] _align1;
        logic[2-1:0] ia;
    } s;
} InnerUnion;


endpackage
