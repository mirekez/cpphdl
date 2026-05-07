package UnionWithStruct_pkg;

typedef union packed {
    struct packed {
        logic[24-1:0] _pad1;
        logic[5-1:0] other1;
        logic[11-1:0] other0;
    } other;
    struct packed {
        logic[5-1:0] _align0;
        logic[3-1:0] us1;
        UnionStruct nested;
        logic[6-1:0] _align1;
        logic[2-1:0] us0;
    } branch;
} UnionWithStruct;


endpackage
