`default_nettype none

import Predef_pkg::*;


module RAM1PORT #(
    parameter WIDTH
,   parameter DEPTH
 )
 (
    input wire clk
,   input wire reset
,   input wire[$clog2(DEPTH)-1:0] addr_in
,   input wire[WIDTH-1:0] data_in
,   input wire wr_in
,   input wire rd_in
,   output wire[WIDTH-1:0] q_out
,   input wire signed[31:0] id_in
);


    // regs and combs
    reg[WIDTH-1:0] q_out_reg;
    reg[((WIDTH + 'h7))/'h8-1:0][8-1:0] buffer[DEPTH];

    // members
    genvar gi, gj, gk;

    // tmp variables
    logic[WIDTH-1:0] q_out_reg_tmp;


    task _work (input logic reset);
    begin: _work
        if (reset) begin
            q_out_reg_tmp = '0;
            disable _work;
        end
        if (wr_in) begin
            buffer[addr_in] <= data_in;
        end
        if (rd_in) begin
            q_out_reg_tmp = buffer[addr_in];
        end
    end
    endtask

    always @(posedge clk) begin
        q_out_reg_tmp = q_out_reg;

        _work(reset);

        q_out_reg <= q_out_reg_tmp;
    end

    assign q_out = q_out_reg;


endmodule
