package HftQuote_pkg;

typedef struct packed {
    logic[31:0] ask_size;
    logic[31:0] bid_size;
    logic[31:0] ask;
    logic[31:0] bid;
    logic[31:0] symbol;
    logic[31:0] _sequence;
} HftQuote;


endpackage
