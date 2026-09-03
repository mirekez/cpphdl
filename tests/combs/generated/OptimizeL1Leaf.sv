`default_nettype none

import Predef_pkg::*;


module OptimizeL1Leaf (
    input wire clk
,   input wire reset
,   input wire[16-1:0] value_in
,   input wire enable_in
);


    // regs and combs
    logic[16-1:0] equal_a_cache;
    logic signed[63:0] equal_a_clock;
    logic[16-1:0] equal_b_cache;
    logic signed[63:0] equal_b_clock;
    logic work_observed;
    logic[31:0] procedural_invocations;
    logic[16-1:0] procedural_cache;
    logic signed[63:0] procedural_clock;

    // members

    // tmp variables


    function logic[16-1:0] equal_a ();
        if (equal_a_clock == $time) begin
            return equal_a_cache;
        end
        equal_a_clock=$time;
        equal_a_cache = value_in + 'h3;
        return equal_a_cache;
    endfunction

    function logic[16-1:0] equal_b ();
        if (equal_b_clock == $time) begin
            return equal_b_cache;
        end
        equal_b_clock=$time;
        equal_b_cache = value_in + 'h3;
        return equal_b_cache;
    endfunction

    function logic[16-1:0] procedural ();
        procedural_invocations=procedural_invocations+1;
        if (procedural_clock == $time) begin
            return procedural_cache;
        end
        procedural_clock=$time;
        procedural_cache = equal_a();
        if (enable_in) begin
            procedural_cache = procedural_cache + equal_b();
        end
        return procedural_cache;
    endfunction

    function logic[16-1:0] _optimized_value ();
        return procedural();
    endfunction

    generate  // _assign
    endgenerate

    task _work (input logic unused);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule
