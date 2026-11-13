package Csr_pkg;

typedef enum logic[32-1:0] {
    CNONE,
    CSRRW,
    CSRRS,
    CSRRC,
    CSRRWI,
    CSRRSI,
    CSRRCI
} Csr;


endpackage
