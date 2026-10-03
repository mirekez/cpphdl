package HftCandidate_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic crc_error;
    logic[7-1:0] _align2;
    logic size_error;
    logic[7-1:0] _align1;
    logic valid;
    logic[31:0] ask;
    logic[31:0] bid;
    logic[31:0] _sequence;
} HftCandidate;


endpackage
