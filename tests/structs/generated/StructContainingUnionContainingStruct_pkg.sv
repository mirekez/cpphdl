package StructContainingUnionContainingStruct_pkg;
import UnionWithStruct_pkg::*;

typedef struct packed {
    logic[1-1:0] _align0;
    logic[7-1:0] tail;
    UnionWithStruct u;
    logic[6-1:0] _align1;
    logic[2-1:0] head;
} StructContainingUnionContainingStruct;


endpackage
