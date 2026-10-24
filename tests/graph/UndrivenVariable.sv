interface UndrivenInterface;
  logic [7:0] external_value;
  logic [1:0][7:0] external_bank;
endinterface

module UndrivenChild(input logic [7:0] data_i, output logic [7:0] data_o);
  assign data_o = data_i + 8'd1;
endmodule

module UndrivenVariable #(parameter bit ENABLE = 0)
  (input logic clk_i, rst_ni, input logic [7:0] data_i,
   output logic [31:0] result_o, output logic [31:0] aggregate_o);
  typedef struct packed {
    logic [2:0] mode;
    logic [4:0] permissions;
  } config_t;
  config_t empty_struct, driven;
  config_t [1:0] empty_array;
  logic [7:0] unused_backing [2];
  logic [7:0] initial_hook;
  initial initial_hook = 8'h12;
  logic [7:0] optional_q, untouched, child_data, written;
  if (ENABLE) begin
    always_ff @(posedge clk_i or negedge rst_ni)
      if (!rst_ni) optional_q <= 0;
      else optional_q <= data_i;
  end
  UndrivenChild child(data_i, child_data);
  always_comb begin
    written[3:0] = data_i[3:0];
    written[7:4] = ~data_i[7:4];
    driven.mode = data_i[7:5];
    driven.permissions = data_i[4:0];
  end
  assign result_o = {optional_q, untouched, child_data, written};
  assign aggregate_o = {empty_struct, empty_array, driven};
endmodule
