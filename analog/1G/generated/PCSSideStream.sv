`default_nettype none

import Predef_pkg::*;


module PCSSideStream (
    input wire clk
,   input wire reset
,   input wire master_in
,   input wire advance_in
,   input wire load_in
,   input wire[33-1:0] seed_in
,   output wire[33-1:0] state_out
,   output wire invalid_out
);


    // regs and combs
    reg[33-1:0] state_reg;
    reg invalid_reg;

    // members

    // tmp variables
    logic[33-1:0] state_reg_tmp;
    logic invalid_reg_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[33-1:0] feedback;
        feedback = ((((state_reg >>> 32'sh20)) ^ ((state_reg >>> signed'(32'(((master_in) ? ('hC) : ('h13)))))))) & 64'h1;
        if (advance_in && !((invalid_reg) != '0)) begin
            state_reg_tmp = ((state_reg <<< 'h1)) | feedback;
        end
        if (load_in) begin
            state_reg_tmp = seed_in;
            invalid_reg_tmp = seed_in == 64'h0;
        end
        if (reset) begin
            state_reg_tmp = 64'h1;
            invalid_reg_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        state_reg_tmp = state_reg;
        invalid_reg_tmp = invalid_reg;

            _work(reset);

        state_reg <= state_reg_tmp;
        invalid_reg <= invalid_reg_tmp;
    end

    assign state_out = state_reg;

    assign invalid_out = invalid_reg;


endmodule
