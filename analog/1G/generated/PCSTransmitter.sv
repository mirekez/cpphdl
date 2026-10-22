`default_nettype none

import Predef_pkg::*;


module PCSTransmitter (
    input wire clk
,   input wire reset
,   input wire[8-1:0] data_in
,   input wire enable_in
,   input wire error_in
,   input wire master_in
,   input wire link_in
,   input wire local_receiver_ok_in
,   input wire[2-1:0] mode_in
,   input wire seed_load_in
,   input wire[33-1:0] seed_in
,   output wire[12-1:0] symbols_out
,   output wire fault_out
);


    // regs and combs

    // members
    wire[8-1:0] bits__data_in;
    wire bits__enable_in;
    wire bits__error_in;
    wire bits__master_in;
    wire bits__link_in;
    wire bits__local_receiver_ok_in;
    wire[2-1:0] bits__mode_in;
    wire bits__seed_load_in;
    wire[33-1:0] bits__seed_in;
    wire[6-1:0] bits__point_out;
    wire[3-1:0] bits__subset_out;
    wire[4-1:0] bits__condition_out;
    wire[4-1:0] bits__negate_out;
    wire bits__fault_out;
    wire[4-1:0] bits__sx_out;
    wire[4-1:0] bits__sy_out;
    wire[4-1:0] bits__sg_out;
    wire[9-1:0] bits__encoded_out;
    wire[3-1:0] bits__trellis_out;
    PCSTransmitBits      bits (
        .clk(clk)
,       .reset(reset)
,       .data_in(bits__data_in)
,       .enable_in(bits__enable_in)
,       .error_in(bits__error_in)
,       .master_in(bits__master_in)
,       .link_in(bits__link_in)
,       .local_receiver_ok_in(bits__local_receiver_ok_in)
,       .mode_in(bits__mode_in)
,       .seed_load_in(bits__seed_load_in)
,       .seed_in(bits__seed_in)
,       .point_out(bits__point_out)
,       .subset_out(bits__subset_out)
,       .condition_out(bits__condition_out)
,       .negate_out(bits__negate_out)
,       .fault_out(bits__fault_out)
,       .sx_out(bits__sx_out)
,       .sy_out(bits__sy_out)
,       .sg_out(bits__sg_out)
,       .encoded_out(bits__encoded_out)
,       .trellis_out(bits__trellis_out)
    );
    wire[6-1:0] mapper__point_in;
    wire[3-1:0] mapper__subset_in;
    wire[4-1:0] mapper__condition_in;
    wire[4-1:0] mapper__negate_in;
    wire[12-1:0] mapper__symbols_out;
    wire mapper__invalid_out;
    PCSSymbolMapper      mapper (
        .clk(clk)
,       .reset(reset)
,       .point_in(mapper__point_in)
,       .subset_in(mapper__subset_in)
,       .condition_in(mapper__condition_in)
,       .negate_in(mapper__negate_in)
,       .symbols_out(mapper__symbols_out)
,       .invalid_out(mapper__invalid_out)
    );

    // tmp variables


    generate  // _assign
        assign bits__data_in = data_in;
        assign bits__enable_in = ((enable_in) != '0);
        assign bits__error_in = ((error_in) != '0);
        assign bits__master_in = ((master_in) != '0);
        assign bits__link_in = ((link_in) != '0);
        assign bits__local_receiver_ok_in = ((local_receiver_ok_in) != '0);
        assign bits__mode_in = mode_in;
        assign bits__seed_load_in = ((seed_load_in) != '0);
        assign bits__seed_in = seed_in;
        assign mapper__point_in = bits__point_out;
        assign mapper__subset_in = bits__subset_out;
        assign mapper__condition_in = bits__condition_out;
        assign mapper__negate_in = bits__negate_out;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end

    assign symbols_out = mapper__symbols_out;

    assign fault_out = ((bits__fault_out || mapper__invalid_out) != '0);


endmodule
