`default_nettype none

import Predef_pkg::*;


module AssignPorts (
    input wire clk
,   input wire reset
,   input wire[16-1:0] seed_in
,   output wire[16-1:0] result_out
);


    // regs and combs
    logic[16-1:0] scalar_source;
    logic[16-1:0] i_source[3];
    logic[16-1:0] j_source[3];
    logic[16-1:0] ij_source[2][3];
    logic[16-1:0] ijk_source[2][2][2];
    logic[16-1:0] cap_source[2][2][2];
    logic[16-1:0] result_comb;

    // members
    genvar __i, __j, __k;
      wire[16-1:0] expr_leaf__value_in;
      wire[16-1:0] expr_leaf__value_out;
    AssignPortsLeaf      expr_leaf (
        .clk(clk)
,       .reset(reset)
,       .value_in(expr_leaf__value_in)
,       .value_out(expr_leaf__value_out)
    );
      wire[16-1:0] var_leaf__value_in;
      wire[16-1:0] var_leaf__value_out;
    AssignPortsLeaf      var_leaf (
        .clk(clk)
,       .reset(reset)
,       .value_in(var_leaf__value_in)
,       .value_out(var_leaf__value_out)
    );
      wire[16-1:0] expr_i__value_in[3];
      wire[16-1:0] expr_i__value_out[3];
    generate
    for (__i=0; __i < 3; __i = __i + 1) begin
        AssignPortsLeaf          expr_i (
            .clk(clk)
        ,           .reset(reset)
        ,           .value_in(expr_i__value_in[__i])
        ,           .value_out(expr_i__value_out[__i])
        );
    end
    endgenerate
      wire[16-1:0] var_i__value_in[3];
      wire[16-1:0] var_i__value_out[3];
    generate
    for (__i=0; __i < 3; __i = __i + 1) begin
        AssignPortsLeaf          var_i (
            .clk(clk)
        ,           .reset(reset)
        ,           .value_in(var_i__value_in[__i])
        ,           .value_out(var_i__value_out[__i])
        );
    end
    endgenerate
      wire[16-1:0] expr_j__value_in[3];
      wire[16-1:0] expr_j__value_out[3];
    generate
    for (__i=0; __i < 3; __i = __i + 1) begin
        AssignPortsLeaf          expr_j (
            .clk(clk)
        ,           .reset(reset)
        ,           .value_in(expr_j__value_in[__i])
        ,           .value_out(expr_j__value_out[__i])
        );
    end
    endgenerate
      wire[16-1:0] var_j__value_in[3];
      wire[16-1:0] var_j__value_out[3];
    generate
    for (__i=0; __i < 3; __i = __i + 1) begin
        AssignPortsLeaf          var_j (
            .clk(clk)
        ,           .reset(reset)
        ,           .value_in(var_j__value_in[__i])
        ,           .value_out(var_j__value_out[__i])
        );
    end
    endgenerate
      wire[16-1:0] expr_ij__value_in[2][3];
      wire[16-1:0] expr_ij__value_out[2][3];
    generate
    for (__i=0; __i < 2; __i = __i + 1) begin
        for (__j=0; __j < 3; __j = __j + 1) begin
            AssignPortsLeaf              expr_ij (
                .clk(clk)
            ,           .reset(reset)
            ,           .value_in(expr_ij__value_in[__i][__j])
            ,           .value_out(expr_ij__value_out[__i][__j])
            );
        end
    end
    endgenerate
      wire[16-1:0] var_ij__value_in[2][3];
      wire[16-1:0] var_ij__value_out[2][3];
    generate
    for (__i=0; __i < 2; __i = __i + 1) begin
        for (__j=0; __j < 3; __j = __j + 1) begin
            AssignPortsLeaf              var_ij (
                .clk(clk)
            ,           .reset(reset)
            ,           .value_in(var_ij__value_in[__i][__j])
            ,           .value_out(var_ij__value_out[__i][__j])
            );
        end
    end
    endgenerate
      wire[16-1:0] expr_ijk__value_in[2][2][2];
      wire[16-1:0] expr_ijk__value_out[2][2][2];
    generate
    for (__i=0; __i < 2; __i = __i + 1) begin
        for (__j=0; __j < 2; __j = __j + 1) begin
            for (__k=0; __k < 2; __k = __k + 1) begin
                AssignPortsLeaf                  expr_ijk (
                    .clk(clk)
                ,           .reset(reset)
                ,           .value_in(expr_ijk__value_in[__i][__j][__k])
                ,           .value_out(expr_ijk__value_out[__i][__j][__k])
                );
            end
        end
    end
    endgenerate
      wire[16-1:0] var_ijk__value_in[2][2][2];
      wire[16-1:0] var_ijk__value_out[2][2][2];
    generate
    for (__i=0; __i < 2; __i = __i + 1) begin
        for (__j=0; __j < 2; __j = __j + 1) begin
            for (__k=0; __k < 2; __k = __k + 1) begin
                AssignPortsLeaf                  var_ijk (
                    .clk(clk)
                ,           .reset(reset)
                ,           .value_in(var_ijk__value_in[__i][__j][__k])
                ,           .value_out(var_ijk__value_out[__i][__j][__k])
                );
            end
        end
    end
    endgenerate
      wire[16-1:0] expr_cap__value_in[2][2][2];
      wire[16-1:0] expr_cap__value_out[2][2][2];
    generate
    for (__i=0; __i < 2; __i = __i + 1) begin
        for (__j=0; __j < 2; __j = __j + 1) begin
            for (__k=0; __k < 2; __k = __k + 1) begin
                AssignPortsLeaf                  expr_cap (
                    .clk(clk)
                ,           .reset(reset)
                ,           .value_in(expr_cap__value_in[__i][__j][__k])
                ,           .value_out(expr_cap__value_out[__i][__j][__k])
                );
            end
        end
    end
    endgenerate
      wire[16-1:0] var_cap__value_in[2][2][2];
      wire[16-1:0] var_cap__value_out[2][2][2];
    generate
    for (__i=0; __i < 2; __i = __i + 1) begin
        for (__j=0; __j < 2; __j = __j + 1) begin
            for (__k=0; __k < 2; __k = __k + 1) begin
                AssignPortsLeaf                  var_cap (
                    .clk(clk)
                ,           .reset(reset)
                ,           .value_in(var_cap__value_in[__i][__j][__k])
                ,           .value_out(var_cap__value_out[__i][__j][__k])
                );
            end
        end
    end
    endgenerate

    // tmp variables


    always_comb begin : result_comb_func  // result_comb_func
        scalar_source = seed_in + unsigned'(16'(unsigned'(16'('h2))));
        i_source['h0] = seed_in + unsigned'(16'(unsigned'(16'('h64))));
        i_source['h1] = seed_in + unsigned'(16'(unsigned'(16'('h65))));
        i_source['h2] = seed_in + unsigned'(16'(unsigned'(16'('h66))));
        j_source['h0] = seed_in + unsigned'(16'(unsigned'(16'('h8C))));
        j_source['h1] = seed_in + unsigned'(16'(unsigned'(16'('h8D))));
        j_source['h2] = seed_in + unsigned'(16'(unsigned'(16'('h8E))));
        ij_source['h0]['h0] = seed_in + unsigned'(16'(unsigned'(16'('hC8))));
        ij_source['h0]['h1] = seed_in + unsigned'(16'(unsigned'(16'('hC9))));
        ij_source['h0]['h2] = seed_in + unsigned'(16'(unsigned'(16'('hCA))));
        ij_source['h1]['h0] = seed_in + unsigned'(16'(unsigned'(16'('hD2))));
        ij_source['h1]['h1] = seed_in + unsigned'(16'(unsigned'(16'('hD3))));
        ij_source['h1]['h2] = seed_in + unsigned'(16'(unsigned'(16'('hD4))));
        ijk_source['h0]['h0]['h0] = seed_in + unsigned'(16'(unsigned'(16'('h12C))));
        ijk_source['h0]['h0]['h1] = seed_in + unsigned'(16'(unsigned'(16'('h12D))));
        ijk_source['h0]['h1]['h0] = seed_in + unsigned'(16'(unsigned'(16'('h136))));
        ijk_source['h0]['h1]['h1] = seed_in + unsigned'(16'(unsigned'(16'('h137))));
        ijk_source['h1]['h0]['h0] = seed_in + unsigned'(16'(unsigned'(16'('h190))));
        ijk_source['h1]['h0]['h1] = seed_in + unsigned'(16'(unsigned'(16'('h191))));
        ijk_source['h1]['h1]['h0] = seed_in + unsigned'(16'(unsigned'(16'('h19A))));
        ijk_source['h1]['h1]['h1] = seed_in + unsigned'(16'(unsigned'(16'('h19B))));
        cap_source['h0]['h0]['h0] = seed_in + unsigned'(16'(unsigned'(16'('h1F4))));
        cap_source['h0]['h0]['h1] = seed_in + unsigned'(16'(unsigned'(16'('h1F5))));
        cap_source['h0]['h1]['h0] = seed_in + unsigned'(16'(unsigned'(16'('h1FE))));
        cap_source['h0]['h1]['h1] = seed_in + unsigned'(16'(unsigned'(16'('h1FF))));
        cap_source['h1]['h0]['h0] = seed_in + unsigned'(16'(unsigned'(16'('h258))));
        cap_source['h1]['h0]['h1] = seed_in + unsigned'(16'(unsigned'(16'('h259))));
        cap_source['h1]['h1]['h0] = seed_in + unsigned'(16'(unsigned'(16'('h262))));
        cap_source['h1]['h1]['h1] = seed_in + unsigned'(16'(unsigned'(16'('h263))));
        result_comb = expr_leaf__value_out + var_leaf__value_out;
        result_comb += expr_i__value_out['h0] + var_i__value_out['h0];
        result_comb += expr_i__value_out['h1] + var_i__value_out['h1];
        result_comb += expr_i__value_out['h2] + var_i__value_out['h2];
        result_comb += expr_j__value_out['h0] + var_j__value_out['h0];
        result_comb += expr_j__value_out['h1] + var_j__value_out['h1];
        result_comb += expr_j__value_out['h2] + var_j__value_out['h2];
        result_comb += expr_ij__value_out['h0]['h0] + var_ij__value_out['h0]['h0];
        result_comb += expr_ij__value_out['h0]['h1] + var_ij__value_out['h0]['h1];
        result_comb += expr_ij__value_out['h0]['h2] + var_ij__value_out['h0]['h2];
        result_comb += expr_ij__value_out['h1]['h0] + var_ij__value_out['h1]['h0];
        result_comb += expr_ij__value_out['h1]['h1] + var_ij__value_out['h1]['h1];
        result_comb += expr_ij__value_out['h1]['h2] + var_ij__value_out['h1]['h2];
        result_comb += expr_ijk__value_out['h0]['h0]['h0] + var_ijk__value_out['h0]['h0]['h0];
        result_comb += expr_ijk__value_out['h0]['h0]['h1] + var_ijk__value_out['h0]['h0]['h1];
        result_comb += expr_ijk__value_out['h0]['h1]['h0] + var_ijk__value_out['h0]['h1]['h0];
        result_comb += expr_ijk__value_out['h0]['h1]['h1] + var_ijk__value_out['h0]['h1]['h1];
        result_comb += expr_ijk__value_out['h1]['h0]['h0] + var_ijk__value_out['h1]['h0]['h0];
        result_comb += expr_ijk__value_out['h1]['h0]['h1] + var_ijk__value_out['h1]['h0]['h1];
        result_comb += expr_ijk__value_out['h1]['h1]['h0] + var_ijk__value_out['h1]['h1]['h0];
        result_comb += expr_ijk__value_out['h1]['h1]['h1] + var_ijk__value_out['h1]['h1]['h1];
        result_comb += expr_cap__value_out['h0]['h0]['h0] + var_cap__value_out['h0]['h0]['h0];
        result_comb += expr_cap__value_out['h0]['h0]['h1] + var_cap__value_out['h0]['h0]['h1];
        result_comb += expr_cap__value_out['h0]['h1]['h0] + var_cap__value_out['h0]['h1]['h0];
        result_comb += expr_cap__value_out['h0]['h1]['h1] + var_cap__value_out['h0]['h1]['h1];
        result_comb += expr_cap__value_out['h1]['h0]['h0] + var_cap__value_out['h1]['h0]['h0];
        result_comb += expr_cap__value_out['h1]['h0]['h1] + var_cap__value_out['h1]['h0]['h1];
        result_comb += expr_cap__value_out['h1]['h1]['h0] + var_cap__value_out['h1]['h1]['h0];
        result_comb += expr_cap__value_out['h1]['h1]['h1] + var_cap__value_out['h1]['h1]['h1];
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    generate  // _assign
        genvar gi;
        genvar gj;
        genvar gk;
        genvar gx;
        genvar gy;
        genvar gz;
        assign expr_leaf__value_in = seed_in + unsigned'(16'(unsigned'(16'('h1))));
        assign var_leaf__value_in = scalar_source;
        for (gi = 'h0;gi < 'h3;gi=gi+1) begin
            assign expr_i__value_in[gi] = seed_in + unsigned'(16'(unsigned'(16'(('hA + gi)))));
            assign var_i__value_in[gi] = i_source[gi];
        end
        for (gj = 'h0;gj < 'h3;gj=gj+1) begin
            assign expr_j__value_in[gj] = seed_in + unsigned'(16'(unsigned'(16'(('h14 + gj)))));
            assign var_j__value_in[gj] = j_source[gj];
        end
        for (gi = 'h0;gi < 'h2;gi=gi+1) begin
            for (gj = 'h0;gj < 'h3;gj=gj+1) begin
                assign expr_ij__value_in[gi][gj] = seed_in + unsigned'(16'(unsigned'(16'((('h1E + (gi*'h5)) + gj)))));
                assign var_ij__value_in[gi][gj] = ij_source[gi][gj];
            end
        end
        for (gi = 'h0;gi < 'h2;gi=gi+1) begin
            for (gj = 'h0;gj < 'h2;gj=gj+1) begin
                for (gk = 'h0;gk < 'h2;gk=gk+1) begin
                    assign expr_ijk__value_in[gi][gj][gk] = seed_in + unsigned'(16'(unsigned'(16'(((('h3C + (gi*'hA)) + (gj*'h3)) + gk)))));
                    assign var_ijk__value_in[gi][gj][gk] = ijk_source[gi][gj][gk];
                end
            end
        end
        for (gx = 'h0;gx < 'h2;gx=gx+1) begin
            for (gy = 'h0;gy < 'h2;gy=gy+1) begin
                for (gz = 'h0;gz < 'h2;gz=gz+1) begin
                    assign expr_cap__value_in[gx][gy][gz] = seed_in + unsigned'(16'(unsigned'(16'(((('h5A + (gx*'hA)) + (gy*'h3)) + gz)))));
                    assign var_cap__value_in[gx][gy][gz] = cap_source[gx][gy][gz];
                end
            end
        end
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign result_out = result_comb;


endmodule
