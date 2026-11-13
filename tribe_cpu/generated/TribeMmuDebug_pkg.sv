package TribeMmuDebug_pkg;

typedef struct packed {
    logic[31:0] ptw_word;
    logic[7-1:0] _align6;
    logic dmmu_fault;
    logic[7-1:0] _align5;
    logic dmmu_busy;
    logic[31:0] dmmu_ptw_addr;
    logic[7-1:0] _align4;
    logic dmmu_ptw_read;
    logic[31:0] immu_last_pte;
    logic[31:0] immu_last_addr;
    logic[31:0] immu_paddr;
    logic[7-1:0] _align3;
    logic immu_fault;
    logic[7-1:0] _align2;
    logic immu_busy;
    logic[31:0] immu_ptw_addr;
    logic[7-1:0] _align1;
    logic immu_ptw_read;
} TribeMmuDebug;


endpackage
