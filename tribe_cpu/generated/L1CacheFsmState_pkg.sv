package L1CacheFsmState_pkg;

typedef enum logic[64-1:0] {
    L1_ST_IDLE = 64'h0,
    L1_ST_LOOKUP = 64'h1,
    L1_ST_DONE = 64'h2,
    L1_ST_REFILL = 64'h3,
    L1_ST_INIT = 64'h4,
    L1_ST_SELECT = 64'h5,
    L1_ST_ASSEMBLE = 64'h6,
    L1_ST_COMPARE = 64'h7
} L1CacheFsmState;


endpackage
