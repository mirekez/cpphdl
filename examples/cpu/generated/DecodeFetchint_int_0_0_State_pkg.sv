package DecodeFetchint_int_0_0_State_pkg;

typedef struct packed {
    logic[5-1:0] rs2;
    logic[5-1:0] rs1;
    logic[5-1:0] rd;
    logic[3-1:0] funct3;
    logic[4-1:0] br_op;
    logic[3-1:0] wb_op;
    logic[2-1:0] mem_op;
    logic[4-1:0] alu_op;
    logic[1-1:0] valid;
    logic[31:0] imm;
    logic[31:0] rs2_val;
    logic[31:0] rs1_val;
    logic[31:0] pc;
} DecodeFetchint_int_0_0_State;


endpackage
