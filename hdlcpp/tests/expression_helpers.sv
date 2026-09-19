module expression_helpers #(
    parameter int unsigned Mode = 1
) (
    input logic [63:0] data_i,
    input logic [1:0] word_i,
    output logic [4095:0] prefix_o,
    output logic [63:0] tree_o,
    output logic [7:0] selected_o,
    output logic [7:0] guarded_o
);
    for (genvar span = 0; span < 64; span++) begin : gen_prefix
        assign prefix_o[span*64+:64] = {1'b1, data_i[span:0]};
    end
    assign tree_o[7:0] = data_i[7:0];
    for (genvar level = 1; level < 8; level++) begin : gen_tree
        assign tree_o[level*8+:8] = {1'b1, tree_o[(level-1)*8+:8-level]};
    end
    for (genvar lane = 0; lane < 8; lane++) begin : gen_select
        logic selected;
        logic guarded;
        always_comb begin
            automatic int unsigned word;
            word = lane / 2;
            selected = data_i[lane] && (word == word_i);
        end
        if (Mode == 1) begin
            assign guarded = data_i[lane];
        end else begin
            assign guarded = data_i[lane] ^ 1'b1;
        end
        assign selected_o[lane] = selected;
        assign guarded_o[lane] = guarded;
    end
endmodule
