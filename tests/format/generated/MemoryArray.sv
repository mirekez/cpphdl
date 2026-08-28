`default_nettype none

import Predef_pkg::*;


module MemoryArray (
    input wire clk
,   input wire reset
);


    // regs and combs
    (* ram_style = "block" *)
    reg[4-1:0][8-1:0] banks[3][32];
    (* ram_style = "distributed" *)
    reg[64-1:0] words[32];

    // members

    // tmp variables


    task _work (input logic reset);
    begin: _work
        logic[63:0] bank;
        logic[63:0] address;
        bank = 'h0;
        address = 'h0;
        if (!reset) begin
            banks[bank][address] <= 'h12345678;
        end
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end


endmodule
