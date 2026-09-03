`default_nettype none

import Predef_pkg::*;


module NonblockingMemory (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic write_enable;
    logic read_enable;
    logic[2-1:0] address;
    logic[8-1:0] write_data;
    logic[4-1:0][8-1:0] memory;
    reg[8-1:0] read_data;

    // members

    // tmp variables


    task _work (input logic unused);
    begin: _work
        if (write_enable) begin
            logic[63:0] index; index = address;
            memory[index] = write_data;
        end
        if (read_enable) begin
            logic[63:0] index; index = address;
            read_data <= memory[index];
        end
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule
