`default_nettype none

import Predef_pkg::*;


module PMATransmitLane (
    input wire clk
,   input wire reset
,   input wire enable_in
,   input wire[3-1:0] symbol_in
,   input wire[3-1:0] test_mode_in
,   input wire[11-1:0] test_seed_in
,   input wire restart_in
,   output wire[4-1:0] main_p_out
,   output wire[4-1:0] main_n_out
,   output wire[4-1:0] history_p_out
,   output wire[4-1:0] history_n_out
,   output wire valid_out
,   output wire fault_out
,   output wire slave_clock_out
);


    // regs and combs
    reg[3-1:0] previous_mode;

    // members
    wire pattern__enable_in;
    wire[3-1:0] pattern__mode_in;
    wire[11-1:0] pattern__seed_in;
    wire pattern__restart_in;
    wire[3-1:0] pattern__symbol_out;
    wire pattern__valid_out;
    wire pattern__slave_clock_out;
    wire pattern__fault_out;
    PMATestPattern      pattern (
        .clk(clk)
,       .reset(reset)
,       .enable_in(pattern__enable_in)
,       .mode_in(pattern__mode_in)
,       .seed_in(pattern__seed_in)
,       .restart_in(pattern__restart_in)
,       .symbol_out(pattern__symbol_out)
,       .valid_out(pattern__valid_out)
,       .slave_clock_out(pattern__slave_clock_out)
,       .fault_out(pattern__fault_out)
    );
    wire[3-1:0] driver__symbol_in;
    wire driver__enable_in;
    wire[4-1:0] driver__main_p_out;
    wire[4-1:0] driver__main_n_out;
    wire[4-1:0] driver__history_p_out;
    wire[4-1:0] driver__history_n_out;
    wire driver__valid_out;
    wire driver__fault_out;
    PAM5DriverControl      driver (
        .clk(clk)
,       .reset(((reset || !enable_in) || restart_in) || (test_mode_in != previous_mode))
,       .symbol_in(driver__symbol_in)
,       .enable_in(driver__enable_in)
,       .main_p_out(driver__main_p_out)
,       .main_n_out(driver__main_n_out)
,       .history_p_out(driver__history_p_out)
,       .history_n_out(driver__history_n_out)
,       .valid_out(driver__valid_out)
,       .fault_out(driver__fault_out)
    );

    // tmp variables
    logic[3-1:0] previous_mode_tmp;


    generate  // _assign
        assign pattern__enable_in = ((enable_in) != '0);
        assign pattern__mode_in = test_mode_in;
        assign pattern__seed_in = test_seed_in;
        assign pattern__restart_in = ((restart_in) != '0);
        assign driver__symbol_in = (test_mode_in == 64'h0) ? (((symbol_in<=64'h4) ? (32'h4 - unsigned'(32'(symbol_in))) : (unsigned'(32'(symbol_in))))) : (32'h4 - unsigned'(32'(pattern__symbol_out)));
        assign driver__enable_in = ((enable_in && (((test_mode_in == 64'h0) || pattern__valid_out))) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
        previous_mode_tmp = test_mode_in;
        if (reset || !enable_in) begin
            previous_mode_tmp = 64'h0;
        end
    end
    endtask

    always @(posedge clk) begin
        previous_mode_tmp = previous_mode;

            _work(reset);

        previous_mode <= previous_mode_tmp;
    end

    assign main_p_out = driver__main_p_out;

    assign main_n_out = driver__main_n_out;

    assign history_p_out = driver__history_p_out;

    assign history_n_out = driver__history_n_out;

    assign valid_out = ((driver__valid_out) != '0);

    assign fault_out = ((driver__fault_out || (((test_mode_in != 64'h0) && pattern__fault_out))) != '0);

    assign slave_clock_out = (((test_mode_in == 64'h3) && pattern__slave_clock_out) != '0);


endmodule
