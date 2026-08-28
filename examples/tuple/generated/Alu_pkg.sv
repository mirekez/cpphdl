package Alu_pkg;

typedef enum logic[32-1:0] {
    ANONE,
    ADD,
    SUB,
    AND,
    OR,
    XOR,
    SLL,
    SRL,
    SRA,
    SLT,
    SLTU,
    PASS,
    MUL,
    MULH,
    DIV,
    REM
} Alu;


endpackage
