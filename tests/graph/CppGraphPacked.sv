module CppGraphPacked(
    input logic clk_i,
    input logic rst_ni,
    input logic enable_i,
    input logic [63:0] first_i,
    input logic [63:0] second_i,
    output logic [127:0] result_o,
    output logic [127:0] state_o
);
  typedef logic [63:0] word_t;
  word_t [0:0][1:0] combined;
  word_t [0:0][1:0] retained;

  always_comb begin
    combined = '0;
    combined[0][0] = first_i;
    combined[0][1] = second_i;
  end

  always_ff @(posedge clk_i) begin
    if (!rst_ni) retained <= '0;
    else begin
      retained[0][0] <= first_i;
      if (enable_i) retained[0][1] <= second_i;
    end
  end

  assign result_o = combined[0];
  assign state_o = retained[0];
endmodule
