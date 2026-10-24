`default_nettype none

import Predef_pkg::*;


module ReceiveDSP (
    input wire clk
,   input wire reset
,   input wire[32-1:0] samples_in
,   input wire[12-1:0] local_in
,   input wire[12-1:0] desired_in
,   input wire[16-1:0] tag_in
,   input wire clipped_in
,   input wire train_in
,   input wire valid_in
,   input wire restart_in
,   output wire[32-1:0] samples_out
,   output wire[16-1:0] tag_out
,   output wire valid_out
,   output wire fault_out
,   output wire clipped_out
,   output wire training_busy_out
);
    localparam  OUTPUT_LATENCY = 'h6;
    localparam  UPDATE_LATENCY = 'hA;


    // regs and combs
    reg[16-1:0] tags[7];
    reg clipped[7];
    reg[10-1:0] training;
    reg fault;
    logic all_valid_comb;
    logic child_fault_comb;
    logic inconsistent_valid_comb;
    logic illegal_input_comb;
    logic unavailable_comb;
    logic output_at_rail_comb;

    // members
    genvar __i;
    wire[8-1:0] lanes__sample_in[4];
    wire[12-1:0] lanes__local_in[4];
    wire[3-1:0] lanes__desired_in[4];
    wire lanes__train_in[4];
    wire lanes__advance_in[4];
    wire[8-1:0] lanes__sample_out[4];
    wire lanes__valid_out[4];
    wire lanes__fault_out[4];
    generate
    for (__i=0; __i < 4; __i = __i + 1) begin
        ReceiveFilter          lanes (
            .clk(clk)
        ,           .reset(reset || unavailable_comb)
        ,           .sample_in(lanes__sample_in[__i])
        ,           .local_in(lanes__local_in[__i])
        ,           .desired_in(lanes__desired_in[__i])
        ,           .train_in(lanes__train_in[__i])
        ,           .advance_in(lanes__advance_in[__i])
        ,           .sample_out(lanes__sample_out[__i])
        ,           .valid_out(lanes__valid_out[__i])
        ,           .fault_out(lanes__fault_out[__i])
        );
    end
    endgenerate

    // tmp variables
    logic[16-1:0] tags_tmp[7];
    logic clipped_tmp[7];
    logic[10-1:0] training_tmp;
    logic fault_tmp;


    always_comb begin : all_valid_comb_func  // all_valid_comb_func
        all_valid_comb=((lanes__valid_out['h0] && lanes__valid_out['h1]) && lanes__valid_out['h2]) && lanes__valid_out['h3];
    end

    always_comb begin : child_fault_comb_func  // child_fault_comb_func
        child_fault_comb=((lanes__fault_out['h0] || lanes__fault_out['h1]) || lanes__fault_out['h2]) || lanes__fault_out['h3];
    end

    always_comb begin : inconsistent_valid_comb_func  // inconsistent_valid_comb_func
        inconsistent_valid_comb=((signed'(32'(lanes__valid_out['h0])) != signed'(32'(lanes__valid_out['h1]))) || (signed'(32'(lanes__valid_out['h0])) != signed'(32'(lanes__valid_out['h2])))) || (signed'(32'(lanes__valid_out['h0])) != signed'(32'(lanes__valid_out['h3])));
    end

    always_comb begin : illegal_input_comb_func  // illegal_input_comb_func
        logic bad;
        bad = 0;
        for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
            bad=bad || (((((unsigned'(32'(local_in)) >>> ((32'h3*lane)))) & 32'h7)) > 32'h4);
            if (train_in) begin
                bad=bad || (((((unsigned'(32'(desired_in)) >>> ((32'h3*lane)))) & 32'h7)) > 32'h4);
            end
        end
        illegal_input_comb=valid_in && bad;
    end

    always_comb begin : unavailable_comb_func  // unavailable_comb_func
        unavailable_comb=(((restart_in || ((fault) != '0)) || child_fault_comb) || inconsistent_valid_comb) || illegal_input_comb;
    end

    always_comb begin : output_at_rail_comb_func  // output_at_rail_comb_func
        logic rail;
        rail = 0;
        for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
            rail=(rail || (lanes__sample_out[lane] == 64'h0)) || (lanes__sample_out[lane] == 64'hFF);
        end
        output_at_rail_comb=all_valid_comb && rail;
    end

    generate  // _assign
        genvar gi;
        for (gi = 32'h0;gi < 32'h4;gi=gi+1) begin
            assign lanes__sample_in[gi] = ((samples_in >>> ((32'h8*gi)))) & 64'hFF;
            assign lanes__local_in[gi] = local_in;
            assign lanes__desired_in[gi] = ((desired_in >>> ((32'h3*gi)))) & 64'h7;
            assign lanes__train_in[gi] = ((train_in) != '0);
            assign lanes__advance_in[gi] = ((valid_in && !unavailable_comb) != '0);
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic stop;
        stop = unavailable_comb;
        tags_tmp['h0] = tag_in;
        clipped_tmp['h0] = valid_in && clipped_in;
        for (logic[31:0] stage = 32'h1;stage<=unsigned'(32'(OUTPUT_LATENCY));stage=stage+1) begin
            tags_tmp[stage] = tags[stage - 32'h1];
            clipped_tmp[stage] = clipped[stage - 32'h1];
        end
        training_tmp = ((training <<< 'h1)) | 64'(32'((valid_in && train_in)));
        if (stop) begin
            fault_tmp = (('h1) != '0);
            training_tmp = 64'h0;
        end
        for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
        end
        if (reset || restart_in) begin
            fault_tmp = (('h0) != '0);
            training_tmp = 64'h0;
            for (logic[31:0] stage = 32'h0;stage<=unsigned'(32'(OUTPUT_LATENCY));stage=stage+1) begin
                tags_tmp[stage] = 64'h0;
                clipped_tmp[stage] = (('h0) != '0);
            end
        end
    end
    endtask

    always @(posedge clk) begin
        tags_tmp = tags;
        clipped_tmp = clipped;
        training_tmp = training;
        fault_tmp = fault;

            _work(reset);

        tags <= tags_tmp;
        clipped <= clipped_tmp;
        training <= training_tmp;
        fault <= fault_tmp;
    end

    assign samples_out = ((unsigned'(32'(lanes__sample_out['h0])) | ((unsigned'(32'(lanes__sample_out['h1])) <<< 'h8))) | ((unsigned'(32'(lanes__sample_out['h2])) <<< 'h10))) | ((unsigned'(32'(lanes__sample_out['h3])) <<< 'h18));

    assign tag_out = tags[OUTPUT_LATENCY];

    assign valid_out = ((all_valid_comb && !unavailable_comb) != '0);

    assign fault_out = (((((fault) != '0) || child_fault_comb) || inconsistent_valid_comb) != '0);

    assign clipped_out = ((((clipped[OUTPUT_LATENCY]) != '0) || output_at_rail_comb) != '0);

    assign training_busy_out = ((!unavailable_comb && (((training != 64'h0) || ((valid_in && train_in))))) != '0);


endmodule
