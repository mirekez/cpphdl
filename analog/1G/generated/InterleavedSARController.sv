`default_nettype none

import Predef_pkg::*;


module InterleavedSARController (
    input wire clk
,   input wire reset
,   input wire enable_in
,   input wire sample_enable_in
,   input wire[16-1:0] comparisons_in
,   output wire[8-1:0] trial_out[16]
,   output wire acquire_out[16]
,   output wire convert_out[16]
,   output wire[8-1:0] data_out
,   output wire[4-1:0] slice_out
,   output wire valid_out
,   output wire fault_out
);


    // regs and combs
    reg[16-1:0] launch_slot;
    reg[4-1:0] slice;
    reg[8-1:0] data;
    reg valid;
    reg fault;

    // members
    genvar __i;
    wire converters__start_in[16];
    wire converters__comparison_in[16];
    wire[8-1:0] converters__trial_out[16];
    wire[8-1:0] converters__data_out[16];
    wire converters__valid_out[16];
    wire converters__acquire_out[16];
    wire converters__convert_out[16];
    wire converters__busy_out[16];
    generate
    for (__i=0; __i < 16; __i = __i + 1) begin
        ADCSARController          converters (
            .clk(clk)
        ,           .reset(reset || !enable_in)
        ,           .start_in(converters__start_in[__i])
        ,           .comparison_in(converters__comparison_in[__i])
        ,           .trial_out(converters__trial_out[__i])
        ,           .data_out(converters__data_out[__i])
        ,           .valid_out(converters__valid_out[__i])
        ,           .acquire_out(converters__acquire_out[__i])
        ,           .convert_out(converters__convert_out[__i])
        ,           .busy_out(converters__busy_out[__i])
        );
    end
    endgenerate

    // tmp variables
    logic[16-1:0] launch_slot_tmp;
    logic[4-1:0] slice_tmp;
    logic[8-1:0] data_tmp;
    logic valid_tmp;
    logic fault_tmp;


    generate  // _assign
        genvar gi;
        for (gi = 32'h0;gi < 32'h10;gi=gi+1) begin
            assign converters__start_in[gi] = (((enable_in && sample_enable_in) && ((((((unsigned'(32'(launch_slot)) >>> gi)) & 32'h1))) != '0)) != '0);
            assign converters__comparison_in[gi] = ((((unsigned'(32'(comparisons_in)) >>> gi)) & 32'h1) != '0);
            assign trial_out[gi] = converters__trial_out[gi];
            assign acquire_out[gi] = ((converters__acquire_out[gi]) != '0);
            assign convert_out[gi] = ((converters__convert_out[gi]) != '0);
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] mask;
        logic[31:0] bits;
        logic[4-1:0][31:0] group;
        logic[4-1:0][31:0] values;
        logic[4-1:0][31:0] indexes;
        logic[31:0] multiple;
        mask=32'h0;
        multiple=32'h0;
        for (logic[31:0] i = 32'h0;i < 32'h4;i=i+1) begin
            group[i]=32'h0;
            values[i]=32'h0;
            indexes[i]=32'h0;
        end
        launch_slot_tmp = ((launch_slot <<< 'h1)) | ((launch_slot >>> 32'shF));
        valid_tmp = (('h0) != '0);
        for (logic[31:0] i = 32'h0;i < 32'h10;i=i+1) begin
            if (converters__valid_out[i]) begin
                mask|='h1 <<< i;
                values[i/32'h4]=converters__data_out[i];
                indexes[i/32'h4]=i;
            end
            if (((enable_in && sample_enable_in) && ((((((unsigned'(32'(launch_slot)) >>> i)) & 32'h1))) != '0)) && converters__busy_out[i]) begin
                fault_tmp = (('h1) != '0);
            end
        end
        for (logic[31:0] i = 32'h0;i < 32'h4;i=i+1) begin
            bits=((mask >>> ((32'h4*i)))) & 32'hF;
            group[i]=bits != 32'h0;
            if (((group[i]) != '0)) begin
                data_tmp = values[i];
                slice_tmp = indexes[i];
            end
            multiple|=32'(((((bits & 32'h3)) == 32'h3) || (((bits & 32'hC)) == 32'hC)) || (((((bits & 32'h3)) != 32'h0) && (((bits & 32'hC)) != 32'h0))));
        end
        valid_tmp = ((((group['h0]) != '0) || ((group['h1]) != '0)) || ((group['h2]) != '0)) || ((group['h3]) != '0);
        if (((((multiple) != '0) || ((((group['h0]) != '0) && ((group['h1]) != '0)))) || ((((group['h2]) != '0) && ((group['h3]) != '0)))) || ((((((group['h0]) != '0) || ((group['h1]) != '0))) && ((((group['h2]) != '0) || ((group['h3]) != '0)))))) begin
            fault_tmp = (('h1) != '0);
            valid_tmp = (('h0) != '0);
        end
        if (reset || !enable_in) begin
            launch_slot_tmp = 64'h1;
            slice_tmp = 64'h0;
            data_tmp = 64'h0;
            valid_tmp = (('h0) != '0);
            fault_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        launch_slot_tmp = launch_slot;
        slice_tmp = slice;
        data_tmp = data;
        valid_tmp = valid;
        fault_tmp = fault;

            _work(reset);

        launch_slot <= launch_slot_tmp;
        slice <= slice_tmp;
        data <= data_tmp;
        valid <= valid_tmp;
        fault <= fault_tmp;
    end

    assign data_out = data;

    assign slice_out = slice;

    assign valid_out = valid;

    assign fault_out = fault;


endmodule
