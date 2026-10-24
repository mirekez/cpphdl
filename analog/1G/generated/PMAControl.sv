`default_nettype none

import Predef_pkg::*;


module PMAControl (
    input wire clk
,   input wire reset
,   input wire enable_in
,   input wire restart_in
,   input wire master_in
,   input wire scrambler_ok_in
,   input wire local_receiver_ok_in
,   input wire remote_receiver_ok_in
,   input wire transmit_active_in
,   input wire tick_ms_in
,   output wire[2-1:0] mode_out
,   output wire link_out
,   output wire timeout_out
,   output wire master_out
,   output wire[3-1:0] state_out
);


    // regs and combs
    reg[3-1:0] state;
    reg[2-1:0] mode;
    reg[7-1:0] dwell;
    reg[10-1:0] waiting;
    reg master;
    reg link;
    reg timeout;

    // members

    // tmp variables
    logic[3-1:0] state_tmp;
    logic[2-1:0] mode_tmp;
    logic[7-1:0] dwell_tmp;
    logic[10-1:0] waiting_tmp;
    logic master_tmp;
    logic link_tmp;
    logic timeout_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] next;
        logic expired;
        logic ready;
        next=state;
        expired=dwell>=64'h7C;
        ready=local_receiver_ok_in && remote_receiver_ok_in;
        if (state == 64'h0) begin
            if (enable_in) begin
                next=32'h1;
                master_tmp = master_in;
                waiting_tmp = 64'h0;
            end
        end
        else begin
            if (state == 64'h1) begin
                if (((master) != '0) || scrambler_ok_in) begin
                    next=32'h2;
                end
            end
            else begin
                if (state == 64'h2) begin
                    if (expired && local_receiver_ok_in) begin
                        next=(remote_receiver_ok_in) ? ('h4) : ('h3);
                    end
                end
                else begin
                    if (state == 64'h3) begin
                        if (expired) begin
                            if (!local_receiver_ok_in) begin
                                next=32'h1;
                            end
                            else begin
                                if (remote_receiver_ok_in) begin
                                    next=32'h4;
                                end
                            end
                        end
                    end
                    else begin
                        if (state == 64'h4) begin
                            if (expired) begin
                                if (!local_receiver_ok_in && !transmit_active_in) begin
                                    next=32'h1;
                                end
                                else begin
                                    if (local_receiver_ok_in && !remote_receiver_ok_in) begin
                                        next=32'h3;
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
        if ((state == 64'h1) || (state == 64'h2)) begin
            if (tick_ms_in) begin
                if (waiting>=unsigned'(64'(signed'(32'(((((master) != '0)) ? ('h2ED) : ('h15D)))))) && (((next == 32'h1) || (next == 32'h2)))) begin
                    next=32'h5;
                end
                else begin
                    waiting_tmp = waiting + 64'h1;
                end
            end
        end
        else begin
            waiting_tmp = 64'h0;
        end
        dwell_tmp = (expired) ? (dwell) : (dwell + 64'h1);
        if (next != unsigned'(32'(state))) begin
            dwell_tmp = 64'h0;
        end
        if ((next == 32'h1) && (state != 64'h1)) begin
            waiting_tmp = 64'h0;
        end
        state_tmp = next;
        mode_tmp = signed'(32'(((next == 32'h2) || (next == 32'h3)) ? ('h1) : ((next == 32'h4) ? ('h2) : ('h0))));
        link_tmp = (next == 32'h4) && ready;
        timeout_tmp = next == 32'h5;
        if ((reset || restart_in) || !enable_in) begin
            state_tmp = 64'h0;
            mode_tmp = 64'h0;
            dwell_tmp = 64'h0;
            waiting_tmp = 64'h0;
            master_tmp = (('h0) != '0);
            link_tmp = (('h0) != '0);
            timeout_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        state_tmp = state;
        mode_tmp = mode;
        dwell_tmp = dwell;
        waiting_tmp = waiting;
        master_tmp = master;
        link_tmp = link;
        timeout_tmp = timeout;

            _work(reset);

        state <= state_tmp;
        mode <= mode_tmp;
        dwell <= dwell_tmp;
        waiting <= waiting_tmp;
        master <= master_tmp;
        link <= link_tmp;
        timeout <= timeout_tmp;
    end

    assign mode_out = mode;

    assign link_out = link;

    assign timeout_out = timeout;

    assign master_out = master;

    assign state_out = state;


endmodule
