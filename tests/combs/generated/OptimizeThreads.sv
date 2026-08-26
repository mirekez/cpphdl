`default_nettype none

import Predef_pkg::*;


module OptimizeThreads (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[32-1:0] _input;
    logic[32-1:0] branch_a_cache;
    logic signed[63:0] branch_a_clock;
    logic[32-1:0] branch_b_cache;
    logic signed[63:0] branch_b_clock;
    logic[32-1:0] branch_c_cache;
    logic signed[63:0] branch_c_clock;
    logic[32-1:0] branch_d_cache;
    logic signed[63:0] branch_d_clock;
    reg[32-1:0] result_a;
    reg[32-1:0] result_b;
    reg[32-1:0] result_c;
    reg[32-1:0] result_d;

    // members

    // tmp variables


    function logic[32-1:0] branch_a ();
        if (branch_a_clock == $time) begin
            return branch_a_cache;
        end
        branch_a_clock=$time;
        branch_a_cache = _input + 'h10203;
        return branch_a_cache;
    endfunction

    function logic[32-1:0] branch_b ();
        if (branch_b_clock == $time) begin
            return branch_b_cache;
        end
        branch_b_clock=$time;
        branch_b_cache = _input ^ 'hA5A55A5A;
        return branch_b_cache;
    endfunction

    function logic[32-1:0] branch_c ();
        if (branch_c_clock == $time) begin
            return branch_c_cache;
        end
        branch_c_clock=$time;
        branch_c_cache = (_input << 'h4) + _input;
        return branch_c_cache;
    endfunction

    function logic[32-1:0] branch_d ();
        if (branch_d_clock == $time) begin
            return branch_d_cache;
        end
        branch_d_clock=$time;
        branch_d_cache = (_input << 'h7) | (_input >> 'h19);
        return branch_d_cache;
    endfunction

    generate  // _assign
    endgenerate

    task _work (input logic unused);
    begin: _work
        result_a <= branch_a();
        result_b <= branch_b();
        result_c <= branch_c();
        result_d <= branch_d();
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule
