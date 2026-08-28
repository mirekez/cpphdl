`default_nettype none

import Predef_pkg::*;


module JsonTestHarness (
    input wire clk
,   input wire reset
,   input wire trigger_in
,   output wire observed_out
);


    // regs and combs
    reg request_reg;
    logic response_value;

    // members
    wire dut__request_in;
    wire dut__response_out;
    JsonAliasLeaf      dut (
        .clk(clk)
,       .reset(reset)
,       .request_in(dut__request_in)
,       .response_out(dut__response_out)
    );

    // tmp variables
    logic request_reg_tmp;


    task _work (input logic unused);
    begin: _work
        response_value=dut__response_out;
    end
    endtask

    generate  // _assign
        assign dut__request_in = request_reg;
    endgenerate

    always @(posedge clk) begin
        request_reg_tmp = request_reg;

        _work(reset);

        request_reg <= request_reg_tmp;
    end


endmodule
