`default_nettype none

import Predef_pkg::*;


module File #(
    parameter MEM_WIDTH
,   parameter MEM_DEPTH
 )
 (
    input wire clk
,   input wire reset
,   input logic[31:0] write_addr_in
,   input wire write_in
,   input logic[31:0] write_data_in
,   input logic[31:0] read_addr0_in
,   input logic[31:0] read_addr1_in
,   input wire read_in
,   output logic[31:0] read_data0_out
,   output logic[31:0] read_data1_out
,   input wire debugen_in
);

    logic[31:0] data0_out_comb;
    logic[31:0] data1_out_comb;
    reg[MEM_WIDTH/32-1:0][31:0] buffer[MEM_DEPTH];
    logic[31:0] i;


    task _work (input logic reset);
    begin: _work
        if (write_in) begin
            buffer[write_addr_in] = write_data_in;
        end
    end
    endtask

    always @(posedge clk) begin
        _work(reset);
    end

endmodule
