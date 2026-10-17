`default_nettype none

import Predef_pkg::*;


module RepeaterCore (
    input wire ref_clock
,   input wire reset
,   input wire[10-1:0] rx_a_in
,   input wire[10-1:0] rx_b_in
,   input wire link_a_in
,   input wire link_b_in
,   input wire enable_in
,   output wire[10-1:0] tx_a_out
,   output wire[10-1:0] tx_b_out
,   output wire active_out
);


    // regs and combs
    reg[10-1:0] tx_a_reg;
    reg[10-1:0] tx_b_reg;
    reg active_reg;

    // members

    // tmp variables
    logic[10-1:0] tx_a_reg_tmp;
    logic[10-1:0] tx_b_reg_tmp;
    logic active_reg_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic forward;
        forward = (enable_in && link_a_in) && link_b_in;
        active_reg_tmp = forward;
        tx_a_reg_tmp = 64'h0;
        tx_b_reg_tmp = 64'h0;
        if (forward) begin
            if (((rx_b_in & 64'h100)) != 64'h0) begin
                tx_a_reg_tmp = rx_b_in;
            end
            if (((rx_a_in & 64'h100)) != 64'h0) begin
                tx_b_reg_tmp = rx_a_in;
            end
        end
        if (reset) begin
            tx_a_reg_tmp = '0;
            tx_b_reg_tmp = '0;
            active_reg_tmp = '0;
        end
    end
    endtask

    always @(posedge ref_clock) begin
        tx_a_reg_tmp = tx_a_reg;
        tx_b_reg_tmp = tx_b_reg;
        active_reg_tmp = active_reg;

            _work(reset);

        tx_a_reg <= tx_a_reg_tmp;
        tx_b_reg <= tx_b_reg_tmp;
        active_reg <= active_reg_tmp;
    end

    assign tx_a_out = tx_a_reg;

    assign tx_b_out = tx_b_reg;

    assign active_out = active_reg;


endmodule
