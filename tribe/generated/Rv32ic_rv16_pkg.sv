package Rv32ic_rv16_pkg;

typedef union packed {
    struct packed {
        logic[3-1:0] funct3;
        logic[1-1:0] b12;
        logic[5-1:0] rs1;
        logic[5-1:0] rs2;
        logic[2-1:0] opcode;
    } big;
    struct packed {
        logic[3-1:0] funct3;
        logic[1-1:0] b12;
        logic[5-1:0] rs1;
        logic[2-1:0] generic;
        logic[3-1:0] rd_p;
        logic[2-1:0] opcode;
    } avg;
    struct packed {
        logic[3-1:0] funct3;
        logic[1-1:0] b12;
        logic[2-1:0] bits11_10;
        logic[3-1:0] rs1_p;
        logic[2-1:0] bits6_5;
        logic[3-1:0] rd_p;
        logic[2-1:0] opcode;
    } base;
    logic[31:0] raw;
} Rv32ic_rv16;


endpackage
