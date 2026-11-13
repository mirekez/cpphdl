package L1RequestGeometryComb_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic direct_cross_beat;
    logic[32-1:0] refill_beat;
    logic[32-1:0] word;
    logic[32-1:0] tag;
    logic[32-1:0] set;
} L1RequestGeometryComb;


endpackage
