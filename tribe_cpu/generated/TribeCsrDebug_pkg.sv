package TribeCsrDebug_pkg;

typedef struct packed {
    logic[6-1:0] _align0;
    logic[2-1:0] priv;
    logic[31:0] stval;
    logic[31:0] scause;
    logic[31:0] stvec;
    logic[31:0] sepc;
    logic[31:0] mtval;
    logic[31:0] mcause;
    logic[31:0] mepc;
    logic[31:0] mtvec;
    logic[31:0] mstatus;
    logic[31:0] satp;
} TribeCsrDebug;


endpackage
