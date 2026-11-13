package L2RequestGeometryComb_pkg;

typedef struct packed {
    logic[8-1:0] cross_write_mask;
    logic[32-1:0] cross_write_data;
    logic[7-1:0] _align3;
    logic addr_in_memory;
    logic[7-1:0] _align2;
    logic cross_line_write;
    logic[7-1:0] _align1;
    logic cross_beat_read;
    logic[32-1:0] tag;
    logic[32-1:0] beat;
    logic[32-1:0] word;
    logic[32-1:0] set;
} L2RequestGeometryComb;


endpackage
