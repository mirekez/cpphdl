module RequestTreeBench #(
    parameter int INPUTS = 16,
    parameter int TREE_BITS = (1 << $clog2(INPUTS)) - 1
) (
    input logic [INPUTS-1:0] requests,
    output logic [TREE_BITS-1:0] tree
);
    rr_arb_tree #(.NumIn(INPUTS), .DataWidth(1), .ExtPrio(1'b1)) dut (
        .clk_i(1'b0), .rst_ni(1'b1), .flush_i(1'b0), .rr_i('0),
        .req_i(requests), .gnt_i(1'b1), .data_i('0),
        .gnt_o(), .req_o(), .data_o(), .idx_o()
    );
    assign tree = dut.gen_arbiter.req_nodes;
endmodule
