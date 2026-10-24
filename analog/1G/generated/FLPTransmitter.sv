`default_nettype none

import Predef_pkg::*;


module FLPTransmitter (
    input wire clk
,   input wire reset
,   input wire[16-1:0] page_in
,   input wire enable_in
,   input wire restart_in
,   output wire pulse_out
,   output wire burst_begin_out
,   output wire burst_done_out
);


    // regs and combs
    reg[21-1:0] phase;
    reg[18-1:0] next_pulse;
    reg[6-1:0] half_slot;
    reg[4-1:0] width;
    reg[16-1:0] page;
    reg pulse;
    reg _begin;
    reg done;

    // members

    // tmp variables
    logic[21-1:0] phase_tmp;
    logic[18-1:0] next_pulse_tmp;
    logic[6-1:0] half_slot_tmp;
    logic[4-1:0] width_tmp;
    logic[16-1:0] page_tmp;
    logic pulse_tmp;
    logic _begin_tmp;
    logic done_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        _begin_tmp = (('h0) != '0);
        done_tmp = phase == 64'h3D09D;
        pulse_tmp = width != 64'h0;
        if (width != 64'h0) begin
            width_tmp = width - 64'h1;
        end
        phase_tmp = unsigned'(32'((phase == 64'h1E847F) ? (32'h0) : (unsigned'(32'(phase)) + 32'h1)));
        if (phase == 64'h0) begin
            page_tmp = page_in;
            _begin_tmp = (('h1) != '0);
            pulse_tmp = (('h1) != '0);
            width_tmp = 64'hC;
            next_pulse_tmp = 64'h1E84;
            half_slot_tmp = 64'h1;
        end
        else begin
            if ((half_slot < 64'h21) && (phase == next_pulse)) begin
                if ((((half_slot & 64'h1)) == 64'h0) || (((((page >>> ((unsigned'(32'(half_slot)) >>> 32'sh1)))) & 64'h1)) != 64'h0)) begin
                    pulse_tmp = (('h1) != '0);
                    width_tmp = 64'hC;
                end
                next_pulse_tmp = 32'(unsigned'(32'(next_pulse)) + unsigned'(32'(((((((half_slot & 64'h1))) != '0)) ? ('h1E85) : ('h1E84)))));
                half_slot_tmp = half_slot + 64'h1;
            end
        end
        if ((reset || !enable_in) || restart_in) begin
            phase_tmp = 64'h0;
            next_pulse_tmp = 64'h0;
            half_slot_tmp = 64'h0;
            width_tmp = 64'h0;
            page_tmp = 64'h0;
            pulse_tmp = (('h0) != '0);
            _begin_tmp = (('h0) != '0);
            done_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        phase_tmp = phase;
        next_pulse_tmp = next_pulse;
        half_slot_tmp = half_slot;
        width_tmp = width;
        page_tmp = page;
        pulse_tmp = pulse;
        _begin_tmp = _begin;
        done_tmp = done;

            _work(reset);

        phase <= phase_tmp;
        next_pulse <= next_pulse_tmp;
        half_slot <= half_slot_tmp;
        width <= width_tmp;
        page <= page_tmp;
        pulse <= pulse_tmp;
        _begin <= _begin_tmp;
        done <= done_tmp;
    end

    assign pulse_out = pulse;

    assign burst_begin_out = _begin;

    assign burst_done_out = done;


endmodule
