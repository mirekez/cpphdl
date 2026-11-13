package L2CacheFsmState_pkg;

typedef enum logic[64-1:0] {
    ST_IDLE = 64'h0,
    ST_INIT = 64'h1,
    ST_LOOKUP = 64'h2,
    ST_AXI_AR = 64'h3,
    ST_AXI_R = 64'h4,
    ST_DONE = 64'h5,
    ST_CROSS_AR0 = 64'h6,
    ST_CROSS_R0 = 64'h7,
    ST_CROSS_AR1 = 64'h8,
    ST_CROSS_R1 = 64'h9,
    ST_EVICT_AW = 64'hA,
    ST_EVICT_W = 64'hB,
    ST_EVICT_B = 64'hC,
    ST_CROSS_WRITE_LOOKUP = 64'hD,
    ST_CROSS_DONE = 64'hE,
    ST_IO_AW = 64'hF,
    ST_IO_W = 64'h10,
    ST_IO_B = 64'h11,
    ST_IO_AR = 64'h12,
    ST_IO_R = 64'h13,
    ST_READ = 64'h14,
    ST_LOOKUP_CAPTURE = 64'h15,
    ST_CROSS_WRITE_READ = 64'h16,
    ST_CROSS_WRITE_CAPTURE = 64'h17,
    ST_LOOKUP_RESULT = 64'h18,
    ST_CROSS_WRITE_RESULT = 64'h19,
    ST_AXI_R_WRITE = 64'h1A,
    ST_IO_R_RESULT = 64'h1B
} L2CacheFsmState;


endpackage
