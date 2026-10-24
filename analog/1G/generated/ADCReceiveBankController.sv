`default_nettype none

import Predef_pkg::*;


module ADCReceiveBankController #(
    parameter Slices = 'hD
 )
 (
    input wire clk
,   input wire reset
,   input wire enable_in
,   input wire sample_enable_in
,   input wire precharge_in
,   input wire[16-1:0] comparisons_in
,   input wire[8-1:0] offset_in
,   input wire[16-1:0] gain_in
,   output wire[8-1:0] trial_out[16]
,   output wire acquire_out[16]
,   output wire convert_out[16]
,   output wire[8-1:0] data_out
,   output wire[4-1:0] slice_out
,   output wire valid_out
,   output wire fault_out
,   output wire[8-1:0] normalized_out
,   output wire normalized_valid_out
,   output wire clipped_out
,   output wire[4-1:0] normalized_slice_out
);


    // regs and combs
    reg[4-1:0] slice_delay[8];

    // members
    wire adc__enable_in;
    wire adc__sample_enable_in;
    wire[unsigned'(64'(Slices))-1:0] adc__comparisons_in;
    wire[8-1:0] adc__trial_out[Slices];
    wire adc__acquire_out[Slices];
    wire adc__convert_out[Slices];
    wire[8-1:0] adc__data_out;
    wire[4-1:0] adc__slice_out;
    wire adc__valid_out;
    wire adc__fault_out;
    InterleavedSARBankController #(
        Slices
    ) adc (
        .clk(clk)
,       .reset(reset)
,       .enable_in(adc__enable_in)
,       .sample_enable_in(adc__sample_enable_in)
,       .comparisons_in(adc__comparisons_in)
,       .trial_out(adc__trial_out)
,       .acquire_out(adc__acquire_out)
,       .convert_out(adc__convert_out)
,       .data_out(adc__data_out)
,       .slice_out(adc__slice_out)
,       .valid_out(adc__valid_out)
,       .fault_out(adc__fault_out)
    );
    wire[8-1:0] normalizer__sample_in;
    wire[8-1:0] normalizer__offset_in;
    wire[16-1:0] normalizer__gain_in;
    wire normalizer__valid_in;
    wire normalizer__enable_in;
    wire[8-1:0] normalizer__sample_out;
    wire normalizer__valid_out;
    wire normalizer__clipped_out;
    ReceiveSampleNormalizer      normalizer (
        .clk(clk)
,       .reset(reset)
,       .sample_in(normalizer__sample_in)
,       .offset_in(normalizer__offset_in)
,       .gain_in(normalizer__gain_in)
,       .valid_in(normalizer__valid_in)
,       .enable_in(normalizer__enable_in)
,       .sample_out(normalizer__sample_out)
,       .valid_out(normalizer__valid_out)
,       .clipped_out(normalizer__clipped_out)
    );

    // tmp variables
    logic[4-1:0] slice_delay_tmp[8];


    generate  // _assign
        genvar gi;
        assign adc__enable_in = ((enable_in && !precharge_in) != '0);
        assign adc__sample_enable_in = ((sample_enable_in) != '0);
        assign adc__comparisons_in = (unsigned'(64'(Slices)))'(64'(unsigned'(32'(comparisons_in))));
        for (gi = 32'h0;gi < 32'h10;gi=gi+1) begin
            if (gi < Slices) begin
                assign trial_out[gi] = adc__trial_out[gi];
                assign acquire_out[gi] = ((enable_in && ((precharge_in || adc__acquire_out[gi]))) != '0);
                assign convert_out[gi] = ((adc__convert_out[gi]) != '0);
            end
            else begin
                assign trial_out[gi] = 'h0;
                assign acquire_out[gi] = ((0) != '0);
                assign convert_out[gi] = ((0) != '0);
            end
        end
        assign normalizer__sample_in = adc__data_out;
        assign normalizer__valid_in = ((adc__valid_out) != '0);
        assign normalizer__offset_in = offset_in;
        assign normalizer__gain_in = gain_in;
        assign normalizer__enable_in = ((enable_in && !precharge_in) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
        slice_delay_tmp['h0] = adc__slice_out;
        for (logic[31:0] i = 32'h1;i < 32'h8;i=i+1) begin
            slice_delay_tmp[i] = slice_delay[i - 32'h1];
        end
        if ((reset || !enable_in) || precharge_in) begin
            for (logic[31:0] i = 32'h0;i < 32'h8;i=i+1) begin
                slice_delay_tmp[i] = 64'h0;
            end
        end
    end
    endtask

    always @(posedge clk) begin
        slice_delay_tmp = slice_delay;

            _work(reset);

        slice_delay <= slice_delay_tmp;
    end

    assign data_out = adc__data_out;

    assign slice_out = adc__slice_out;

    assign valid_out = (((enable_in && !precharge_in) && adc__valid_out) != '0);

    assign fault_out = ((adc__fault_out) != '0);

    assign normalized_out = normalizer__sample_out;

    assign normalized_valid_out = (((enable_in && !precharge_in) && normalizer__valid_out) != '0);

    assign clipped_out = ((normalizer__clipped_out) != '0);

    assign normalized_slice_out = slice_delay['h7];


endmodule
