`default_nettype none

import Predef_pkg::*;
import HelperInstantiationType_pkg::*;
import TemplateInstantiationHelperHelperInstantiationType_pkg::*;


module TemplateHelperInstantiationLeafHelperInstantiationType (
    input wire clk
,   input wire reset
,   input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);


    // regs and combs
    TemplateInstantiationHelperHelperInstantiationType helper;
    logic[8-1:0] value_comb;

    // members

    // tmp variables


    function logic[8-1:0] TemplateInstantiationHelperHelperInstantiationType___method (
        input TemplateInstantiationHelperHelperInstantiationType _this
,       input logic[8-1:0] value
    );
        return unsigned'(8'(value)) + HelperInstantiationType_pkg::BIAS;
    endfunction

    always_comb begin : value_comb_func  // value_comb_func
        value_comb = TemplateInstantiationHelperHelperInstantiationType___method(helper, value_in);
    end

    task _work (input logic unused);
    begin: _work
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = value_comb;


endmodule
