`default_nettype none

import Predef_pkg::*;


module MasterSlaveResolver (
    input wire clk
,   input wire reset
,   input wire[11-1:0] local_up1_in
,   input wire[11-1:0] remote_up1_in
,   input wire[11-1:0] local_seed_in
,   input wire[11-1:0] remote_seed_in
,   input wire advance_in
,   output wire resolved_out
,   output wire master_out
,   output wire fault_out
,   output wire collision_out
);


    // regs and combs
    reg[4-1:0] status;

    // members

    // tmp variables
    logic[4-1:0] status_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic local_manual;
        logic remote_manual;
        logic local_master;
        logic remote_master;
        local_manual=((local_up1_in & 64'h1)) != 64'h0;
        remote_manual=((remote_up1_in & 64'h1)) != 64'h0;
        local_master=((local_up1_in & 64'h2)) != 64'h0;
        remote_master=((remote_up1_in & 64'h2)) != 64'h0;
        if (advance_in) begin
            if ((local_manual && remote_manual) && (signed'(32'(local_master)) == signed'(32'(remote_master)))) begin
                status_tmp = 64'h4;
            end
            else begin
                if (local_manual) begin
                    status_tmp = signed'(32'((local_master) ? ('h3) : ('h1)));
                end
                else begin
                    if (remote_manual) begin
                        status_tmp = signed'(32'((remote_master) ? ('h1) : ('h3)));
                    end
                    else begin
                        if (((((local_up1_in ^ remote_up1_in)) & 64'h4)) != 64'h0) begin
                            status_tmp = signed'(32'((((local_up1_in & 64'h4)) != 64'h0) ? ('h3) : ('h1)));
                        end
                        else begin
                            if (local_seed_in == remote_seed_in) begin
                                status_tmp = 64'h8;
                            end
                            else begin
                                status_tmp = signed'(32'((local_seed_in > remote_seed_in) ? ('h3) : ('h1)));
                            end
                        end
                    end
                end
            end
        end
        if (reset) begin
            status_tmp = 64'h0;
        end
    end
    endtask

    always @(posedge clk) begin
        status_tmp = status;

            _work(reset);

        status <= status_tmp;
    end

    assign resolved_out = ((((status & 64'h1)) != 64'h0) != '0);

    assign master_out = ((((status & 64'h2)) != 64'h0) != '0);

    assign fault_out = ((((status & 64'h4)) != 64'h0) != '0);

    assign collision_out = ((((status & 64'h8)) != 64'h0) != '0);


endmodule
