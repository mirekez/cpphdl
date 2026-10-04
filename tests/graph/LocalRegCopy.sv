// Independent RTL reference: locals are combinational copies, not extra flops.
module LocalRegCopy #(parameter int LANES = 1) (
    input logic clk, reset,
    input logic [7:0] data_in,
    output logic [7:0] result_out, last_out, committed_copy_out,
    output logic [63:0] audit_out
);
    logic [7:0] state [LANES];
    logic [7:0] first_copy, next_copy;
    assign result_out = state[0];
    assign last_out = state[LANES-1];
    assign first_copy = data_in ^ 8'ha5;
    assign next_copy = data_in ^ 8'h5a;
    always @(posedge clk) begin
        for (int i = 0; i < LANES; ++i) state[i] <= reset ? 0 : 8'(data_in + i);
        state[0] <= reset ? 0 : data_in ^ 8'h3c;
        audit_out <= reset ? 0 : {8'(next_copy + (data_in[0] ? 4 : 7)), 8'(first_copy + 3), next_copy,
                                 state[0], 8'(data_in + 2), 8'(state[0] + 1),
                                 next_copy, first_copy};
        committed_copy_out <= (reset ? 8'h0 : data_in ^ 8'h3c) ^ 8'he7;
    end
endmodule
