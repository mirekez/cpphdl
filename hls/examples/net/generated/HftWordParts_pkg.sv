package HftWordParts_pkg;

typedef struct packed {
    logic[31:0] udp1;
    logic[31:0] udp0;
    logic[31:0] ip1;
    logic[31:0] ip0;
    logic[31:0] metadata;
    logic[31:0] word;
} HftWordParts;


endpackage
