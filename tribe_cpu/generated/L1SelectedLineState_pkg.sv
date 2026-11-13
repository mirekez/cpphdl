package L1SelectedLineState_pkg;

typedef struct packed {
    logic[7-1:0] _align0;
    logic valid;
    logic[32-1:0] addr;
    logic[128-1:0] odd;
    logic[128-1:0] even;
} L1SelectedLineState;


endpackage
