package TribeWritebackDebug_pkg;

typedef struct packed {
    logic[7:0] state_funct3;
    logic[7:0] state_rd;
    logic[7:0] state_mem_op;
    logic[7:0] state_wb_op;
    logic[31:0] state_pc;
    logic[31:0] alu_addr;
    logic[7-1:0] _align7;
    logic split_load_in;
    logic[7-1:0] _align6;
    logic held_load_valid;
    logic[7-1:0] _align5;
    logic split_high_valid;
    logic[7-1:0] _align4;
    logic split_low_valid;
    logic[31:0] load_addr;
    logic[7-1:0] _align3;
    logic load_data_valid;
    logic[7-1:0] _align2;
    logic mem_wait;
    logic[7-1:0] _align1;
    logic load_ready;
} TribeWritebackDebug;


endpackage
