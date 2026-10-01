// This is wiring, not a replacement arbiter. Match the R-channel arbiter in
// CVA6's RV32 outer AXI demux: 11 inputs, 64-bit data, 32-bit user, 4-bit ID.
module ArbiterBench #(
    parameter int INPUTS = 11
) (
    input logic clk_i,
    input logic rst_ni,
    input logic flush_i,
    input logic ready,
    input logic [INPUTS-1:0] requests,
    input logic [INPUTS-1:0][102:0] payload,
    output logic [INPUTS-1:0] grants,
    output logic valid,
    output logic [$clog2(INPUTS)-1:0] index,
    output logic [3:0] id,
    output logic [63:0] data,
    output logic [1:0] resp,
    output logic last,
    output logic [31:0] user
);
    typedef struct packed {
        logic [3:0] id;
        logic [63:0] data;
        logic [1:0] resp;
        logic last;
        logic [31:0] user;
    } r_chan_t;
    r_chan_t [INPUTS-1:0] channels;
    r_chan_t selected;
    assign channels = payload;
    assign {id, data, resp, last, user} = selected;
    rr_arb_tree #(
        .NumIn(INPUTS), .DataWidth(32), .DataType(r_chan_t), .ExtPrio(1'b0),
        .AxiVldRdy(1'b1), .LockIn(1'b1), .FairArb(1'b1)
    ) dut (
        .clk_i(clk_i), .rst_ni(rst_ni), .flush_i(flush_i), .rr_i('0), .req_i(requests),
        .gnt_o(grants), .data_i(channels), .req_o(valid),
        .gnt_i(ready), .data_o(selected), .idx_o(index)
    );
endmodule
