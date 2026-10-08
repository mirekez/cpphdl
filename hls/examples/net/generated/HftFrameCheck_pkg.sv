package HftFrameCheck_pkg;
import HftCollection_pkg::*;
import HftQuote_pkg::*;

typedef struct packed {
    logic[7-1:0] _align0;
    logic size_error;
    logic[7-1:0] _align2;
    logic normal_length;
    logic[7-1:0] _align1;
    logic complete;
    HftCollection collection;
} HftFrameCheck;


endpackage
