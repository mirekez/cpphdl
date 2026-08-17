package Trap_pkg;

typedef enum {
    TNONE,
    INST_MISALIGNED,
    ILLEGAL_INST,
    BREAKPOINT,
    LOAD_MISALIGNED,
    STORE_MISALIGNED,
    INST_PAGE_FAULT,
    LOAD_PAGE_FAULT,
    STORE_PAGE_FAULT,
    ECALL_U,
    ECALL_S,
    ECALL_M
} Trap;


endpackage
