package L1RefillState_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic req_data_valid;
    logic[32-1:0] req_data;
    logic[128-1:0] odd_line;
    logic[128-1:0] even_line;
    logic[8-1:0] beat;
} L1RefillState;


endpackage
