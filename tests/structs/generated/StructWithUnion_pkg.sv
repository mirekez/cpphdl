package StructWithUnion_pkg;
import InnerUnion_pkg::*;

typedef struct packed {
    logic[3-1:0] _align0;
    logic[5-1:0] suffix;
    InnerUnion inner;
    logic[5-1:0] _align1;
    logic[3-1:0] prefix;
} StructWithUnion;


endpackage
