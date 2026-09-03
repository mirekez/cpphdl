`default_nettype none

import Predef_pkg::*;


module ModuleArrayCallRoot (
    input wire clk
,   input wire reset
);
    localparam  constant_index = 'h1;


    // regs and combs
    logic[1-1:0] _input;
    logic[1-1:0] select;
    logic[1-1:0] _output;
    logic[1-1:0] constant_output;
    logic[1-1:0] port_constant_output;
    logic[1-1:0] work_value;
    logic[1-1:0] output_comb;

    // members
    genvar __i;
    generate
    for (__i=0; __i < 2; __i = __i + 1) begin
        ModuleArrayCallLeaf          children (
            .clk(clk)
        ,           .reset(reset)
        );
    end
    endgenerate
    ModuleArrayCallPortSink      port_sink (
        .clk(clk)
,       .reset(reset)
    );

    // tmp variables


    always_comb begin : output_comb_func  // output_comb_func
        output_comb = 'h0;
        output_comb = children[unsigned'(64'(select))]._output;
    end

    generate  // _assign
        assign children['h0]._input = _input;
        assign children['h1]._input = _input;
        assign port_sink__sources = children;
    endgenerate

    task _work (input logic unused);
    begin: _work
        work_value = output_comb;
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule
