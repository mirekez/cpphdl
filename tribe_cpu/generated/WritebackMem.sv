`default_nettype none

import Predef_pkg::*;
import State_pkg::*;
import Wb_pkg::*;
import Amo_pkg::*;


module WritebackMem (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire State state_in
,   input wire[31:0] alu_result_in
,   input wire split_load_in
,   input wire[31:0] split_load_low_addr_in
,   input wire[31:0] split_load_high_addr_in
,   input wire dcache_read_valid_in
,   input wire[31:0] dcache_read_addr_in
,   input wire[31:0] dcache_read_data_in
,   input wire dcache_write_valid_in
,   input wire[31:0] dcache_write_addr_in
,   input wire[31:0] dcache_write_data_in
,   input wire[7:0] dcache_write_mask_in
,   input wire store_forward_enable_in
,   input wire retire_in
,   output wire load_ready_out
,   output wire[31:0] load_raw_out
,   output wire[31:0] load_result_out
,   output wire[31:0] wb_mem_data_out
,   output wire[31:0] wb_mem_data_hi_out
,   output wire debug_load_data_valid_out
,   output wire[31:0] debug_load_addr_out
,   output wire debug_split_low_valid_out
,   output wire debug_split_high_valid_out
,   output wire debug_held_load_valid_out
);


    // regs and combs
    reg[32-1:0] load_data_reg;
    reg[32-1:0] load_addr_reg;
    reg load_data_valid_reg;
    reg[32-1:0] load_raw_result_reg;
    reg load_result_valid_reg;
    reg[32-1:0] split_load_low_reg;
    reg[32-1:0] split_load_high_reg;
    reg split_load_low_valid_reg;
    reg split_load_high_valid_reg;
    reg[2-1:0][32-1:0] store_forward_addr_reg;
    reg[2-1:0][32-1:0] store_forward_data_reg;
    reg[2-1:0][8-1:0] store_forward_mask_reg;
    reg[2-1:0] store_forward_valid_reg;
    reg[4-1:0][32-1:0] load_byte_addr_reg;
    reg[2-1:0][4-1:0][32-1:0] store_forward_byte_addr_reg;
    reg[2-1:0][4-1:0][8-1:0] store_forward_byte_data_reg;
    reg[2-1:0][4-1:0] store_forward_byte_valid_reg;
    logic debug_load_data_valid_comb;
    logic[31:0] debug_load_addr_comb;
    logic debug_split_low_valid_comb;
    logic debug_split_high_valid_comb;
    logic held_load_valid_comb;
    logic held_load_result_valid_comb;
    logic split_load_current_low_valid_comb;
    logic split_load_current_high_valid_comb;
    logic split_load_low_ready_comb;
    logic split_load_high_ready_comb;
    logic non_split_current_valid_comb;
    logic[31:0] split_load_low_data_comb;
    logic[31:0] split_load_high_data_comb;
    logic load_ready_comb;
    logic[31:0] load_candidate_raw_comb;
    logic[31:0] load_raw_comb;
    logic[31:0] wb_mem_data_comb;
    logic[31:0] wb_mem_data_hi_comb;
    logic[31:0] load_result_comb;

    // members

    // tmp variables
    logic[32-1:0] load_data_reg_tmp;
    logic[32-1:0] load_addr_reg_tmp;
    logic load_data_valid_reg_tmp;
    logic[32-1:0] load_raw_result_reg_tmp;
    logic load_result_valid_reg_tmp;
    logic[32-1:0] split_load_low_reg_tmp;
    logic[32-1:0] split_load_high_reg_tmp;
    logic split_load_low_valid_reg_tmp;
    logic split_load_high_valid_reg_tmp;
    logic[2-1:0][32-1:0] store_forward_addr_reg_tmp;
    logic[2-1:0][32-1:0] store_forward_data_reg_tmp;
    logic[2-1:0][8-1:0] store_forward_mask_reg_tmp;
    logic[2-1:0] store_forward_valid_reg_tmp;
    logic[4-1:0][32-1:0] load_byte_addr_reg_tmp;
    logic[2-1:0][4-1:0][32-1:0] store_forward_byte_addr_reg_tmp;
    logic[2-1:0][4-1:0][8-1:0] store_forward_byte_data_reg_tmp;
    logic[2-1:0][4-1:0] store_forward_byte_valid_reg_tmp;


    always_comb begin : held_load_result_valid_comb_func  // held_load_result_valid_comb_func
        held_load_result_valid_comb=((load_result_valid_reg) != '0);
    end

    always_comb begin : load_ready_comb_func  // load_ready_comb_func
        load_ready_comb=(((state_in.valid) != '0) && (signed'(32'(state_in.wb_op)) == signed'(32'(Wb_pkg::MEM)))) && held_load_result_valid_comb;
    end

    always_comb begin : load_raw_comb_func  // load_raw_comb_func
        load_raw_comb=load_raw_result_reg;
    end

    always_comb begin : load_result_comb_func  // load_result_comb_func
        logic[31:0] raw;
        raw=load_raw_comb;
        load_result_comb=32'h0;
        case (signed'(32'(state_in.funct3)))
        'h0: begin
            load_result_comb=signed'(32'(signed'(8'(raw))));
        end
        'h1: begin
            load_result_comb=signed'(32'(signed'(16'(raw))));
        end
        'h2: begin
            load_result_comb=raw;
        end
        'h4: begin
            load_result_comb=8'(raw);
        end
        'h5: begin
            load_result_comb=16'(raw);
        end
        endcase
    end

    always_comb begin : split_load_low_data_comb_func  // split_load_low_data_comb_func
        split_load_low_data_comb=(((split_load_low_valid_reg) != '0)) ? (unsigned'(32'(split_load_low_reg))) : (32'h0);
    end

    always_comb begin : held_load_valid_comb_func  // held_load_valid_comb_func
        held_load_valid_comb=((load_data_valid_reg) != '0);
    end

    always_comb begin : wb_mem_data_comb_func  // wb_mem_data_comb_func
        if (split_load_in) begin
            wb_mem_data_comb=split_load_low_data_comb;
        end
        else begin
            wb_mem_data_comb=(held_load_valid_comb) ? (unsigned'(32'(load_data_reg))) : (32'h0);
        end
    end

    always_comb begin : split_load_high_data_comb_func  // split_load_high_data_comb_func
        split_load_high_data_comb=(((split_load_high_valid_reg) != '0)) ? (unsigned'(32'(split_load_high_reg))) : (32'h0);
    end

    always_comb begin : wb_mem_data_hi_comb_func  // wb_mem_data_hi_comb_func
        wb_mem_data_hi_comb=(split_load_in) ? (split_load_high_data_comb) : (32'h0);
    end

    always_comb begin : debug_load_data_valid_comb_func  // debug_load_data_valid_comb_func
        debug_load_data_valid_comb=((load_data_valid_reg) != '0);
    end

    always_comb begin : debug_load_addr_comb_func  // debug_load_addr_comb_func
        debug_load_addr_comb=load_addr_reg;
    end

    always_comb begin : debug_split_low_valid_comb_func  // debug_split_low_valid_comb_func
        debug_split_low_valid_comb=((split_load_low_valid_reg) != '0);
    end

    always_comb begin : debug_split_high_valid_comb_func  // debug_split_high_valid_comb_func
        debug_split_high_valid_comb=((split_load_high_valid_reg) != '0);
    end

    always_comb begin : split_load_current_low_valid_comb_func  // split_load_current_low_valid_comb_func
        split_load_current_low_valid_comb=dcache_read_valid_in && (dcache_read_addr_in == split_load_low_addr_in);
    end

    always_comb begin : split_load_current_high_valid_comb_func  // split_load_current_high_valid_comb_func
        split_load_current_high_valid_comb=dcache_read_valid_in && (dcache_read_addr_in == split_load_high_addr_in);
    end

    always_comb begin : split_load_low_ready_comb_func  // split_load_low_ready_comb_func
        split_load_low_ready_comb=((split_load_low_valid_reg) != '0);
    end

    always_comb begin : split_load_high_ready_comb_func  // split_load_high_ready_comb_func
        split_load_high_ready_comb=((split_load_high_valid_reg) != '0);
    end

    always_comb begin : non_split_current_valid_comb_func  // non_split_current_valid_comb_func
        non_split_current_valid_comb=dcache_read_valid_in && (dcache_read_addr_in == alu_result_in);
    end

    always_comb begin : load_candidate_raw_comb_func  // load_candidate_raw_comb_func
        logic[31:0] raw;
        logic[32-1:0] result;
        logic[31:0] shift;
        logic[31:0] lane;
        logic[31:0] store_slot_order;
        logic[31:0] store_slot;
        logic[31:0] store_lane;
        logic[31:0] forwarded_byte;
        logic allow_store_forward;
        if (split_load_in) begin
            shift=((alu_result_in & 'h3))*'h8;
            raw=((split_load_low_data_comb >>> shift)) | ((split_load_high_data_comb <<< (('h20 - shift))));
        end
        else begin
            raw=(held_load_valid_comb) ? (unsigned'(32'(load_data_reg))) : (32'h0);
        end
        result = raw;
        allow_store_forward=store_forward_enable_in && (signed'(32'(state_in.amo_op)) == signed'(32'(Amo_pkg::AMONONE)));
        for (lane=32'h0;lane < 32'h4;lane=lane+1) begin
            forwarded_byte=((raw >>> ((lane*'h8)))) & 'hFF;
            for (store_slot_order=32'h0;store_slot_order < 32'h2;store_slot_order=store_slot_order+1) begin
                store_slot='h1 - store_slot_order;
                for (store_lane=32'h0;store_lane < 32'h4;store_lane=store_lane+1) begin
                    if ((allow_store_forward && ((store_forward_byte_valid_reg[64'(store_slot)][64'(store_lane)]) != '0)) && (load_byte_addr_reg[64'(lane)] == store_forward_byte_addr_reg[64'(store_slot)][64'(store_lane)])) begin
                        forwarded_byte=store_forward_byte_data_reg[64'(store_slot)][64'(store_lane)];
                    end
                end
            end
            result[lane*32'h8 +:8] = 64'(forwarded_byte);
        end
        load_candidate_raw_comb=result;
    end

    task _work (input logic reset);
    begin: _work
        logic[31:0] lane;
        if (((state_in.valid) != '0) && (signed'(32'(state_in.wb_op)) == signed'(32'(Wb_pkg::MEM)))) begin
            for (lane=32'h0;lane < 32'h4;lane=lane+1) begin
                load_byte_addr_reg_tmp[64'(lane)] = 32'(alu_result_in + lane);
            end
        end
        if (dcache_write_valid_in && ((dcache_write_mask_in) != '0)) begin
            logic same_head; same_head = ((((store_forward_valid_reg[64'h0]) != '0) && (unsigned'(32'(store_forward_addr_reg[64'h0])) == dcache_write_addr_in)) && (unsigned'(32'(store_forward_data_reg[64'h0])) == dcache_write_data_in)) && (signed'(32'(unsigned'(8'(store_forward_mask_reg[64'h0])))) == signed'(32'(dcache_write_mask_in)));
            if (!same_head) begin
                store_forward_addr_reg_tmp[64'h1] = store_forward_addr_reg[64'h0];
                store_forward_data_reg_tmp[64'h1] = store_forward_data_reg[64'h0];
                store_forward_mask_reg_tmp[64'h1] = store_forward_mask_reg[64'h0];
                store_forward_valid_reg_tmp[64'h1] = store_forward_valid_reg[64'h0];
                store_forward_byte_addr_reg_tmp[64'h1] = store_forward_byte_addr_reg[64'h0];
                store_forward_byte_data_reg_tmp[64'h1] = store_forward_byte_data_reg[64'h0];
                store_forward_byte_valid_reg_tmp[64'h1] = store_forward_byte_valid_reg[64'h0];
            end
            store_forward_addr_reg_tmp[64'h0] = dcache_write_addr_in;
            store_forward_data_reg_tmp[64'h0] = dcache_write_data_in;
            store_forward_mask_reg_tmp[64'h0] = dcache_write_mask_in;
            store_forward_valid_reg_tmp[64'h0] = 1;
            for (lane=32'h0;lane < 32'h4;lane=lane+1) begin
                store_forward_byte_addr_reg_tmp[64'h0][64'(lane)] = 32'(dcache_write_addr_in + lane);
                store_forward_byte_data_reg_tmp[64'h0][64'(lane)] = ((dcache_write_data_in >>> ((lane*'h8)))) & 'hFF;
                store_forward_byte_valid_reg_tmp[64'h0][64'(lane)] = ((unsigned'(32'(dcache_write_mask_in)) & (('h1 <<< lane)))) != 32'h0;
            end
        end
        else begin
            store_forward_valid_reg_tmp[64'h0] = 0;
            store_forward_valid_reg_tmp[64'h1] = 0;
            for (lane=32'h0;lane < 32'h4;lane=lane+1) begin
                store_forward_byte_valid_reg_tmp[64'h0][64'(lane)] = 0;
                store_forward_byte_valid_reg_tmp[64'h1][64'(lane)] = 0;
            end
        end
        if (split_load_in) begin
            if (split_load_current_low_valid_comb) begin
                split_load_low_reg_tmp = dcache_read_data_in;
                split_load_low_valid_reg_tmp = 1;
            end
            if (split_load_current_high_valid_comb) begin
                split_load_high_reg_tmp = dcache_read_data_in;
                split_load_high_valid_reg_tmp = 1;
            end
        end
        else begin
            if ((((state_in.valid) != '0) && (signed'(32'(state_in.wb_op)) == signed'(32'(Wb_pkg::MEM)))) && non_split_current_valid_comb) begin
                load_data_reg_tmp = dcache_read_data_in;
                load_addr_reg_tmp = dcache_read_addr_in;
                load_data_valid_reg_tmp = 1;
            end
        end
        if (((((state_in.valid) != '0) && (signed'(32'(state_in.wb_op)) == signed'(32'(Wb_pkg::MEM)))) && !held_load_result_valid_comb) && ((split_load_in) ? (((((split_load_low_valid_reg) != '0) && ((split_load_high_valid_reg) != '0)))) : (held_load_valid_comb))) begin
            load_raw_result_reg_tmp = load_candidate_raw_comb;
            load_result_valid_reg_tmp = 1;
        end
        if ((retire_in || !((state_in.valid) != '0)) || (signed'(32'(state_in.wb_op)) != signed'(32'(Wb_pkg::MEM)))) begin
            load_data_valid_reg_tmp = 0;
            load_result_valid_reg_tmp = 0;
            split_load_low_valid_reg_tmp = 0;
            split_load_high_valid_reg_tmp = 0;
        end
        if (reset) begin
            load_data_reg_tmp = '0;
            load_addr_reg_tmp = '0;
            load_data_valid_reg_tmp = '0;
            load_raw_result_reg_tmp = '0;
            load_result_valid_reg_tmp = '0;
            split_load_low_reg_tmp = '0;
            split_load_high_reg_tmp = '0;
            split_load_low_valid_reg_tmp = '0;
            split_load_high_valid_reg_tmp = '0;
            store_forward_addr_reg_tmp = '0;
            store_forward_data_reg_tmp = '0;
            store_forward_mask_reg_tmp = '0;
            store_forward_valid_reg_tmp = '0;
            load_byte_addr_reg_tmp = '0;
            store_forward_byte_addr_reg_tmp = '0;
            store_forward_byte_data_reg_tmp = '0;
            store_forward_byte_valid_reg_tmp = '0;
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
        load_data_reg_tmp = load_data_reg;
        load_addr_reg_tmp = load_addr_reg;
        load_data_valid_reg_tmp = load_data_valid_reg;
        load_raw_result_reg_tmp = load_raw_result_reg;
        load_result_valid_reg_tmp = load_result_valid_reg;
        split_load_low_reg_tmp = split_load_low_reg;
        split_load_high_reg_tmp = split_load_high_reg;
        split_load_low_valid_reg_tmp = split_load_low_valid_reg;
        split_load_high_valid_reg_tmp = split_load_high_valid_reg;
        store_forward_addr_reg_tmp = store_forward_addr_reg;
        store_forward_data_reg_tmp = store_forward_data_reg;
        store_forward_mask_reg_tmp = store_forward_mask_reg;
        store_forward_valid_reg_tmp = store_forward_valid_reg;
        load_byte_addr_reg_tmp = load_byte_addr_reg;
        store_forward_byte_addr_reg_tmp = store_forward_byte_addr_reg;
        store_forward_byte_data_reg_tmp = store_forward_byte_data_reg;
        store_forward_byte_valid_reg_tmp = store_forward_byte_valid_reg;

            _work(reset);

        load_data_reg <= load_data_reg_tmp;
        load_addr_reg <= load_addr_reg_tmp;
        load_data_valid_reg <= load_data_valid_reg_tmp;
        load_raw_result_reg <= load_raw_result_reg_tmp;
        load_result_valid_reg <= load_result_valid_reg_tmp;
        split_load_low_reg <= split_load_low_reg_tmp;
        split_load_high_reg <= split_load_high_reg_tmp;
        split_load_low_valid_reg <= split_load_low_valid_reg_tmp;
        split_load_high_valid_reg <= split_load_high_valid_reg_tmp;
        store_forward_addr_reg <= store_forward_addr_reg_tmp;
        store_forward_data_reg <= store_forward_data_reg_tmp;
        store_forward_mask_reg <= store_forward_mask_reg_tmp;
        store_forward_valid_reg <= store_forward_valid_reg_tmp;
        load_byte_addr_reg <= load_byte_addr_reg_tmp;
        store_forward_byte_addr_reg <= store_forward_byte_addr_reg_tmp;
        store_forward_byte_data_reg <= store_forward_byte_data_reg_tmp;
        store_forward_byte_valid_reg <= store_forward_byte_valid_reg_tmp;
    end

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

    end

    assign load_ready_out = load_ready_comb;

    assign load_raw_out = load_raw_comb;

    assign load_result_out = load_result_comb;

    assign wb_mem_data_out = wb_mem_data_comb;

    assign wb_mem_data_hi_out = wb_mem_data_hi_comb;

    assign debug_load_data_valid_out = debug_load_data_valid_comb;

    assign debug_load_addr_out = debug_load_addr_comb;

    assign debug_split_low_valid_out = debug_split_low_valid_comb;

    assign debug_split_high_valid_out = debug_split_high_valid_comb;

    assign debug_held_load_valid_out = held_load_valid_comb;


endmodule
