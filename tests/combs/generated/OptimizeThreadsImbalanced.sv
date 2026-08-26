`default_nettype none

import Predef_pkg::*;


module OptimizeThreadsImbalanced (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[32-1:0] _input;
    logic[32-1:0] heavy_0_cache;
    logic signed[63:0] heavy_0_clock;
    logic[32-1:0] heavy_1_cache;
    logic signed[63:0] heavy_1_clock;
    logic[32-1:0] heavy_2_cache;
    logic signed[63:0] heavy_2_clock;
    logic[32-1:0] heavy_3_cache;
    logic signed[63:0] heavy_3_clock;
    logic[32-1:0] heavy_4_cache;
    logic signed[63:0] heavy_4_clock;
    logic[32-1:0] heavy_5_cache;
    logic signed[63:0] heavy_5_clock;
    logic[32-1:0] heavy_6_cache;
    logic signed[63:0] heavy_6_clock;
    logic[32-1:0] heavy_7_cache;
    logic signed[63:0] heavy_7_clock;
    logic[32-1:0] heavy_8_cache;
    logic signed[63:0] heavy_8_clock;
    logic[32-1:0] heavy_9_cache;
    logic signed[63:0] heavy_9_clock;
    logic[32-1:0] light_a_cache;
    logic signed[63:0] light_a_clock;
    logic[32-1:0] light_b_cache;
    logic signed[63:0] light_b_clock;
    logic[32-1:0] light_c_cache;
    logic signed[63:0] light_c_clock;
    reg[32-1:0] result_heavy;
    reg[32-1:0] result_a;
    reg[32-1:0] result_b;
    reg[32-1:0] result_c;

    // members

    // tmp variables


    function logic[32-1:0] heavy_0 ();
        if (heavy_0_clock == $time) begin
            return heavy_0_cache;
        end
        heavy_0_clock=$time;
        heavy_0_cache = _input + 'h1;
        return heavy_0_cache;
    endfunction

    function logic[32-1:0] heavy_1 ();
        if (heavy_1_clock == $time) begin
            return heavy_1_cache;
        end
        heavy_1_clock=$time;
        heavy_1_cache = heavy_0() ^ 'h10203;
        return heavy_1_cache;
    endfunction

    function logic[32-1:0] heavy_2 ();
        if (heavy_2_clock == $time) begin
            return heavy_2_cache;
        end
        heavy_2_clock=$time;
        heavy_2_cache = heavy_1() + 'h3;
        return heavy_2_cache;
    endfunction

    function logic[32-1:0] heavy_3 ();
        if (heavy_3_clock == $time) begin
            return heavy_3_cache;
        end
        heavy_3_clock=$time;
        heavy_3_cache = (heavy_2() << 'h3) | (heavy_2() >> 'h1D);
        return heavy_3_cache;
    endfunction

    function logic[32-1:0] heavy_4 ();
        if (heavy_4_clock == $time) begin
            return heavy_4_cache;
        end
        heavy_4_clock=$time;
        heavy_4_cache = heavy_3() ^ 'hA5A55A5A;
        return heavy_4_cache;
    endfunction

    function logic[32-1:0] heavy_5 ();
        if (heavy_5_clock == $time) begin
            return heavy_5_cache;
        end
        heavy_5_clock=$time;
        heavy_5_cache = heavy_4() + 'h5;
        return heavy_5_cache;
    endfunction

    function logic[32-1:0] heavy_6 ();
        if (heavy_6_clock == $time) begin
            return heavy_6_cache;
        end
        heavy_6_clock=$time;
        heavy_6_cache = (heavy_5() << 'h7) | (heavy_5() >> 'h19);
        return heavy_6_cache;
    endfunction

    function logic[32-1:0] heavy_7 ();
        if (heavy_7_clock == $time) begin
            return heavy_7_cache;
        end
        heavy_7_clock=$time;
        heavy_7_cache = heavy_6() ^ 'h31415926;
        return heavy_7_cache;
    endfunction

    function logic[32-1:0] heavy_8 ();
        if (heavy_8_clock == $time) begin
            return heavy_8_cache;
        end
        heavy_8_clock=$time;
        heavy_8_cache = heavy_7() + 'h7;
        return heavy_8_cache;
    endfunction

    function logic[32-1:0] heavy_9 ();
        if (heavy_9_clock == $time) begin
            return heavy_9_cache;
        end
        heavy_9_clock=$time;
        heavy_9_cache = heavy_8() ^ (heavy_8() >> 'hB);
        return heavy_9_cache;
    endfunction

    function logic[32-1:0] light_a ();
        if (light_a_clock == $time) begin
            return light_a_cache;
        end
        light_a_clock=$time;
        light_a_cache = _input + 'hB;
        return light_a_cache;
    endfunction

    function logic[32-1:0] light_b ();
        if (light_b_clock == $time) begin
            return light_b_cache;
        end
        light_b_clock=$time;
        light_b_cache = _input ^ 'hD;
        return light_b_cache;
    endfunction

    function logic[32-1:0] light_c ();
        if (light_c_clock == $time) begin
            return light_c_cache;
        end
        light_c_clock=$time;
        light_c_cache = _input << 'h1;
        return light_c_cache;
    endfunction

    generate  // _assign
    endgenerate

    task _work (input logic unused);
    begin: _work
        result_heavy <= heavy_9();
        result_a <= light_a();
        result_b <= light_b();
        result_c <= light_c();
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule
