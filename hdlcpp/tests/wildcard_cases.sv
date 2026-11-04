module wildcard_tag #(parameter TAG = 1'b1)(input wire [4:0] data, output reg [1:0] decoded);
  always @* casez (data)
    {TAG, {2{2'b?1}}}: decoded = 3;
    {TAG, 4'b????}: decoded = 1;
    default: decoded = 2;
  endcase
endmodule

module wildcard_cases(
  input wire [15:0] data,
  input wire [3:0] fallback,
  input wire [127:0] wide,
  output reg [3:0] first_zero,
  output reg [2:0] xdecode,
  output reg [2:0] zdecode,
  output reg [1:0] wide_decode,
  output reg strict_z,
  output reg [1:0] context_decode,
  output wire [1:0] symbolic0,
  output wire [1:0] symbolic1
);
  wildcard_tag #(.TAG(1'b0)) tag0(.data(data[4:0]), .decoded(symbolic0));
  wildcard_tag tag1(.data(data[4:0]), .decoded(symbolic1));
  // OpenC910's MMU priority decoder: exhaustive without a default clause.
  always @* casez (data)
      16'b???????????????0: first_zero = 4'd0;
      16'b??????????????01: first_zero = 4'd1;
      16'b?????????????011: first_zero = 4'd2;
      16'b????????????0111: first_zero = 4'd3;
      16'b???????????01111: first_zero = 4'd4;
      16'b??????????011111: first_zero = 4'd5;
      16'b?????????0111111: first_zero = 4'd6;
      16'b????????01111111: first_zero = 4'd7;
      16'b???????011111111: first_zero = 4'd8;
      16'b??????0111111111: first_zero = 4'd9;
      16'b?????01111111111: first_zero = 4'd10;
      16'b????011111111111: first_zero = 4'd11;
      16'b???0111111111111: first_zero = 4'd12;
      16'b??01111111111111: first_zero = 4'd13;
      16'b?011111111111111: first_zero = 4'd14;
      16'b0111111111111111: first_zero = 4'd15;
      16'b1111111111111111: first_zero = fallback;
    endcase
  always @* casex (data[3:0])
    4'b1xxx: xdecode = 1;
    4'b01zz: xdecode = 2;
    4'b001?: xdecode = 3;
    4'b0000: xdecode = 4;
    4'b0001: xdecode = 5;
  endcase
  // Keep first-match priority, including a default written before the labels.
  always @* casez (data[7:0])
    default: zdecode = 7;
    8'h?f: zdecode = 1;
    {4'b1???, 4'b????}: zdecode = 2;
    8'o1??: zdecode = 3;
  endcase
  always @* casez (wide)
    128'h8???_????_????_????_????_????_????_????: wide_decode = 1;
    128'h?fff_ffff_ffff_ffff_ffff_ffff_ffff_ffff: wide_decode = 2;
    default: wide_decode = 3;
  endcase
  always @* casez (data[1:0])
    2'bx0: strict_z = 0;
    default: strict_z = 1;
  endcase
  always @* casez (data[3:0])
    ('1): context_decode = 1;
    8'h0f: context_decode = 2;
    default: context_decode = 0;
  endcase
endmodule
