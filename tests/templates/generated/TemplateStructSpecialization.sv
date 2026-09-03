`default_nettype none

import Predef_pkg::*;
import TemplateStructSpecSmall_pkg::*;
import TemplateStructPayload3_TemplateStructSpecSmall_alpha_pkg::*;
import TemplateStructSpecLarge_pkg::*;
import TemplateStructPayload6_TemplateStructSpecLarge_beta_pkg::*;


module TemplateStructSpecialization (
    input wire clk
,   input wire reset
,   input wire TemplateStructPayload3_TemplateStructSpecSmall_alpha small_in
,   input wire TemplateStructPayload6_TemplateStructSpecLarge_beta large_in
,   output wire[16-1:0] small_raw_out
,   output wire[16-1:0] large_raw_out
);


    // regs and combs
    logic[16-1:0] small_comb;
    logic[16-1:0] large_comb;

    // members

    // tmp variables


    always_comb begin : small_comb_func  // small_comb_func
        TemplateStructPayload3_TemplateStructSpecSmall_alpha payload;
        payload = small_in;
        small_comb = unsigned'(16'((((((payload.raw + payload.selected) + payload.formatted) + TemplateStructPayload3_TemplateStructSpecSmall_alpha_pkg::WIDTH_VALUE) + TemplateStructPayload3_TemplateStructSpecSmall_alpha_pkg::FORMAT_BITS) + TemplateStructPayload3_TemplateStructSpecSmall_alpha_pkg::TAG_FIRST)));
    end

    always_comb begin : large_comb_func  // large_comb_func
        TemplateStructPayload6_TemplateStructSpecLarge_beta payload;
        payload = large_in;
        large_comb = unsigned'(16'((((((payload.raw + payload.selected) + payload.formatted) + TemplateStructPayload6_TemplateStructSpecLarge_beta_pkg::WIDTH_VALUE) + TemplateStructPayload6_TemplateStructSpecLarge_beta_pkg::FORMAT_BITS) + TemplateStructPayload6_TemplateStructSpecLarge_beta_pkg::TAG_FIRST)));
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

    assign small_raw_out = small_comb;

    assign large_raw_out = large_comb;


endmodule
