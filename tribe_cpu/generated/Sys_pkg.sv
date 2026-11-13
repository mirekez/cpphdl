package Sys_pkg;

typedef enum logic[32-1:0] {
    SNONE,
    ECALL,
    EBREAK,
    MRET,
    SRET,
    WFI,
    FENCEI,
    SFENCE_VMA,
    TRAP,
    FENCE
} Sys;


endpackage
