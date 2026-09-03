package Native16_pkg;

typedef struct packed {
    union packed {
        struct packed {
            logic[1-1:0] sign;
            logic[5-1:0] exponent;
            logic[10-1:0] mantissa;
        } data;
        logic[15:0] raw;
    } _;
} Native16;


endpackage
