package HftCollection_pkg;
import HftQuote_pkg::*;

typedef struct packed {
    logic[7-1:0] _align0;
    logic udp_checksum_present;
    logic[7-1:0] _align4;
    logic sbe_ok;
    logic[7-1:0] _align3;
    logic udp_ok;
    logic[7-1:0] _align2;
    logic ipv4_ok;
    logic[7-1:0] _align1;
    logic ethernet_ok;
    logic[31:0] udp_sum;
    logic[31:0] ip_sum;
    logic[31:0] crc;
    HftQuote quote;
} HftCollection;


endpackage
