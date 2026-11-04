module legacy_ports #(
  parameter WIDTH = 8,
  parameter PTR = {{(WIDTH-1){1'b0}}, 1'b1}
)(
  input wire [7:0] data,
  input wire [7:0] enable,
  input integer log_input,
  output wire [8:0] wide,
  output wire [7:0] narrow,
  output wire [7:0] array_value,
  output wire [7:0] pad_value,
  output wire [7:0] implicit_value,
  output wire [7:0] sync_value,
  output wire [31:0] log_value,
  output wire [7:0] case_value
);
  // Children deliberately follow their parent in this source file.
  port_collision collision(.data(data), .value(wide), .value_out(narrow));
  for (genvar i=0; i<2; i=i+1) begin : pieces
    later_leaf #(4) leaf(.data(data[i*4+:4]), .value(array_value[i*4+:4]));
  end
  wire [7:0] pads;
  output_only_pad gpio(.data(data), .enable(enable), .pad(pads), .readback(pad_value));
  assign create_en = enable[0]; // legal implicit scalar net
  assign implicit_value = create_en ? data : PTR;
  sync synchronizer(.data(data), .value(sync_value));
  function automatic integer count_bits(input integer a);
    for (count_bits=32'd0; a>0; count_bits=count_bits+32'd1)
      a = a >>> 1;
  endfunction
  assign log_value = count_bits(log_input);
  parameter_cases sized(.select(data[2:0]), .value(case_value));
endmodule

module parameter_cases(select, value);
  parameter BYTE = 2'b00, HALF = 2'b01, WORD = 2'b10;
  localparam TAG = 3'b101;
  input [2:0] select;
  output reg [7:0] value;
  always @* begin
    case (select)
      {1'b1, BYTE}: value = {TAG, 5'b00010};
      {1'b1, HALF}: value = {TAG, 5'b00100};
      {1'b1, WORD}: value = {TAG, 5'b01000};
      default: value = {TAG, 5'b00001};
    endcase
  end
endmodule

module sync(input wire [7:0] data, output wire [7:0] value);
  assign value = data;
endmodule

module port_collision(data, value, value_out);
  input [7:0] data;
  output [8:0] value;
  output [7:0] value_out;
  assign value = {data, 1'b1};
  assign value_out = data ^ 8'ha5;
endmodule

module later_leaf(data, value);
  parameter WIDTH = 4;
  input [WIDTH-1:0] data;
  output [WIDTH-1:0] value;
  assign value = data;
endmodule

module output_only_pad(data, enable, pad, readback);
  input [7:0] data, enable;
  inout [7:0] pad;
  output [7:0] readback;
  for (genvar i=0; i<8; i=i+1) begin : bits
    assign pad[i] = enable[i] ? data[i] : 1'bz;
  end
  assign readback = pad;
endmodule
