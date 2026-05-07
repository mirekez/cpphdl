`default_nettype none

import Predef_pkg::*;


module LogicBitsIndexing (
    input wire clk
,   input wire reset
,   input wire[3-1:0] word_in
,   input wire[32-1:0] seed_in
,   output wire[16-1:0] direct_out
,   output wire[16-1:0] next_out
,   output wire[8-1:0] byte_out
,   output wire[128-1:0] edited_out
);


    // regs and combs
    logic[128-1:0] source_comb;
    logic[128-1:0] edited_comb;
    logic[16-1:0] direct_comb;
    logic[16-1:0] next_comb;
    logic[8-1:0] byte_comb;

    // members
    genvar gi, gj, gk;

    // tmp variables


    task build_source ();
    begin: build_source
        logic[31:0] seed; seed = seed_in;
        source_comb = 'h0;
        source_comb['h0 +:16] = unsigned'(64'(seed)) + 'h1000;
        source_comb['h10 +:16] = unsigned'(64'(seed)) + 'h2111;
        source_comb['h20 +:16] = unsigned'(64'(seed)) + 'h3222;
        source_comb['h30 +:16] = unsigned'(64'(seed)) + 'h4333;
        source_comb['h40 +:16] = unsigned'(64'(seed)) + 'h5444;
        source_comb['h50 +:16] = unsigned'(64'(seed)) + 'h6555;
        source_comb['h60 +:16] = unsigned'(64'(seed)) + 'h7666;
        source_comb['h70 +:16] = unsigned'(64'(seed)) + 'h8777;
    end
    endtask

    always @(*) begin  // direct_comb_func
        logic[31:0] word; word = unsigned'(32'(word_in));
        build_source();
        direct_comb = source_comb[word*'h10 +:16];
    end

    always @(*) begin  // next_comb_func
        logic[31:0] word; word = unsigned'(32'(word_in));
        build_source();
        next_comb = source_comb[((word + 'h1))*'h10 +:16];
    end

    always @(*) begin  // byte_comb_func
        logic[31:0] word; word = unsigned'(32'(word_in));
        build_source();
        byte_comb = source_comb[word*'h8 +:8];
    end

    always @(*) begin  // edited_comb_func
        logic[31:0] word; word = unsigned'(32'(word_in));
        build_source();
        edited_comb = source_comb;
        edited_comb[word*'h10 +:16] = unsigned'(64'(seed_in)) ^ 'h55AA;
        edited_comb[((word + 'h1))*'h10 +:16] = unsigned'(64'(seed_in)) ^ 'hAA55;
        edited_comb[word*'h8 +:8] = unsigned'(64'(seed_in)) ^ 'h5A;
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

    assign direct_out = direct_comb;

    assign next_out = next_comb;

    assign byte_out = byte_comb;

    assign edited_out = edited_comb;


endmodule
