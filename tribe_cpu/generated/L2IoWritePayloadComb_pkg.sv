package L2IoWritePayloadComb_pkg;

typedef struct packed {
    logic[32-1:0] strobe;
    logic[256-1:0] data;
} L2IoWritePayloadComb;


endpackage
