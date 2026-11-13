package CacheRequest_pkg;

typedef struct packed {
    logic[4-1:0] _align0;
    logic[4-1:0] slave_id;
    logic[8-1:0] slave_index;
    logic[5-1:0] _align6;
    logic[3-1:0] cpu_index;
    logic[7-1:0] _align5;
    logic cache_disable;
    logic[7-1:0] _align4;
    logic from_slave;
    logic[7-1:0] _align3;
    logic port;
    logic[7-1:0] _align2;
    logic write;
    logic[7-1:0] _align1;
    logic read;
    logic[8-1:0] write_word_mask;
    logic[32-1:0] write_strobe;
    logic[8-1:0] write_mask;
    logic[256-1:0] write_beat;
    logic[32-1:0] write_data;
    logic[32-1:0] addr;
} CacheRequest;


endpackage
