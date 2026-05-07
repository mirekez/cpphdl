`default_nettype none

import Predef_pkg::*;
import Br_pkg::*;


module BranchPredictor #(
    parameter ENTRIES
,   parameter COUNTER_BITS
 )
 (
    input wire clk
,   input wire reset
,   input wire lookup_valid_in
,   input wire[31:0] lookup_pc_in
,   input wire[31:0] lookup_target_in
,   input wire[31:0] lookup_fallthrough_in
,   input wire[4-1:0] lookup_br_op_in
,   output wire predict_taken_out
,   output wire[31:0] predict_next_out
,   input wire update_valid_in
,   input wire[31:0] update_pc_in
,   input wire update_taken_in
,   input wire[31:0] update_target_in
);
    parameter  INDEX_BITS = (ENTRIES<='h1) ? ('h1) : ($clog2(ENTRIES));
    parameter  COUNTER_MAX = (('h1 <<< COUNTER_BITS)) - 'h1;
    parameter  COUNTER_INIT = COUNTER_MAX >>> 'h1;


    // regs and combs
    reg[ENTRIES-1:0][COUNTER_BITS-1:0] counter_reg;
    reg[ENTRIES-1:0][32-1:0] target_reg;
    reg[ENTRIES-1:0][32-1:0] tag_reg;
    reg[ENTRIES-1:0] valid_reg;
    logic[INDEX_BITS-1:0] lookup_index_comb;
;
    logic lookup_hit_comb;
;
    logic lookup_unconditional_comb;
;
    logic predict_taken_comb;
;
    logic[31:0] predict_next_comb;
;

    // members
    genvar gi, gj, gk;

    // tmp variables
    logic[ENTRIES-1:0][COUNTER_BITS-1:0] counter_reg_tmp;
    logic[ENTRIES-1:0][32-1:0] target_reg_tmp;
    logic[ENTRIES-1:0][32-1:0] tag_reg_tmp;
    logic[ENTRIES-1:0] valid_reg_tmp;


    task _work (input logic reset);
    begin: _work
        logic[31:0] index;
        logic[31:0] counter;
        logic[63:0] i;
        if (update_valid_in) begin
            index=((update_pc_in >>> 'h1)) & ((ENTRIES - 'h1));
            counter=unsigned'(32'(counter_reg[index]));
            if (update_taken_in) begin
                if (counter != COUNTER_MAX) begin
                    counter_reg_tmp[index] = counter + 'h1;
                end
            end
            else begin
                if (counter != 'h0) begin
                    counter_reg_tmp[index] = counter - 'h1;
                end
            end
            target_reg_tmp[index] = update_target_in;
            tag_reg_tmp[index] = update_pc_in;
            valid_reg_tmp[index] = 1;
        end
        if (reset) begin
            for (i='h0;i < ENTRIES;i=i+1) begin
                counter_reg_tmp[i] = COUNTER_INIT;
                target_reg_tmp[i] = 'h0;
                tag_reg_tmp[i] = 'h0;
                valid_reg_tmp[i] = 0;
            end
        end
    end
    endtask

    generate  // _assign
    endgenerate

    always @(*) begin  // lookup_index_comb_func
        lookup_index_comb=(lookup_pc_in >>> 'h1);
    end

    always @(*) begin  // lookup_unconditional_comb_func
        lookup_unconditional_comb=((lookup_br_op_in == Br_pkg::JAL) || (lookup_br_op_in == Br_pkg::JALR)) || (lookup_br_op_in == Br_pkg::JR);
    end

    always @(*) begin  // lookup_hit_comb_func
        logic[31:0] index;
        index=unsigned'(32'(lookup_index_comb));
        lookup_hit_comb=valid_reg[index] && (tag_reg[index] == lookup_pc_in);
    end

    always @(*) begin  // predict_taken_comb_func
        logic[31:0] index;
        index=unsigned'(32'(lookup_index_comb));
        predict_taken_comb=0;
        if (lookup_valid_in) begin
            if (lookup_unconditional_comb) begin
                predict_taken_comb=1;
            end
            else begin
                if (lookup_hit_comb && counter_reg[index]>=((((COUNTER_MAX + 'h1)) >>> 'h1))) begin
                    predict_taken_comb=1;
                end
            end
        end
    end

    always @(*) begin  // predict_next_comb_func
        logic[31:0] index;
        index=unsigned'(32'(lookup_index_comb));
        predict_next_comb=lookup_fallthrough_in;
        if (predict_taken_comb) begin
            predict_next_comb=(lookup_hit_comb) ? (unsigned'(32'(target_reg[index]))) : (lookup_target_in);
        end
    end

    always @(posedge clk) begin
        counter_reg_tmp = counter_reg;
        target_reg_tmp = target_reg;
        tag_reg_tmp = tag_reg;
        valid_reg_tmp = valid_reg;

        _work(reset);

        counter_reg <= counter_reg_tmp;
        target_reg <= target_reg_tmp;
        tag_reg <= tag_reg_tmp;
        valid_reg <= valid_reg_tmp;
    end

    assign predict_taken_out = predict_taken_comb;

    assign predict_next_out = predict_next_comb;


endmodule
