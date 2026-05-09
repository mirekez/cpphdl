package PayloadChoice_pkg;

typedef union packed {
    logic[8-1:0] raw;
    struct packed {
        logic[5-1:0] value;
        logic[3-1:0] tag;
    } s;
} PayloadChoice;


endpackage
