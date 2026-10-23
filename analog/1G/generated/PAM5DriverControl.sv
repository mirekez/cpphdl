`default_nettype none

import Predef_pkg::*;


module PAM5DriverControl (
    input wire clk
,   input wire reset
,   input wire[3-1:0] symbol_in
,   input wire enable_in
,   output wire[4-1:0] main_p_out
,   output wire[4-1:0] main_n_out
,   output wire[4-1:0] history_p_out
,   output wire[4-1:0] history_n_out
,   output wire valid_out
,   output wire fault_out
);


    // regs and combs
    reg[3-1:0] previous;
    reg[4-1:0] main_p;
    reg[4-1:0] main_n;
    reg[4-1:0] history_p;
    reg[4-1:0] history_n;
    reg valid;
    reg fault;

    // members

    // tmp variables
    logic[3-1:0] previous_tmp;
    logic[4-1:0] main_p_tmp;
    logic[4-1:0] main_n_tmp;
    logic[4-1:0] history_p_tmp;
    logic[4-1:0] history_n_tmp;
    logic valid_tmp;
    logic fault_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] code;
        logic[31:0] current_mask;
        logic[31:0] previous_mask;
        code=symbol_in;
        current_mask=(('h1 <<< code)) - 32'h1;
        previous_mask=(('h1 <<< unsigned'(32'(previous)))) - 32'h1;
        valid_tmp = (('h0) != '0);
        if ((enable_in && !((fault) != '0)) && code<=32'h4) begin
            main_p_tmp = current_mask;
            main_n_tmp = 32'(current_mask ^ 32'hF);
            history_p_tmp = previous_mask;
            history_n_tmp = 32'(previous_mask ^ 32'hF);
            previous_tmp = code;
            valid_tmp = (('h1) != '0);
        end
        else begin
            main_p_tmp = 64'h0;
            main_n_tmp = 64'h0;
            history_p_tmp = 64'h0;
            history_n_tmp = 64'h0;
            previous_tmp = 64'h2;
            if (enable_in && (code > 32'h4)) begin
                fault_tmp = (('h1) != '0);
            end
        end
        if (reset) begin
            previous_tmp = 64'h2;
            main_p_tmp = 64'h0;
            main_n_tmp = 64'h0;
            history_p_tmp = 64'h0;
            history_n_tmp = 64'h0;
            valid_tmp = (('h0) != '0);
            fault_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        previous_tmp = previous;
        main_p_tmp = main_p;
        main_n_tmp = main_n;
        history_p_tmp = history_p;
        history_n_tmp = history_n;
        valid_tmp = valid;
        fault_tmp = fault;

            _work(reset);

        previous <= previous_tmp;
        main_p <= main_p_tmp;
        main_n <= main_n_tmp;
        history_p <= history_p_tmp;
        history_n <= history_n_tmp;
        valid <= valid_tmp;
        fault <= fault_tmp;
    end

    assign main_p_out = main_p;

    assign main_n_out = main_n;

    assign history_p_out = history_p;

    assign history_n_out = history_n;

    assign valid_out = valid;

    assign fault_out = fault;


endmodule
