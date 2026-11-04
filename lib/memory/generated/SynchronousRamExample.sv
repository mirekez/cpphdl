`default_nettype none

import Predef_pkg::*;


module SynchronousRamExample (
    input wire clk
,   input wire reset
,   input wire write_in
,   input wire read_in
,   input wire[31:0] write_address_in
,   input wire[31:0] read_address_in
,   input wire[3-1:0][32-1:0] write_data_in
,   output wire[3-1:0][32-1:0] data_out
,   output wire[31:0] narrow_out
);

    typedef logic[3-1:0][32-1:0] Data;

    // regs and combs

    // members
    wire narrow__read_in;
    wire narrow__write_in;
    wire[31:0] narrow__read_address_in;
    wire[31:0] narrow__write_address_in;
    wire[unsigned'(64'(1))-1:0][32-1:0] narrow__data_in;
    wire[unsigned'(64'(1))-1:0][32-1:0] narrow__data_out;
    cpphdl_examples_SynchronousRam #(
        1
,       64
    ) narrow (
        .clk(clk)
,       .reset(reset)
,       .read_in(narrow__read_in)
,       .write_in(narrow__write_in)
,       .read_address_in(narrow__read_address_in)
,       .write_address_in(narrow__write_address_in)
,       .data_in(narrow__data_in)
,       .data_out(narrow__data_out)
    );
    wire wide__read_in;
    wire wide__write_in;
    wire[31:0] wide__read_address_in;
    wire[31:0] wide__write_address_in;
    wire[unsigned'(64'(3))-1:0][32-1:0] wide__data_in;
    wire[unsigned'(64'(3))-1:0][32-1:0] wide__data_out;
    cpphdl_examples_SynchronousRam #(
        3
,       64
    ) wide (
        .clk(clk)
,       .reset(reset)
,       .read_in(wide__read_in)
,       .write_in(wide__write_in)
,       .read_address_in(wide__read_address_in)
,       .write_address_in(wide__write_address_in)
,       .data_in(wide__data_in)
,       .data_out(wide__data_out)
    );

    // tmp variables


    generate  // _assign
        assign narrow__write_in = ((write_in) != '0);
        assign wide__write_in = ((write_in) != '0);
        assign narrow__read_in = ((read_in) != '0);
        assign wide__read_in = ((read_in) != '0);
        assign narrow__write_address_in = write_address_in;
        assign wide__write_address_in = write_address_in;
        assign narrow__read_address_in = read_address_in;
        assign wide__read_address_in = read_address_in;
        assign narrow__data_in = write_data_in[64'h0];
        assign wide__data_in = write_data_in;
        assign data_out = wide__data_out;
        assign narrow_out = unsigned'(32'(narrow__data_out[64'h0]));
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end


endmodule
