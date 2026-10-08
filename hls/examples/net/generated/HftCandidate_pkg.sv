package HftCandidate_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic crc_error;
    logic[7-1:0] _align4;
    logic size_error;
    logic[7-1:0] _align3;
    logic valid;
    logic[7-1:0] _align2;
    logic ask_liquid;
    logic[7-1:0] _align1;
    logic bid_liquid;
    logic[31:0] ask;
    logic[31:0] bid;
    logic[31:0] symbol;
    logic[31:0] _sequence;
} HftCandidate;


endpackage
