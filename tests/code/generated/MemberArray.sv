`default_nettype none

import Predef_pkg::*;


module MemberArray (
    input wire clk
,   input wire reset
,   input wire[16-1:0] seed_in
,   output wire[16-1:0] result_out
);


    // regs and combs
    logic[16-1:0] result_comb;

    // members
    genvar gi, gj, gk;
      wire[16-1:0] line__base_in[3];
      wire[16-1:0] line__add_in[3];
      wire[16-1:0] line__value_out[3];
    generate
    for (gi=0; gi < 3; gi = gi + 1) begin
        MemberArrayLeaf          line (
            .clk(clk)
        ,           .reset(reset)
        ,           .base_in(line__base_in[gi])
        ,           .add_in(line__add_in[gi])
        ,           .value_out(line__value_out[gi])
        );
    end
    endgenerate
      wire[16-1:0] grid__base_in[2][3];
      wire[16-1:0] grid__add_in[2][3];
      wire[16-1:0] grid__value_out[2][3];
    generate
    for (gi=0; gi < 2; gi = gi + 1) begin
        for (gj=0; gj < 3; gj = gj + 1) begin
            MemberArrayLeaf              grid (
                .clk(clk)
            ,           .reset(reset)
            ,           .base_in(grid__base_in[gi][gj])
            ,           .add_in(grid__add_in[gi][gj])
            ,           .value_out(grid__value_out[gi][gj])
            );
        end
    end
    endgenerate
      wire[16-1:0] cube__base_in[2][2][2];
      wire[16-1:0] cube__add_in[2][2][2];
      wire[16-1:0] cube__value_out[2][2][2];
    generate
    for (gi=0; gi < 2; gi = gi + 1) begin
        for (gj=0; gj < 2; gj = gj + 1) begin
            for (gk=0; gk < 2; gk = gk + 1) begin
                MemberArrayLeaf                  cube (
                    .clk(clk)
                ,           .reset(reset)
                ,           .base_in(cube__base_in[gi][gj][gk])
                ,           .add_in(cube__add_in[gi][gj][gk])
                ,           .value_out(cube__value_out[gi][gj][gk])
                );
            end
        end
    end
    endgenerate

    // tmp variables


    always_comb begin : result_comb_func  // result_comb_func
        result_comb = (line__value_out['h2] + grid__value_out['h1]['h2]) + cube__value_out['h1]['h1]['h1];
        disable result_comb_func;
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    generate  // _assign
        assign line__base_in['h0] = seed_in;
        assign line__add_in['h0] = unsigned'(16'('h1));
        assign line__base_in['h1] = line__value_out['h0];
        assign line__add_in['h1] = unsigned'(16'('h2));
        assign line__base_in['h2] = line__value_out['h1];
        assign line__add_in['h2] = unsigned'(16'('h3));
        assign grid__base_in['h0]['h0] = line__value_out['h1];
        assign grid__add_in['h0]['h0] = unsigned'(16'('hA));
        assign grid__base_in['h0]['h1] = grid__value_out['h0]['h0];
        assign grid__add_in['h0]['h1] = unsigned'(16'('hB));
        assign grid__base_in['h0]['h2] = grid__value_out['h0]['h1];
        assign grid__add_in['h0]['h2] = unsigned'(16'('hC));
        assign grid__base_in['h1]['h0] = line__value_out['h2];
        assign grid__add_in['h1]['h0] = unsigned'(16'('h14));
        assign grid__base_in['h1]['h1] = grid__value_out['h1]['h0];
        assign grid__add_in['h1]['h1] = unsigned'(16'('h15));
        assign grid__base_in['h1]['h2] = grid__value_out['h1]['h1];
        assign grid__add_in['h1]['h2] = unsigned'(16'('h16));
        assign cube__base_in['h0]['h0]['h0] = grid__value_out['h0]['h1];
        assign cube__add_in['h0]['h0]['h0] = unsigned'(16'('h64));
        assign cube__base_in['h0]['h0]['h1] = cube__value_out['h0]['h0]['h0];
        assign cube__add_in['h0]['h0]['h1] = unsigned'(16'('h65));
        assign cube__base_in['h0]['h1]['h0] = grid__value_out['h0]['h2];
        assign cube__add_in['h0]['h1]['h0] = unsigned'(16'('h6E));
        assign cube__base_in['h0]['h1]['h1] = cube__value_out['h0]['h1]['h0];
        assign cube__add_in['h0]['h1]['h1] = unsigned'(16'('h6F));
        assign cube__base_in['h1]['h0]['h0] = grid__value_out['h1]['h1];
        assign cube__add_in['h1]['h0]['h0] = unsigned'(16'('hC8));
        assign cube__base_in['h1]['h0]['h1] = cube__value_out['h1]['h0]['h0];
        assign cube__add_in['h1]['h0]['h1] = unsigned'(16'('hC9));
        assign cube__base_in['h1]['h1]['h0] = grid__value_out['h1]['h2];
        assign cube__add_in['h1]['h1]['h0] = unsigned'(16'('hD2));
        assign cube__base_in['h1]['h1]['h1] = cube__value_out['h1]['h1]['h0];
        assign cube__add_in['h1]['h1]['h1] = unsigned'(16'('hD3));
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign result_out = result_comb;


endmodule
