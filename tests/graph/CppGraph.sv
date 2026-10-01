module CppGraph(input logic clk_i,
                input logic rst_ni,
                input logic choose_i,
                input logic [7:0] data_i,
                input logic [7:0] other_i,
                output logic [7:0] result_o,
                output logic [7:0] state_o);
  logic [7:0] next_state;
  always_comb begin
    next_state = state_o;
    if (choose_i) next_state = data_i + other_i;
    else next_state = data_i ^ other_i;
  end
  always_ff @(posedge clk_i) begin
    if (!rst_ni) state_o <= 0;
    else state_o <= next_state;
  end
  assign result_o = state_o ^ data_i;
endmodule
