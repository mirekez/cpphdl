package OuterBits_pkg;
import TinyBits_pkg::*;
import MixedBits_pkg::*;

typedef struct packed {
    logic[7-1:0] _align0;
    logic[1-1:0] last;
    logic[4-1:0] _align3;
    logic[4-1:0] nibble;
    MixedBits mixed;
    logic[3-1:0] _align2;
    logic[5-1:0] mid;
    TinyBits tiny;
    logic[5-1:0] _align1;
    logic[3-1:0] head;
} OuterBits;


endpackage
