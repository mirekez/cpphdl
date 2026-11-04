module legacy_clock(clk_in, clk_out);
  input clk_in;
  output clk_out;
  assign clk_out = clk_in;
endmodule

// An implicit parent clock must also connect to the APB clock spelling.
module counter(input wire pclk, input wire rst_n, output wire [7:0] count);
  reg [7:0] counter;
  always @(posedge pclk) if (!rst_n) counter <= 0; else counter <= counter + 1;
  assign count = counter;
endmodule

module legacy_operators(
  input wire clk,
  input wire rst_n,
  input wire [1:0] addr,
  input wire [12:0] data,
  output wire [12:0] high,
  output wire [12:0] low,
  output wire [12:0] xnor_word,
  output wire xnor_bit,
  output wire [7:0] count,
  output wire [63:0] repeated_low,
  output wire [10:0] repeated_high,
  output wire [6:0] other1,
  output wire [6:0] other2,
  output wire [1:0] selected,
  output wire [7:0] loop_bits,
  output wire [3:0] loop_index
);
  reg [12:0] hi [0:3];
  reg [12:0] lo [0:3];
  for (genvar row=0; row<4; row=row+1) begin : rows
    always @(posedge clk or negedge rst_n)
      if (!rst_n) {hi[row][12:0], lo[row][12:0]} <= 26'h123456;
      else if (addr == row) {hi[row][12:0], lo[row][12:0]} <= {data ^ 13'h155, data};
  end
  assign high = hi[addr];
  assign low = lo[addr];
  assign xnor_word = data ~^ 13'h156;
  assign xnor_bit = data[0] ^~ data[1];
  wire forwarded_clk;
  legacy_clock forwarder(.clk_in(clk), .clk_out(forwarded_clk));
  counter timer(.pclk(clk), .rst_n(rst_n), .count(count));
  wire [74:0] repeated;
  assign repeated = {{31{2'b10}}, 2'b00, 2'b01, 9'h155};
  assign repeated_low = repeated[63:0];
  assign repeated_high = repeated[74:64];
  // OpenC910 issue-queue concatenations select six/five bits from a seven-bit
  // net. Offsets must use the selected width, not the net's declared width.
  assign {other1[6:1], selected[0], other1[0]} = data[7:0];
  assign {other2[6:2], selected[1], other2[1:0]} = data[7:0];
  integer scan;
  reg [7:0] scanned;
  always @(posedge clk)
    if (!rst_n) begin
      for (scan=0; scan<8; scan=scan+1) scanned[scan] <= 0;
    end else begin
      for (scan=0; scan<8; scan++) scanned[scan] <= data[scan];
    end
  assign loop_bits = scanned;
  assign loop_index = scan;
endmodule
