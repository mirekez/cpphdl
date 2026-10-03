package HftFraming_pkg;

parameter MIN_BYTES = 'h40;
parameter MAX_BYTES = 'h64;
typedef struct packed {
    logic[1-1:0] _pad1;
} HftFraming;


endpackage
