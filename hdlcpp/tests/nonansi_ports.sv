module legacy_ram(q, clk, addr, data, we);
  parameter WIDTH = 8;
  parameter ADDR = 2;
  parameter DEPTH = 2**ADDR;
  localparam LAST = DEPTH - 1;
  input clk, we;
  input [ADDR-1:0] addr;
  input [WIDTH-1:0] data;
  output [WIDTH-1:0] q;
  wire [ADDR-1:0] addr;
  reg [WIDTH-1:0] q;
  reg [WIDTH-1:0] mem [0:LAST];
  always @(posedge clk)
    if (we) mem[addr] <= data;
    else q <= mem[addr];
endmodule

module legacy_sensitivity(y, a, b, sel);
  input [12:0] a, b;
  input sel;
  output [12:0] y;
  reg [12:0] y, selected;
  // Nonblocking syntax here still describes combinational hardware.
  always @(a or b, sel) selected <= sel ? a : b;
  generate if (1) begin : output_stage
    always @(selected) y <= selected ^ 13'h155;
  end endgenerate
endmodule

module nonansi_ports(
  input wire clk,
  input wire we,
  input wire [3:0] addr,
  input wire [12:0] data,
  output wire [12:0] wide,
  output wire [7:0] narrow,
  output wire [12:0] live
);
  // Positional ports follow the header, not the body declaration order.
  legacy_ram #(13, 4) wider(wide, clk, addr, data, we);
  legacy_ram bytes(.clk(clk), .we(we), .addr(addr[1:0]), .data(data[7:0]), .q(narrow));
  legacy_sensitivity comb(live, data, {9'b0,addr}, we);
endmodule
