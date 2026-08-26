`default_nettype none

import Predef_pkg::*;


module TemplateModuleBase (
    input wire clk
,   input wire reset
,   input wire[16-1:0] data_in
,   output wire[16-1:0] data_out
);
    localparam  MODULE_BITS = 64'h10;
    localparam  MODULE_MASK = 64'h3F;
    localparam  MODULE_EXTRA = 64'h4;
    localparam  EXTRA_BITS = 64'h4;
    localparam  TOTAL_BITS = 64'hA;
    localparam  BASE_BITS = 64'h6;
    localparam  BASE_MASK = 64'h3F;


    // regs and combs
    logic[16-1:0] data_comb;

    // members

    // tmp variables


    function logic[15:0] TemplateBaseGeometry6___trim (input logic[15:0] value);
        return unsigned'(16'((value & BASE_MASK)));
    endfunction

    function logic[15:0] TemplateBaseOpsTemplateBaseGeometry6___encode (input logic[15:0] value);
        return unsigned'(16'((((TemplateBaseGeometry6___trim(value) <<< EXTRA_BITS)) | BASE_BITS)));
    endfunction

    always_comb begin : data_comb_func  // data_comb_func
        logic[15:0] encoded;
        encoded=TemplateBaseOpsTemplateBaseGeometry6___encode(unsigned'(16'(data_in)));
        data_comb = unsigned'(16'(((encoded + MODULE_MASK) + MODULE_EXTRA)));
    end

    task _work (input logic unused);
    begin: _work
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign data_out = data_comb;


endmodule
