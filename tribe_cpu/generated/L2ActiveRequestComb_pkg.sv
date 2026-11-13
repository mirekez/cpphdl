package L2ActiveRequestComb_pkg;
import CacheRequest_pkg::*;

typedef struct packed {
    logic[7-1:0] _align0;
    logic cross_line_read;
    logic[7-1:0] _align1;
    logic valid;
    logic[32-1:0] cache_set;
    CacheRequest request;
} L2ActiveRequestComb;


endpackage
