`default_nettype none

import Predef_pkg::*;


module OptimizeMath (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[32-1:0] _input;
    logic sign;
    logic[32-1:0] reverse_cache;
    logic[20-1:0] replicate_cache;
    logic[32-1:0] sign_extend_cache;
    logic[8-1:0] partial_cache;
    reg[32-1:0] reverse_result;
    reg[20-1:0] replicate_result;
    reg[32-1:0] sign_extend_result;
    reg[8-1:0] partial_result;

    // members

    // tmp variables


    function logic[32-1:0] reverse ();
        reverse_cache['h0] = ((unsigned'(64'((_input))) >>> 'h1F)) & 64'h1;
        reverse_cache['h1] = ((unsigned'(64'((_input))) >>> 'h1E)) & 64'h1;
        reverse_cache['h2] = ((unsigned'(64'((_input))) >>> 'h1D)) & 64'h1;
        reverse_cache['h3] = ((unsigned'(64'((_input))) >>> 'h1C)) & 64'h1;
        reverse_cache['h4] = ((unsigned'(64'((_input))) >>> 'h1B)) & 64'h1;
        reverse_cache['h5] = ((unsigned'(64'((_input))) >>> 'h1A)) & 64'h1;
        reverse_cache['h6] = ((unsigned'(64'((_input))) >>> 'h19)) & 64'h1;
        reverse_cache['h7] = ((unsigned'(64'((_input))) >>> 'h18)) & 64'h1;
        reverse_cache['h8] = ((unsigned'(64'((_input))) >>> 'h17)) & 64'h1;
        reverse_cache['h9] = ((unsigned'(64'((_input))) >>> 'h16)) & 64'h1;
        reverse_cache['hA] = ((unsigned'(64'((_input))) >>> 'h15)) & 64'h1;
        reverse_cache['hB] = ((unsigned'(64'((_input))) >>> 'h14)) & 64'h1;
        reverse_cache['hC] = ((unsigned'(64'((_input))) >>> 'h13)) & 64'h1;
        reverse_cache['hD] = ((unsigned'(64'((_input))) >>> 'h12)) & 64'h1;
        reverse_cache['hE] = ((unsigned'(64'((_input))) >>> 'h11)) & 64'h1;
        reverse_cache['hF] = ((unsigned'(64'((_input))) >>> 'h10)) & 64'h1;
        reverse_cache['h10] = ((unsigned'(64'((_input))) >>> 'hF)) & 64'h1;
        reverse_cache['h11] = ((unsigned'(64'((_input))) >>> 'hE)) & 64'h1;
        reverse_cache['h12] = ((unsigned'(64'((_input))) >>> 'hD)) & 64'h1;
        reverse_cache['h13] = ((unsigned'(64'((_input))) >>> 'hC)) & 64'h1;
        reverse_cache['h14] = ((unsigned'(64'((_input))) >>> 'hB)) & 64'h1;
        reverse_cache['h15] = ((unsigned'(64'((_input))) >>> 'hA)) & 64'h1;
        reverse_cache['h16] = ((unsigned'(64'((_input))) >>> 'h9)) & 64'h1;
        reverse_cache['h17] = ((unsigned'(64'((_input))) >>> 'h8)) & 64'h1;
        reverse_cache['h18] = ((unsigned'(64'((_input))) >>> 'h7)) & 64'h1;
        reverse_cache['h19] = ((unsigned'(64'((_input))) >>> 'h6)) & 64'h1;
        reverse_cache['h1A] = ((unsigned'(64'((_input))) >>> 'h5)) & 64'h1;
        reverse_cache['h1B] = ((unsigned'(64'((_input))) >>> 'h4)) & 64'h1;
        reverse_cache['h1C] = ((unsigned'(64'((_input))) >>> 'h3)) & 64'h1;
        reverse_cache['h1D] = ((unsigned'(64'((_input))) >>> 'h2)) & 64'h1;
        reverse_cache['h1E] = ((unsigned'(64'((_input))) >>> 'h1)) & 64'h1;
        reverse_cache['h1F] = ((unsigned'(64'((_input))) >>> 'h0)) & 64'h1;
        return reverse_cache;
    endfunction

    function logic[20-1:0] replicate ();
        replicate_cache['h13] = sign;
        replicate_cache['h12] = sign;
        replicate_cache['h11] = sign;
        replicate_cache['h10] = sign;
        replicate_cache['hF] = sign;
        replicate_cache['hE] = sign;
        replicate_cache['hD] = sign;
        replicate_cache['hC] = sign;
        replicate_cache['hB] = sign;
        replicate_cache['hA] = sign;
        replicate_cache['h9] = sign;
        replicate_cache['h8] = sign;
        replicate_cache['h7] = sign;
        replicate_cache['h6] = sign;
        replicate_cache['h5] = sign;
        replicate_cache['h4] = sign;
        replicate_cache['h3] = sign;
        replicate_cache['h2] = sign;
        replicate_cache['h1] = sign;
        replicate_cache['h0] = sign;
        return replicate_cache;
    endfunction

    function logic[32-1:0] sign_extend ();
        sign_extend_cache['h1F] = sign;
        sign_extend_cache['h1E] = sign;
        sign_extend_cache['h1D] = sign;
        sign_extend_cache['h1C] = sign;
        sign_extend_cache['h1B] = sign;
        sign_extend_cache['h1A] = sign;
        sign_extend_cache['h19] = sign;
        sign_extend_cache['h18] = sign;
        sign_extend_cache['h17] = sign;
        sign_extend_cache['h16] = sign;
        sign_extend_cache['h15] = sign;
        sign_extend_cache['h14] = sign;
        sign_extend_cache['h13] = sign;
        sign_extend_cache['h12] = sign;
        sign_extend_cache['h11] = sign;
        sign_extend_cache['h10] = sign;
        sign_extend_cache['hF] = sign;
        sign_extend_cache['hE] = sign;
        sign_extend_cache['hD] = sign;
        sign_extend_cache['hC] = sign;
        sign_extend_cache['hB] = sign;
        sign_extend_cache['hA] = sign;
        sign_extend_cache['h9] = sign;
        sign_extend_cache['h8] = sign;
        sign_extend_cache['h0 +:8] = _input;
        return sign_extend_cache;
    endfunction

    function logic[8-1:0] partial ();
        partial_cache['h3] = sign;
        partial_cache['h2] = sign;
        partial_cache['h1] = sign;
        partial_cache['h0] = sign;
        return partial_cache;
    endfunction

    generate  // _assign
    endgenerate

    task _work (input logic unused);
    begin: _work
        reverse_result <= reverse();
        replicate_result <= replicate();
        sign_extend_result <= sign_extend();
        partial_result <= partial();
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule
