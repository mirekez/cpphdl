`default_nettype none

import Predef_pkg::*;


module MemberAssignGenvars (
    input wire clk
,   input wire reset
,   input wire[32-1:0] seed_in
,   output wire[32-1:0] result_out
);


    // regs and combs
    logic[32-1:0] source5[2][2][2][2][2];
    logic[32-1:0] result_comb;

    // members
    genvar __i, __j, __k, __l;
      wire[32-1:0] expr4__value_in[2][2][2][2];
      wire[32-1:0] expr4__value_out[2][2][2][2];
    generate
    for (__i=0; __i < 2; __i = __i + 1) begin
        for (__j=0; __j < 2; __j = __j + 1) begin
            for (__k=0; __k < 2; __k = __k + 1) begin
                for (__l=0; __l < 2; __l = __l + 1) begin
                    MemberAssignGenvarsLeaf                      expr4 (
                        .clk(clk)
                    ,           .reset(reset)
                    ,           .value_in(expr4__value_in[__i][__j][__k][__l])
                    ,           .value_out(expr4__value_out[__i][__j][__k][__l])
                    );
                end
            end
        end
    end
    endgenerate
      wire[32-1:0] var4__value_in[2][2][2][2];
      wire[32-1:0] var4__value_out[2][2][2][2];
    generate
    for (__i=0; __i < 2; __i = __i + 1) begin
        for (__j=0; __j < 2; __j = __j + 1) begin
            for (__k=0; __k < 2; __k = __k + 1) begin
                for (__l=0; __l < 2; __l = __l + 1) begin
                    MemberAssignGenvarsLeaf                      var4 (
                        .clk(clk)
                    ,           .reset(reset)
                    ,           .value_in(var4__value_in[__i][__j][__k][__l])
                    ,           .value_out(var4__value_out[__i][__j][__k][__l])
                    );
                end
            end
        end
    end
    endgenerate

    // tmp variables


    always_comb begin : result_comb_func  // result_comb_func
        source5['h0]['h0]['h0]['h0]['h1] = seed_in + unsigned'(32'('h7D1));
        source5['h0]['h0]['h0]['h1]['h1] = seed_in + unsigned'(32'('h7D2));
        source5['h0]['h0]['h1]['h0]['h1] = seed_in + unsigned'(32'('h7D3));
        source5['h0]['h0]['h1]['h1]['h1] = seed_in + unsigned'(32'('h7D4));
        source5['h0]['h1]['h0]['h0]['h1] = seed_in + unsigned'(32'('h7DB));
        source5['h0]['h1]['h0]['h1]['h1] = seed_in + unsigned'(32'('h7DC));
        source5['h0]['h1]['h1]['h0]['h1] = seed_in + unsigned'(32'('h7DD));
        source5['h0]['h1]['h1]['h1]['h1] = seed_in + unsigned'(32'('h7DE));
        source5['h1]['h0]['h0]['h0]['h1] = seed_in + unsigned'(32'('h835));
        source5['h1]['h0]['h0]['h1]['h1] = seed_in + unsigned'(32'('h836));
        source5['h1]['h0]['h1]['h0]['h1] = seed_in + unsigned'(32'('h837));
        source5['h1]['h0]['h1]['h1]['h1] = seed_in + unsigned'(32'('h838));
        source5['h1]['h1]['h0]['h0]['h1] = seed_in + unsigned'(32'('h83F));
        source5['h1]['h1]['h0]['h1]['h1] = seed_in + unsigned'(32'('h840));
        source5['h1]['h1]['h1]['h0]['h1] = seed_in + unsigned'(32'('h841));
        source5['h1]['h1]['h1]['h1]['h1] = seed_in + unsigned'(32'('h842));
        result_comb = expr4__value_out['h0]['h0]['h0]['h0] + var4__value_out['h0]['h0]['h0]['h0];
        result_comb += expr4__value_out['h0]['h0]['h0]['h1] + var4__value_out['h0]['h0]['h0]['h1];
        result_comb += expr4__value_out['h0]['h0]['h1]['h0] + var4__value_out['h0]['h0]['h1]['h0];
        result_comb += expr4__value_out['h0]['h0]['h1]['h1] + var4__value_out['h0]['h0]['h1]['h1];
        result_comb += expr4__value_out['h0]['h1]['h0]['h0] + var4__value_out['h0]['h1]['h0]['h0];
        result_comb += expr4__value_out['h0]['h1]['h0]['h1] + var4__value_out['h0]['h1]['h0]['h1];
        result_comb += expr4__value_out['h0]['h1]['h1]['h0] + var4__value_out['h0]['h1]['h1]['h0];
        result_comb += expr4__value_out['h0]['h1]['h1]['h1] + var4__value_out['h0]['h1]['h1]['h1];
        result_comb += expr4__value_out['h1]['h0]['h0]['h0] + var4__value_out['h1]['h0]['h0]['h0];
        result_comb += expr4__value_out['h1]['h0]['h0]['h1] + var4__value_out['h1]['h0]['h0]['h1];
        result_comb += expr4__value_out['h1]['h0]['h1]['h0] + var4__value_out['h1]['h0]['h1]['h0];
        result_comb += expr4__value_out['h1]['h0]['h1]['h1] + var4__value_out['h1]['h0]['h1]['h1];
        result_comb += expr4__value_out['h1]['h1]['h0]['h0] + var4__value_out['h1]['h1]['h0]['h0];
        result_comb += expr4__value_out['h1]['h1]['h0]['h1] + var4__value_out['h1]['h1]['h0]['h1];
        result_comb += expr4__value_out['h1]['h1]['h1]['h0] + var4__value_out['h1]['h1]['h1]['h0];
        result_comb += expr4__value_out['h1]['h1]['h1]['h1] + var4__value_out['h1]['h1]['h1]['h1];
        disable result_comb_func;
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    generate  // _assign
        genvar ga;
        genvar gb;
        genvar gc;
        genvar gd;
        genvar ge;
        for (ga = 'h0;ga < 'h2;ga=ga+1) begin
            for (gb = 'h0;gb < 'h2;gb=gb+1) begin
                for (gc = 'h0;gc < 'h2;gc=gc+1) begin
                    for (gd = 'h0;gd < 'h2;gd=gd+1) begin
                        for (ge = 'h0;ge < 'h2;ge=ge+1) begin
                            if (ge == 'h0) begin
                                assign expr4__value_in[ga][gb][gc][gd] = seed_in + unsigned'(32'(((((('h3E8 + (ga*'h64)) + (gb*'hA)) + (gc*'h2)) + gd) + ge)));
                            end
                            if (ge == 'h1) begin
                                assign var4__value_in[ga][gb][gc][gd] = source5[ga][gb][gc][gd][ge];
                            end
                        end
                    end
                end
            end
        end
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign result_out = result_comb;


endmodule
