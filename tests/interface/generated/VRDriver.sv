`default_nettype none

import Predef_pkg::*;


module VRDriver #(
    parameter DATAWIDTH = 64
 )
 (
    input wire clk
,   input wire reset
,   output wire source_out__valid_out
,   input wire source_out__ready_in
,   output wire[DATAWIDTH-1:0] source_out__data_out
,   output wire done_out
);


    // regs and combs
    reg[32-1:0] state_reg;
    reg[16-1:0] sent_reg;
    reg valid_reg;
    reg[DATAWIDTH-1:0] data_reg;
    logic done_comb;

    // members
    genvar gi, gj, gk;

    // tmp variables
    logic[32-1:0] state_reg_tmp;
    logic[16-1:0] sent_reg_tmp;
    logic valid_reg_tmp;
    logic[DATAWIDTH-1:0] data_reg_tmp;


    always @(*) begin  // done_comb_func
        done_comb=sent_reg>='h100;
    end

    generate  // _assign
        assign source_out__valid_out = valid_reg;
        assign source_out__data_out = data_reg;
    endgenerate

    task _work (input logic reset);
    begin: _work
        if (reset) begin
            state_reg_tmp = unsigned'(32'('h13579BDF));
            sent_reg_tmp = '0;
            valid_reg_tmp = '0;
            data_reg_tmp = '0;
            data_reg_tmp = 'h0;
            data_reg_tmp['h0 +:32] = unsigned'(32'('h13579BDF));
            data_reg_tmp['h20 +:16] = unsigned'(16'('h0));
            data_reg_tmp['h30 +:16] = unsigned'(16'('h9BDF));
            disable _work;
        end
        state_reg_tmp = state_reg;
        sent_reg_tmp = sent_reg;
        valid_reg_tmp = sent_reg < 'h100;
        data_reg_tmp = data_reg;
        if (valid_reg && source_out__ready_in) begin
            sent_reg_tmp = sent_reg + unsigned'(16'('h1));
            state_reg_tmp = (state_reg*unsigned'(32'('h19660D))) + unsigned'(32'('h3C6EF35F));
            valid_reg_tmp = sent_reg_tmp < 'h100;
        end
        if (!valid_reg || source_out__ready_in) begin
            data_reg_tmp = 'h0;
            data_reg_tmp['h0 +:32] = state_reg_tmp;
            data_reg_tmp['h20 +:16] = sent_reg_tmp;
            data_reg_tmp['h30 +:16] = (((unsigned'(64'(state_reg_tmp))) & 'hFFFF)) ^ sent_reg_tmp;
        end
    end
    endtask

    always @(posedge clk) begin
        state_reg_tmp = state_reg;
        sent_reg_tmp = sent_reg;
        valid_reg_tmp = valid_reg;
        data_reg_tmp = data_reg;

        _work(reset);

        state_reg <= state_reg_tmp;
        sent_reg <= sent_reg_tmp;
        valid_reg <= valid_reg_tmp;
        data_reg <= data_reg_tmp;
    end

    assign done_out = done_comb;


endmodule
