`default_nettype none

import Predef_pkg::*;


module ReceiveQuality (
    input wire clk
,   input wire reset
,   input wire[10-1:0] receive_in
,   input wire valid_in
,   input wire locked_in
,   input wire fault_in
,   input wire clipped_in
,   input wire enable_in
,   output wire ready_out
,   output wire[6-1:0] flushed_out
,   output wire[7-1:0] idles_out
);
    localparam  PIPELINE_WORDS = 'h27;


    // regs and combs
    reg[6-1:0] flushed;
    reg[7-1:0] idles;
    reg qualified;

    // members

    // tmp variables
    logic[6-1:0] flushed_tmp;
    logic[7-1:0] idles_tmp;
    logic qualified_tmp;


    function logic bad (
        input logic enabled
,       input logic locked
,       input logic fault
,       input logic clipped
,       input logic valid
,       input logic[31:0] word
    );
        return (((!enabled || !locked) || fault) || clipped) || ((valid && (((word & 32'h200)) != 32'h0)));
    endfunction

    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        if (valid_in && (flushed < unsigned'(64'(PIPELINE_WORDS)))) begin
            flushed_tmp = flushed + 64'h1;
        end
        else begin
            if (valid_in && !((qualified) != '0)) begin
                if (receive_in == 64'h0) begin
                    if (idles < 64'h40) begin
                        idles_tmp = idles + 64'h1;
                    end
                    if (idles>=64'h3F) begin
                        qualified_tmp = (('h1) != '0);
                    end
                end
                else begin
                    idles_tmp = 64'h0;
                end
            end
        end
        if (bad(enable_in, locked_in, fault_in, clipped_in, valid_in, unsigned'(32'(receive_in))) || reset) begin
            flushed_tmp = 64'h0;
            idles_tmp = 64'h0;
            qualified_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        flushed_tmp = flushed;
        idles_tmp = idles;
        qualified_tmp = qualified;

            _work(reset);

        flushed <= flushed_tmp;
        idles <= idles_tmp;
        qualified <= qualified_tmp;
    end

    assign ready_out = ((((qualified) != '0) && !bad(enable_in, locked_in, fault_in, clipped_in, valid_in, unsigned'(32'(receive_in)))) != '0);

    assign flushed_out = flushed;

    assign idles_out = idles;


endmodule
