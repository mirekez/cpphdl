`default_nettype none

import Predef_pkg::*;


module ReferenceMillisecond (
    input wire clk
,   input wire reset
,   output wire tick_out
);


    // regs and combs
    reg[17-1:0] count;
    reg tick;

    // members

    // tmp variables
    logic[17-1:0] count_tmp;
    logic tick_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        tick_tmp = count == 64'h1E847;
        count_tmp = unsigned'(32'((count == 64'h1E847) ? (32'h0) : (unsigned'(32'(count)) + 32'h1)));
        if (reset) begin
            count_tmp = 64'h0;
            tick_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        count_tmp = count;
        tick_tmp = tick;

            _work(reset);

        count <= count_tmp;
        tick <= tick_tmp;
    end

    assign tick_out = tick;


endmodule
