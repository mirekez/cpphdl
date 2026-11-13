package CacheResponse_pkg;
import Axi4WriteResponse4_pkg::*;
import Axi4ReadData4_256_pkg::*;

typedef struct packed {
    Axi4ReadData4_256 r;
    Axi4WriteResponse4 b;
    logic[32-1:0] addr;
    logic[7-1:0] _align4;
    logic data_port;
    logic[7-1:0] _align3;
    logic write;
    logic[7-1:0] _align2;
    logic read;
    logic[7-1:0] _align1;
    logic valid;
} CacheResponse;


endpackage
