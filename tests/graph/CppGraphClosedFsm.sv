module ClosedFsm(input logic clk, input logic rst_n, input logic advance,
                 output logic [2:0] state);
  logic [2:0] next_state;
  always @(posedge clk)
    if (!rst_n) state <= 3'd0;
    else state <= next_state;
  // OpenC910 PMU pattern: all transitions stay in 0..4. The undefined
  // fallback for 5..7 is unreachable from value-initialized/reset state.
  always @* case (state)
    3'd0: next_state = advance ? 3'd1 : 3'd0;
    3'd1: next_state = advance ? 3'd2 : 3'd1;
    3'd2: next_state = advance ? 3'd3 : 3'd2;
    3'd3: next_state = advance ? 3'd4 : 3'd3;
    3'd4: next_state = advance ? 3'd0 : 3'd4;
  endcase
endmodule
