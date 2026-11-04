`default_nettype none

import Predef_pkg::*;
import cpphdl_examples_math_MaximumMethods_pkg::*;


module MaximumExample (
    input wire clk
,   input wire reset
,   input wire[63:0] a_in
,   input wire[63:0] b_in
,   output wire[63:0] result_out
);


    // regs and combs
    cpphdl_examples_math_MaximumMethods methods;
    logic[63:0] result_comb;

    // members

    // tmp variables


    function logic[63:0] cpphdl_examples_math_MaximumMethods___maximum_merge (
        input cpphdl_examples_math_MaximumMethods _this
,       input logic[63:0] a
,       input logic[63:0] b
    );
        logic[31:0] ai;
        logic[31:0] bi;
        logic[31:0] av;
        logic[31:0] bv;
        logic[31:0] ak;
        logic[31:0] bk;
        logic an;
        logic bn;
        logic equal;
        logic take_b;
        ai=a >>> 32'sh20;
        bi=b >>> 32'sh20;
        av=a;
        bv=b;
        an=((av & 'h7FFFFFFF)) > 'h7F800000;
        bn=((bv & 'h7FFFFFFF)) > 'h7F800000;
        ak=((((av & 'h80000000)) != '0)) ? (~av) : ((av ^ 'h80000000));
        bk=((((bv & 'h80000000)) != '0)) ? (~bv) : ((bv ^ 'h80000000));
        equal=(av == bv) || (((((av & 'h7FFFFFFF)) == 32'h0) && (((bv & 'h7FFFFFFF)) == 32'h0)));
        take_b=(bi != 'hFFFFFFFF) && (((ai == 'hFFFFFFFF) || ((!(((ai == 32'h0) && an)) && (((((bi == 32'h0) && bn)) || ((!bn && ((an || ((equal) ? ((bi < ai)) : ((ak < bk)))))))))))));
        return (take_b) ? (b) : (a);
    endfunction

    always_comb begin : result_comb_func  // result_comb_func
        result_comb=cpphdl_examples_math_MaximumMethods___maximum_merge(methods, a_in, b_in);
    end

    generate  // _assign
        assign result_out = result_comb;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end


endmodule
