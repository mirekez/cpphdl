`default_nettype none

import Predef_pkg::*;


module MaskedSynchronousRamExample (
    input wire clk
,   input wire reset
,   input wire read_in
,   input wire write_in
,   input wire[31:0] read_address_in
,   input wire[31:0] write_address_in
,   input wire[3-1:0] write_mask_in
,   input wire[3-1:0][32-1:0] data_in
,   output wire[3-1:0][32-1:0] data_out
);

    typedef logic[3-1:0][32-1:0] Data;

    // regs and combs

    // members
    wire ram__read_in;
    wire ram__write_in;
    wire[31:0] ram__read_address_in;
    wire[31:0] ram__write_address_in;
    wire[unsigned'(64'(3))-1:0] ram__write_mask_in;
    wire[unsigned'(64'(3))-1:0][32-1:0] ram__data_in;
    wire[unsigned'(64'(3))-1:0][32-1:0] ram__data_out;
    cpphdl_examples_MaskedSynchronousRam #(
        3
,       17
    ) ram (
        .clk(clk)
,       .reset(reset)
,       .read_in(ram__read_in)
,       .write_in(ram__write_in)
,       .read_address_in(ram__read_address_in)
,       .write_address_in(ram__write_address_in)
,       .write_mask_in(ram__write_mask_in)
,       .data_in(ram__data_in)
,       .data_out(ram__data_out)
    );

    // tmp variables


    generate  // _assign
        assign ram__read_in = read_in;
        assign ram__write_in = write_in;
        assign ram__read_address_in = read_address_in;
        assign ram__write_address_in = write_address_in;
        assign ram__write_mask_in = write_mask_in;
        assign ram__data_in = data_in;
        assign data_out = ram__data_out;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end


endmodule
