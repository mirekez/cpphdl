package TribeDecodeDebug_pkg;

typedef struct packed {
    logic[31:0] imm;
    logic[7:0] br;
    logic[31:0] pc;
    logic[31:0] instr;
} TribeDecodeDebug;


endpackage
