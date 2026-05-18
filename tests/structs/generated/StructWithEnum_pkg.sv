package StructWithEnum_pkg;
import AlignMode_MODES_pkg::*;
import TinyBits_pkg::*;

typedef struct packed {
    logic[5-1:0] _align0;
    logic[3-1:0] suffix;
    TinyBits tiny;
    AlignMode_MODES mode;
    logic[6-1:0] _align1;
    logic[2-1:0] prefix;
} StructWithEnum;


endpackage
