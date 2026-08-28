`default_nettype none

import Predef_pkg::*;


module TemplateInheritedBaseArray #(
    parameter COUNT = 'h2
 )
 (
    input wire clk
,   input wire reset
,   output wire[8-1:0] value_out
);


    // regs and combs
    logic[8-1:0] TemplateInheritedArrayBase___value_comb;

    // members
    genvar __i;
    wire[8-1:0] TemplateInheritedArrayBase___leaf__value_in[COUNT];
    wire[8-1:0] TemplateInheritedArrayBase___leaf__value_out[COUNT];
    generate
    for (__i=0; __i < COUNT; __i = __i + 1) begin
        TemplateInheritedArrayLeaf          TemplateInheritedArrayBase___leaf (
            .clk(clk)
        ,           .reset(reset)
        ,           .value_in(TemplateInheritedArrayBase___leaf__value_in[__i])
        ,           .value_out(TemplateInheritedArrayBase___leaf__value_out[__i])
        );
    end
    endgenerate

    // tmp variables


    generate  // TemplateInheritedArrayBase____assign
        genvar gi;
        for (gi='h0;gi < COUNT;gi=gi+1) begin
            assign TemplateInheritedArrayBase___leaf__value_in[gi] = gi;
        end
    endgenerate

    generate  // _assign
    endgenerate

    task TemplateInheritedArrayBase____work (input logic reset);
    begin: TemplateInheritedArrayBase____work
        logic[63:0] i;
        for (i='h0;i < COUNT;i=i+1) begin
        end
    end
    endtask

    task _work (input logic reset);
    begin: _work
        TemplateInheritedArrayBase____work(reset);
    end
    endtask

    always_comb begin : TemplateInheritedArrayBase___value_comb_func  // TemplateInheritedArrayBase___value_comb_func
        TemplateInheritedArrayBase___value_comb=TemplateInheritedArrayBase___leaf__value_out['h0];
    end

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = TemplateInheritedArrayBase___value_comb;


endmodule
