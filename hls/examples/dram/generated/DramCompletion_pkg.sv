package DramCompletion_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic valid;
    logic[31:0] error;
    logic[31:0] scale;
    logic[31:0] tag;
    logic[63:0] sum;
} DramCompletion;


endpackage
