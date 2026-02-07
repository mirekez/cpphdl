package Rv32im_pkg;

typedef struct packed {
    union packed {
        struct packed {
            logic[1-1:0] imm20;
            logic[10-1:0] imm10_1;
            logic[1-1:0] imm11;
            logic[8-1:0] imm19_12;
            logic[5-1:0] rd;
            logic[7-1:0] opcode;
        } j;
        struct packed {
            logic[20-1:0] imm31_12;
            logic[5-1:0] rd;
            logic[7-1:0] opcode;
        } u;
        struct packed {
            logic[1-1:0] imm12;
            logic[6-1:0] imm10_5;
            logic[5-1:0] rs2;
            logic[5-1:0] rs1;
            logic[3-1:0] funct3;
            logic[4-1:0] imm4_1;
            logic[1-1:0] imm11;
            logic[7-1:0] opcode;
        } b;
        struct packed {
            logic[7-1:0] imm11_5;
            logic[5-1:0] rs2;
            logic[5-1:0] rs1;
            logic[3-1:0] funct3;
            logic[5-1:0] imm4_0;
            logic[7-1:0] opcode;
        } s;
        struct packed {
            logic[12-1:0] imm11_0;
            logic[5-1:0] rs1;
            logic[3-1:0] funct3;
            logic[5-1:0] rd;
            logic[7-1:0] opcode;
        } i;
        struct packed {
            logic[7-1:0] funct7;
            logic[5-1:0] rs2;
            logic[5-1:0] rs1;
            logic[3-1:0] funct3;
            logic[5-1:0] rd;
            logic[7-1:0] opcode;
        } r;
        logic[31:0] raw;
    } _;
} Rv32im;


endpackage
