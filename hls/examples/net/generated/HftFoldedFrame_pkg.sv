package HftFoldedFrame_pkg;
import HftQuote_pkg::*;

typedef struct packed {
    logic[7-1:0] _align0;
    logic crc_error;
    logic[7-1:0] _align3;
    logic size_error;
    logic[7-1:0] _align2;
    logic udp_checksum_present;
    logic[7-1:0] _align1;
    logic valid;
    logic[31:0] udp_sum;
    logic[31:0] ip_sum;
    HftQuote quote;
} HftFoldedFrame;


endpackage
