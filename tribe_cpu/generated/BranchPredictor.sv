`default_nettype none

import Predef_pkg::*;
import Br_pkg::*;


module BranchPredictor #(
    parameter ENTRIES = 'h10
,   parameter COUNTER_BITS = 'h2
 )
 (
    input wire clk
,   input wire l2_clock
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
    localparam  INDEX_BITS = 64'(unsigned'(32'((ENTRIES<=64'h1) ? (32'h1) : ($clog2(unsigned'(32'(ENTRIES)))))));
    localparam  COUNTER_MAX = (('h1 <<< COUNTER_BITS)) - 32'h1;
    localparam  COUNTER_INIT = unsigned'(32'(COUNTER_MAX)) >>> 32'sh1;


    // regs and combs
    reg[ENTRIES-1:0][COUNTER_BITS-1:0] counter_reg;
    reg[ENTRIES-1:0][32-1:0] target_reg;
    reg[ENTRIES-1:0][32-1:0] tag_reg;
    reg[ENTRIES-1:0] valid_reg;
    logic[INDEX_BITS-1:0] lookup_index_comb;
    logic lookup_hit_comb;
    logic lookup_unconditional_comb;
    logic predict_taken_comb;
    logic[31:0] predict_next_comb;

    // members

    // tmp variables
    logic[ENTRIES-1:0][COUNTER_BITS-1:0] counter_reg_tmp;
    logic[ENTRIES-1:0][32-1:0] target_reg_tmp;
    logic[ENTRIES-1:0][32-1:0] tag_reg_tmp;
    logic[ENTRIES-1:0] valid_reg_tmp;


    always_comb begin : lookup_index_comb_func  // lookup_index_comb_func
        lookup_index_comb = INDEX_BITS'(64'(unsigned'(32'((lookup_pc_in >>> 32'sh1)))));
    end

    always_comb begin : lookup_unconditional_comb_func  // lookup_unconditional_comb_func
        lookup_unconditional_comb=((lookup_br_op_in == unsigned'(64'(Br_pkg::JAL))) || (lookup_br_op_in == unsigned'(64'(Br_pkg::JALR)))) || (lookup_br_op_in == unsigned'(64'(Br_pkg::JR)));
    end

    always_comb begin : lookup_hit_comb_func  // lookup_hit_comb_func
        logic[31:0] index;
        index=lookup_index_comb;
        lookup_hit_comb=((valid_reg[64'(index)]) != '0) && (tag_reg[64'(index)] == unsigned'(64'(lookup_pc_in)));
    end

    always_comb begin : predict_taken_comb_func  // predict_taken_comb_func
        logic[31:0] index;
        index=lookup_index_comb;
        predict_taken_comb=0;
        if (lookup_valid_in) begin
            if (lookup_unconditional_comb) begin
                predict_taken_comb=1;
            end
            else begin
                if (lookup_hit_comb && counter_reg[64'(index)]>=64'(32'(((((COUNTER_MAX + 32'h1)) >>> 32'sh1))))) begin
                    predict_taken_comb=1;
                end
            end
        end
    end

    always_comb begin : predict_next_comb_func  // predict_next_comb_func
        logic[31:0] index;
        index=lookup_index_comb;
        predict_next_comb=lookup_fallthrough_in;
        if (predict_taken_comb) begin
            predict_next_comb=(lookup_hit_comb) ? (unsigned'(32'(target_reg[64'(index)]))) : (lookup_target_in);
        end
    end

    task _work (input logic reset);
    begin: _work
        logic[31:0] index;
        logic[31:0] counter;
        logic[63:0] i;
        if (update_valid_in) begin
            index=64'(unsigned'(32'(((update_pc_in >>> 32'sh1))))) & ((ENTRIES - 64'h1));
            counter=counter_reg[64'(index)];
            if (update_taken_in) begin
                if (counter != unsigned'(32'(COUNTER_MAX))) begin
                    counter_reg_tmp[64'(index)] = 32'(counter + 32'h1);
                end
            end
            else begin
                if (counter != 32'h0) begin
                    counter_reg_tmp[64'(index)] = 32'(counter - 32'h1);
                end
            end
            target_reg_tmp[64'(index)] = update_target_in;
            tag_reg_tmp[64'(index)] = update_pc_in;
            valid_reg_tmp[64'(index)] = 1;
        end
        if (reset) begin
            for (i=64'h0;i < ENTRIES;i=i+1) begin
                counter_reg_tmp[i] = COUNTER_INIT;
                target_reg_tmp[i] = 32'h0;
                tag_reg_tmp[i] = 32'h0;
                valid_reg_tmp[i] = 0;
            end
        end
    end
    endtask

    generate  // _assign
    endgenerate

    task _work_l2_clock (input logic reset);
    begin: _work_l2_clock
    end
    endtask

    always_ff @(posedge clk) begin
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

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

    end

    assign predict_taken_out = predict_taken_comb;

    assign predict_next_out = predict_next_comb;


endmodule
