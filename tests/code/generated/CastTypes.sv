`default_nettype none

import Predef_pkg::*;


module CastTypes #(
    parameter WIDTH_PARAM
 )
 (
    input wire clk
,   input wire reset
,   input wire[32-1:0] value_in
,   output wire[32-1:0] cstyle_out
,   output wire[32-1:0] functional_out
,   output wire[32-1:0] direct_cstyle_out
,   output wire[32-1:0] direct_functional_out
,   output wire[32-1:0] constructor_template_out
);
    parameter  WIDTH = WIDTH_PARAM;
    parameter  CAST_BITS = (WIDTH<='h1) ? ('h1) : ($clog2(WIDTH));


    // regs and combs
    logic[32-1:0] cstyle_comb;
    logic[32-1:0] functional_comb;
    logic[32-1:0] direct_cstyle_comb;
    logic[32-1:0] direct_functional_comb;
    logic[32-1:0] constructor_template_comb;
    logic[CAST_BITS-1:0] cstyle_narrow;
    logic[CAST_BITS-1:0] functional_narrow;

    // members

    // tmp variables


    function logic[32-1:0] scaled_value ();
        return unsigned'(32'((((unsigned'(32'(value_in)) & (((WIDTH*WIDTH) - 'h1))))/WIDTH)));
    endfunction

    always_comb begin : cstyle_comb_func  // cstyle_comb_func
        cstyle_narrow = unsigned'(CAST_BITS'(unsigned'(CAST_BITS'(scaled_value()))));
        cstyle_comb = unsigned'(32'(cstyle_narrow));
    end

    always_comb begin : functional_comb_func  // functional_comb_func
        functional_narrow = unsigned'(CAST_BITS'(unsigned'(CAST_BITS'(scaled_value()))));
        functional_comb = unsigned'(32'(functional_narrow));
    end

    always_comb begin : direct_cstyle_comb_func  // direct_cstyle_comb_func
        logic[31:0] v; v = scaled_value();
        direct_cstyle_comb = unsigned'(32'((unsigned'($clog2(WIDTH_PARAM)'(unsigned'($clog2(WIDTH_PARAM)'(v)))))));
    end

    always_comb begin : direct_functional_comb_func  // direct_functional_comb_func
        logic[31:0] v; v = scaled_value();
        direct_functional_comb = unsigned'(32'((unsigned'($clog2(WIDTH_PARAM)'(unsigned'($clog2(WIDTH_PARAM)'(v)))))));
    end

    always_comb begin : constructor_template_comb_func  // constructor_template_comb_func
        logic[3-1:0] narrow;
        logic[8-1:0] widened;
        narrow = unsigned'($clog2(WIDTH_PARAM)'(unsigned'($clog2(WIDTH_PARAM)'(scaled_value()))));
        widened = unsigned'(8'(unsigned'(3'(narrow))));
        constructor_template_comb = unsigned'(32'(widened));
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign cstyle_out = cstyle_comb;

    assign functional_out = functional_comb;

    assign direct_cstyle_out = direct_cstyle_comb;

    assign direct_functional_out = direct_functional_comb;

    assign constructor_template_out = constructor_template_comb;


endmodule
