`default_nettype none

import Predef_pkg::*;


module CombFunc (
    input wire clk
,   input wire reset
,   input wire early_in
,   input wire[32-1:0] seed_in
,   output wire[32-1:0] plain_out
,   output wire[32-1:0] array_out
,   output wire[32-1:0] early_out
);


    // regs and combs
    logic[32-1:0] plain_comb;
    logic[32-1:0] array_comb;
    logic[32-1:0] early_comb;
    logic[32-1:0] c_line[4];
    logic[32-1:0] c_grid[2][3];
    logic[4-1:0][32-1:0] cpp_line;
    logic[2-1:0][3-1:0][32-1:0] cpp_grid;

    // members

    // tmp variables


    always_comb begin : plain_comb_func  // plain_comb_func
        plain_comb = seed_in + unsigned'(32'(unsigned'(32'('h1))));
        plain_comb = plain_comb + unsigned'(32'(unsigned'(32'('h2))));
    end

    always_comb begin : array_comb_func  // array_comb_func
        logic[63:0] i;
        logic[63:0] y;
        logic[63:0] x;
        array_comb = 'h0;
        for (i='h0;i < 'h4;i=i+1) begin
            c_line[i] = seed_in + unsigned'(32'(unsigned'(32'((i + 'hA)))));
            cpp_line[i] = c_line[i] + unsigned'(32'(unsigned'(32'((i + 'h14)))));
            array_comb = array_comb + cpp_line[i];
        end
        for (y='h0;y < 'h2;y=y+1) begin
            for (x='h0;x < 'h3;x=x+1) begin
                c_grid[y][x] = array_comb + unsigned'(32'(unsigned'(32'(((y*'hA) + x)))));
                cpp_grid[y][x] = c_grid[y][x] + unsigned'(32'(unsigned'(32'((('h64 + (y*'hA)) + x)))));
                array_comb = array_comb + cpp_grid[y][x];
            end
        end
    end

    always_comb begin : early_comb_func  // early_comb_func
        early_comb = seed_in + unsigned'(32'(unsigned'(32'('h3E8))));
        if (early_in) begin
            early_comb = seed_in + unsigned'(32'(unsigned'(32'('h7D0))));
            disable early_comb_func;
        end
        early_comb = seed_in + unsigned'(32'(unsigned'(32'('hBB8))));
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

    assign plain_out = plain_comb;

    assign array_out = array_comb;

    assign early_out = early_comb;


endmodule
