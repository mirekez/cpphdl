package Conv16_pkg;

typedef struct packed {
    union packed {
        struct packed {
            logic[1-1:0] sign;
            logic[8-1:0] exponent;
            logic[7-1:0] mantissa;
        } data;
        logic[15:0] raw;
    } _;
} Conv16;


endpackage
