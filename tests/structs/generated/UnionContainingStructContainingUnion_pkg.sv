package UnionContainingStructContainingUnion_pkg;
import StructWithUnion_pkg::*;
import InnerUnion_pkg::*;

typedef union packed {
    struct packed {
        logic[32-1:0] _pad2;
        logic[5-1:0] _align0;
        logic[2-1:0] alt2;
        logic[9-1:0] alt1;
        logic[1-1:0] _align1;
        logic[7-1:0] alt0;
    } alt;
    struct packed {
        logic[2-1:0] _align0;
        logic[6-1:0] ub;
        StructWithUnion nested;
        logic[7-1:0] _align1;
        logic[1-1:0] ua;
    } wrapped;
} UnionContainingStructContainingUnion;


endpackage
