`default_nettype none

import Predef_pkg::*;
import StructuralNttpInterfaceStructuralNttpConfig{{{}_{1}}_3}_request_pkg::*;


module StructuralNttpArrayProjectedLeafStructuralNttpInterfaceStructuralNttpConfig{{{}_{1}}_3}_request (
    input wire clk
,   input wire reset
);


    // regs and combs
    StructuralNttpInterfaceStructuralNttpConfig{{{}_{1}}_3}_request[2-1:0] input;
    logic[3-1:0] _output;
    logic[3-1:0] output_comb;

    // members

    // tmp variables


    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule
