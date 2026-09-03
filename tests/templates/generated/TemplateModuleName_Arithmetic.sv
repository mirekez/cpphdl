`default_nettype none

import Predef_pkg::*;

module TemplateModuleName_Arithmetic #(parameter int PARAM1 = 0, parameter int PARAM2 = 0)
 (
    input wire clk,
    input wire reset,
    input wire[16-1:0] value_in,
    output wire[16-1:0] value_out
);
    assign value_out = value_in ^ 16'(PARAM1 + PARAM2 + 16'h1234);
endmodule
