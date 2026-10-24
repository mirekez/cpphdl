`default_nettype none

import Predef_pkg::*;


module PCSReceiver (
    input wire clk
,   input wire reset
,   input wire[32-1:0] samples_in
,   input wire symbol_enable_in
,   output wire receive_valid_out
,   input wire remote_master_in
,   input wire pma_ok_in
,   output wire[10-1:0] receive_out
,   output wire locked_out
,   output wire remote_receiver_ok_out
);
    localparam  TAG_BITS = 'hB;
    localparam  TAG_DEPTH = 'h25;
    localparam  RECEIVE_LATENCY = 'h27;


    // regs and combs
    reg[12-1:0] hard_delayed;
    reg[32-1:0] samples_delayed;
    reg[32-1:0] samples_for_metrics;
    reg[28-1:0] control_history;
    reg[407-1:0] tags;
    reg[10-1:0] output_reg;
    reg output_valid;

    // members
    wire sync__symbol_enable_in;
    wire[12-1:0] sync__symbols_in;
    wire sync__remote_master_in;
    wire sync__pma_ok_in;
    wire sync__check_idle_in;
    wire sync__restart_in;
    wire sync__locked_out;
    wire[33-1:0] sync__state_out;
    wire[4-1:0] sync__sx_out;
    wire[4-1:0] sync__sy_out;
    wire[4-1:0] sync__sg_out;
    wire[4-1:0] sync__low_mask_out;
    wire[4-1:0] sync__polarity_out;
    wire sync__remote_receiver_ok_out;
    PCSReceiveSynchronizer      sync (
        .clk(clk)
,       .reset(reset)
,       .symbol_enable_in(sync__symbol_enable_in)
,       .symbols_in(sync__symbols_in)
,       .remote_master_in(sync__remote_master_in)
,       .pma_ok_in(sync__pma_ok_in)
,       .check_idle_in(sync__check_idle_in)
,       .restart_in(sync__restart_in)
,       .locked_out(sync__locked_out)
,       .state_out(sync__state_out)
,       .sx_out(sync__sx_out)
,       .sy_out(sync__sy_out)
,       .sg_out(sync__sg_out)
,       .low_mask_out(sync__low_mask_out)
,       .polarity_out(sync__polarity_out)
,       .remote_receiver_ok_out(sync__remote_receiver_ok_out)
    );
    wire framer__symbol_enable_in;
    wire[12-1:0] framer__symbols_in;
    wire[4-1:0] framer__sg_in;
    wire[4-1:0] framer__sx_in;
    wire[4-1:0] framer__low_mask_in;
    wire[4-1:0] framer__polarity_in;
    wire framer__locked_in;
    wire framer__pma_ok_in;
    wire[4-1:0] framer__negate_out;
    wire[7-1:0] framer__control_out;
    wire[20-1:0] framer__tag_out;
    wire framer__restart_out;
    wire framer__remote_receiver_ok_out;
    wire framer__receiving_out;
    PCSReceiveFramer      framer (
        .clk(clk)
,       .reset(reset)
,       .symbol_enable_in(framer__symbol_enable_in)
,       .symbols_in(framer__symbols_in)
,       .sg_in(framer__sg_in)
,       .sx_in(framer__sx_in)
,       .low_mask_in(framer__low_mask_in)
,       .polarity_in(framer__polarity_in)
,       .locked_in(framer__locked_in)
,       .pma_ok_in(framer__pma_ok_in)
,       .negate_out(framer__negate_out)
,       .control_out(framer__control_out)
,       .tag_out(framer__tag_out)
,       .restart_out(framer__restart_out)
,       .remote_receiver_ok_out(framer__remote_receiver_ok_out)
,       .receiving_out(framer__receiving_out)
    );
    wire metrics__symbol_enable_in;
    wire[32-1:0] metrics__samples_in;
    wire[4-1:0] metrics__negate_in;
    wire metrics__advance_in;
    wire[18-1:0] metrics__metric_out[8];
    wire[6-1:0] metrics__point_out[8];
    wire metrics__valid_out;
    PipelinedPCSBranchMetrics      metrics (
        .clk(clk)
,       .reset(reset)
,       .symbol_enable_in(metrics__symbol_enable_in)
,       .samples_in(metrics__samples_in)
,       .negate_in(metrics__negate_in)
,       .advance_in(metrics__advance_in)
,       .metric_out(metrics__metric_out)
,       .point_out(metrics__point_out)
,       .valid_out(metrics__valid_out)
    );
    wire decoder__symbol_enable_in;
    wire[18-1:0] decoder__metric_in[8];
    wire[6-1:0] decoder__point_in[8];
    wire decoder__advance_in;
    wire decoder__start_in;
    wire decoder__end_in;
    wire[8-1:0] decoder__data_out;
    wire decoder__valid_out;
    wire[3-1:0] decoder__state_out;
    PCSTrellisDecoder      decoder (
        .clk(clk)
,       .reset(reset)
,       .symbol_enable_in(decoder__symbol_enable_in)
,       .metric_in(decoder__metric_in)
,       .point_in(decoder__point_in)
,       .advance_in(decoder__advance_in)
,       .start_in(decoder__start_in)
,       .end_in(decoder__end_in)
,       .data_out(decoder__data_out)
,       .valid_out(decoder__valid_out)
,       .state_out(decoder__state_out)
    );

    // tmp variables
    logic[12-1:0] hard_delayed_tmp;
    logic[32-1:0] samples_delayed_tmp;
    logic[32-1:0] samples_for_metrics_tmp;
    logic[28-1:0] control_history_tmp;
    logic[407-1:0] tags_tmp;
    logic[10-1:0] output_reg_tmp;
    logic output_valid_tmp;


    function logic[31:0] decoder_control (input logic[31:0] history);
        return ((history >>> 32'sh15)) & 32'h7F;
    endfunction

    function logic[31:0] slice (input logic[31:0] samples);
        logic[31:0] _packed;
        logic[31:0] sample;
        logic[31:0] code;
        _packed=32'h0;
        for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
            sample=((samples >>> ((lane*32'h8)))) & 32'hFF;
            code=(sample<=32'h50) ? ('h0) : ((sample<=32'h70) ? ('h1) : ((sample<=32'h90) ? ('h2) : ((sample<=32'hB0) ? ('h3) : ('h4))));
            _packed=_packed | ((code <<< ((lane*32'h3))));
        end
        return _packed;
    endfunction

    generate  // _assign
        genvar gi;
        assign sync__symbol_enable_in = ((symbol_enable_in) != '0);
        assign framer__symbol_enable_in = ((symbol_enable_in) != '0);
        assign metrics__symbol_enable_in = ((symbol_enable_in) != '0);
        assign decoder__symbol_enable_in = ((symbol_enable_in) != '0);
        assign sync__symbols_in = slice(unsigned'(32'(samples_in)));
        assign sync__remote_master_in = ((remote_master_in) != '0);
        assign sync__pma_ok_in = ((pma_ok_in) != '0);
        assign sync__check_idle_in = ((0) != '0);
        assign sync__restart_in = ((framer__restart_out) != '0);
        assign framer__symbols_in = hard_delayed;
        assign framer__sg_in = sync__sg_out;
        assign framer__sx_in = sync__sx_out;
        assign framer__low_mask_in = sync__low_mask_out;
        assign framer__polarity_in = sync__polarity_out;
        assign framer__locked_in = ((sync__locked_out) != '0);
        assign framer__pma_ok_in = ((pma_ok_in) != '0);
        assign metrics__samples_in = samples_for_metrics;
        assign metrics__negate_in = framer__negate_out;
        assign metrics__advance_in = ((((framer__control_out & 64'h1)) != 64'h0) != '0);
        for (gi = 32'h0;gi < 32'h8;gi=gi+1) begin
            assign decoder__metric_in[gi] = ((((decoder_control(unsigned'(32'(control_history))) & 32'h8)) != '0)) ? (unsigned'(32'(((((((decoder_control(unsigned'(32'(control_history))) >>> 32'sh4)) & 32'h7)) == gi) ? ('h0) : ('h3FFFF))))) : (unsigned'(32'(metrics__metric_out[gi])));
            assign decoder__point_in[gi] = ((((decoder_control(unsigned'(32'(control_history))) & 32'h8)) != '0)) ? (32'h0) : (unsigned'(32'(metrics__point_out[gi])));
        end
        assign decoder__advance_in = ((((decoder_control(unsigned'(32'(control_history))) & 32'h1)) != 32'h0) != '0);
        assign decoder__start_in = ((((decoder_control(unsigned'(32'(control_history))) & 32'h2)) != 32'h0) != '0);
        assign decoder__end_in = ((((decoder_control(unsigned'(32'(control_history))) & 32'h4)) != 32'h0) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] tag;
        logic[31:0] _byte;
        logic[31:0] kind;
        logic[31:0] source;
        logic[31:0] encoded;
        output_valid_tmp = symbol_enable_in && pma_ok_in;
        if (symbol_enable_in) begin
            tag=unsigned'(32'((tags >> ((((TAG_DEPTH - 32'h1))*TAG_BITS))))) & 32'h7FF;
            kind=tag & 32'h7;
            _byte=unsigned'(32'(decoder__data_out)) ^ ((tag >>> 32'sh3));
            output_reg_tmp = unsigned'(32'((kind == 32'h0) ? (32'h0) : ((kind == 32'h1) ? (32'h155) : ((kind == 32'h2) ? (32'h100 | _byte) : ((kind == 32'h3) ? (32'h300 | _byte) : (unsigned'(32'((kind == 32'h4) ? ('h20E) : ((kind == 32'h5) ? ('h20F) : ((kind == 32'h6) ? ('h21F) : ('h300)))))))))));
            if ((((kind == 32'h2) || (kind == 32'h3))) && !decoder__valid_out) begin
                output_reg_tmp = 32'(32'h300 | _byte);
            end
            source=framer__tag_out;
            kind=((((source & 32'h400)) != '0)) ? ((((((source & 32'h200)) != '0)) ? ('h3) : ('h2))) : ((((source & 32'h3FF)) == 32'h155) ? ('h1) : ((((source & 32'h3FF)) == 32'h300) ? ('h7) : ((((source & 32'h3FF)) == 32'h21F) ? ('h6) : ((((source & 32'h3FF)) == 32'h20F) ? ('h5) : (((((source & 32'h200)) != '0)) ? ('h4) : ('h0))))));
            encoded=kind | ((((((source >>> 32'shB)) & 32'hFF)) <<< 'h3));
            tags_tmp = (tags << TAG_BITS) | encoded;
            if (((source & 32'h80000) != '0)) begin
                logic[64'(unsigned'(32'(TAG_BITS*TAG_DEPTH)))-1:0] previous_mask;
                previous_mask = 'h7FF;
                previous_mask = previous_mask << TAG_BITS;
                tags_tmp = (tags_tmp & $bits(previous_mask)'(~(previous_mask))) | ((64'(unsigned'(32'(TAG_BITS*TAG_DEPTH))))'('h1) << TAG_BITS);
            end
            hard_delayed_tmp = slice(unsigned'(32'(samples_in)));
            samples_delayed_tmp = samples_in;
            samples_for_metrics_tmp = samples_delayed;
            control_history_tmp = ((control_history <<< 'h7)) | 64'(unsigned'(32'(framer__control_out)));
        end
        if (!pma_ok_in) begin
            output_reg_tmp = 64'h200;
            tags_tmp = 'h0;
            control_history_tmp = 64'h0;
        end
        if (reset) begin
            output_valid_tmp = (('h0) != '0);
            hard_delayed_tmp = 64'h492;
            samples_delayed_tmp = 64'h80808080;
            samples_for_metrics_tmp = 64'h80808080;
            control_history_tmp = 64'h0;
            tags_tmp = 'h0;
            output_reg_tmp = 64'h0;
        end
    end
    endtask

    always @(posedge clk) begin
        hard_delayed_tmp = hard_delayed;
        samples_delayed_tmp = samples_delayed;
        samples_for_metrics_tmp = samples_for_metrics;
        control_history_tmp = control_history;
        tags_tmp = tags;
        output_reg_tmp = output_reg;
        output_valid_tmp = output_valid;

            _work(reset);

        hard_delayed <= hard_delayed_tmp;
        samples_delayed <= samples_delayed_tmp;
        samples_for_metrics <= samples_for_metrics_tmp;
        control_history <= control_history_tmp;
        tags <= tags_tmp;
        output_reg <= output_reg_tmp;
        output_valid <= output_valid_tmp;
    end

    assign receive_valid_out = output_valid;

    assign receive_out = output_reg;

    assign locked_out = ((sync__locked_out) != '0);

    assign remote_receiver_ok_out = ((framer__remote_receiver_ok_out) != '0);


endmodule
