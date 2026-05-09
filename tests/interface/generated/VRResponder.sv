`default_nettype none

import Predef_pkg::*;


module VRResponder #(
    parameter DATAWIDTH = 64
 )
 (
    input wire clk
,   input wire reset
,   input wire sink_in__valid_in
,   output wire sink_in__ready_out
,   input wire[DATAWIDTH-1:0] sink_in__data_in
,   output wire done_out
,   output wire error_out
);


    // regs and combs
    reg[32-1:0] state_reg;
    reg[16-1:0] received_reg;
    reg[8-1:0] ready_lfsr_reg;
    reg ready_reg;
    reg error_reg;
    logic[DATAWIDTH-1:0] expected_data;
    logic done_comb;
    logic error_comb;

    // members
    genvar gi, gj, gk;

    // tmp variables
    logic[32-1:0] state_reg_tmp;
    logic[16-1:0] received_reg_tmp;
    logic[8-1:0] ready_lfsr_reg_tmp;
    logic ready_reg_tmp;
    logic error_reg_tmp;


    always_comb begin : done_comb_func  // done_comb_func
        done_comb=received_reg>='h100;
        disable done_comb_func;
    end

    always_comb begin : error_comb_func  // error_comb_func
        error_comb=error_reg;
        disable error_comb_func;
    end

    generate  // _assign
        assign sink_in__ready_out = ready_reg;
    endgenerate

    task _work (input logic reset);
    begin: _work
        if (reset) begin
            state_reg_tmp = unsigned'(32'('h13579BDF));
            received_reg_tmp = '0;
            ready_lfsr_reg_tmp = unsigned'(8'('h5A));
            ready_reg_tmp = '0;
            error_reg_tmp = '0;
            disable _work;
        end
        state_reg_tmp = state_reg;
        received_reg_tmp = received_reg;
        ready_lfsr_reg_tmp = ready_lfsr_reg;
        ready_reg_tmp = ready_reg;
        error_reg_tmp = error_reg;
        ready_lfsr_reg_tmp = ((ready_lfsr_reg <<< 'h1)) ^ unsigned'(8'(((((((unsigned'(64'(ready_lfsr_reg)) >>> 'h7)) ^ ((unsigned'(64'(ready_lfsr_reg)) >>> 'h5))) ^ 'h1)) & 'h1)));
        ready_reg_tmp = ((unsigned'(64'(ready_lfsr_reg)) & 'h7)) != 'h0;
        if (sink_in__valid_in && sink_in__ready_out) begin
            expected_data = 'h0;
            expected_data['h0 +:32] = state_reg;
            expected_data['h20 +:16] = received_reg;
            expected_data['h30 +:16] = (((unsigned'(64'(state_reg))) & 'hFFFF)) ^ received_reg;
            if (sink_in__data_in != expected_data) begin
                error_reg_tmp = 'h1;
            end
            state_reg_tmp = (state_reg*unsigned'(32'('h19660D))) + unsigned'(32'('h3C6EF35F));
            received_reg_tmp = received_reg + unsigned'(16'('h1));
        end
    end
    endtask

    always @(posedge clk) begin
        state_reg_tmp = state_reg;
        received_reg_tmp = received_reg;
        ready_lfsr_reg_tmp = ready_lfsr_reg;
        ready_reg_tmp = ready_reg;
        error_reg_tmp = error_reg;

        _work(reset);

        state_reg <= state_reg_tmp;
        received_reg <= received_reg_tmp;
        ready_lfsr_reg <= ready_lfsr_reg_tmp;
        ready_reg <= ready_reg_tmp;
        error_reg <= error_reg_tmp;
    end

    assign done_out = done_comb;

    assign error_out = error_comb;


endmodule
