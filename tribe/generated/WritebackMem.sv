`default_nettype none

import Predef_pkg::*;
import State_pkg::*;
import Wb_pkg::*;


module WritebackMem (
    input wire clk
,   input wire reset
,   input State state_in
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
,   input wire hold_in
,   output wire load_ready_out
,   output wire[31:0] load_raw_out
,   output wire[31:0] load_result_out
,   output wire[31:0] wb_mem_data_out
,   output wire[31:0] wb_mem_data_hi_out
);


    // regs and combs
    reg[32-1:0] load_data_reg;
    reg load_data_valid_reg;
    reg[32-1:0] split_load_low_reg;
    reg[32-1:0] split_load_high_reg;
    reg split_load_low_valid_reg;
    reg split_load_high_valid_reg;
    reg[2-1:0][32-1:0] store_forward_addr_reg;
    reg[2-1:0][32-1:0] store_forward_data_reg;
    reg[2-1:0][8-1:0] store_forward_mask_reg;
    reg[2-1:0] store_forward_valid_reg;
    logic split_load_current_low_valid_comb;
;
    logic split_load_current_high_valid_comb;
;
    logic split_load_low_ready_comb;
;
    logic split_load_high_ready_comb;
;
    logic[31:0] split_load_low_data_comb;
;
    logic[31:0] split_load_high_data_comb;
;
    logic load_ready_comb;
;
    logic[31:0] load_raw_comb;
;
    logic[31:0] wb_mem_data_comb;
;
    logic[31:0] wb_mem_data_hi_comb;
;
    logic[31:0] load_result_comb;
;

    // members
    genvar gi, gj, gk;

    // tmp variables
    logic[32-1:0] load_data_reg_tmp;
    logic load_data_valid_reg_tmp;
    logic[32-1:0] split_load_low_reg_tmp;
    logic[32-1:0] split_load_high_reg_tmp;
    logic split_load_low_valid_reg_tmp;
    logic split_load_high_valid_reg_tmp;
    logic[2-1:0][32-1:0] store_forward_addr_reg_tmp;
    logic[2-1:0][32-1:0] store_forward_data_reg_tmp;
    logic[2-1:0][8-1:0] store_forward_mask_reg_tmp;
    logic[2-1:0] store_forward_valid_reg_tmp;


    always_comb begin : split_load_current_low_valid_comb_func  // split_load_current_low_valid_comb_func
        split_load_current_low_valid_comb=dcache_read_valid_in && (dcache_read_addr_in == split_load_low_addr_in);
        disable split_load_current_low_valid_comb_func;
    end

    always_comb begin : split_load_low_ready_comb_func  // split_load_low_ready_comb_func
        split_load_low_ready_comb=split_load_low_valid_reg || split_load_current_low_valid_comb;
        disable split_load_low_ready_comb_func;
    end

    always_comb begin : split_load_current_high_valid_comb_func  // split_load_current_high_valid_comb_func
        split_load_current_high_valid_comb=dcache_read_valid_in && (dcache_read_addr_in == split_load_high_addr_in);
        disable split_load_current_high_valid_comb_func;
    end

    always_comb begin : split_load_high_ready_comb_func  // split_load_high_ready_comb_func
        split_load_high_ready_comb=split_load_high_valid_reg || split_load_current_high_valid_comb;
        disable split_load_high_ready_comb_func;
    end

    always_comb begin : load_ready_comb_func  // load_ready_comb_func
        if (split_load_in) begin
            load_ready_comb=split_load_low_ready_comb && split_load_high_ready_comb;
        end
        else begin
            load_ready_comb=(state_in.valid && (state_in.wb_op == Wb_pkg::MEM)) && ((load_data_valid_reg || ((dcache_read_valid_in && (dcache_read_addr_in == alu_result_in)))));
        end
        disable load_ready_comb_func;
    end

    always_comb begin : split_load_low_data_comb_func  // split_load_low_data_comb_func
        split_load_low_data_comb=(split_load_low_valid_reg) ? (unsigned'(32'(split_load_low_reg))) : (((split_load_current_low_valid_comb) ? (dcache_read_data_in) : (unsigned'(32'('h0)))));
        disable split_load_low_data_comb_func;
    end

    always_comb begin : split_load_high_data_comb_func  // split_load_high_data_comb_func
        split_load_high_data_comb=(split_load_high_valid_reg) ? (unsigned'(32'(split_load_high_reg))) : (((split_load_current_high_valid_comb) ? (dcache_read_data_in) : (unsigned'(32'('h0)))));
        disable split_load_high_data_comb_func;
    end

    always_comb begin : load_raw_comb_func  // load_raw_comb_func
        logic[31:0] raw;
        logic[31:0] result;
        logic[31:0] load_addr;
        logic[31:0] byte_addr;
        logic[31:0] store_addr;
        logic[31:0] store_data;
        logic[31:0] store_byte;
        logic[31:0] diff;
        logic[7:0] store_mask;
        logic[31:0] shift;
        if (split_load_in) begin
            shift=((alu_result_in & 'h3))*'h8;
            raw=((split_load_low_data_comb >>> shift)) | ((split_load_high_data_comb <<< (('h20 - shift))));
        end
        else begin
            raw=(load_data_valid_reg) ? (unsigned'(32'(load_data_reg))) : (dcache_read_data_in);
        end
        result=raw;
        load_addr=alu_result_in;
        if (store_forward_valid_reg['h1]) begin
            store_addr=store_forward_addr_reg['h1];
            store_data=store_forward_data_reg['h1];
            store_mask=store_forward_mask_reg['h1];
            byte_addr=load_addr;
            diff=byte_addr - store_addr;
            if ((byte_addr>=store_addr && (diff < 'h4)) && ((store_mask & (('h1 <<< diff))))) begin
                store_byte=((store_data >>> ((diff*'h8)))) & 'hFF;
                result=((result & ~'hFF)) | store_byte;
            end
            byte_addr=load_addr + 'h1;
            diff=byte_addr - store_addr;
            if ((byte_addr>=store_addr && (diff < 'h4)) && ((store_mask & (('h1 <<< diff))))) begin
                store_byte=((store_data >>> ((diff*'h8)))) & 'hFF;
                result=((result & ~'hFF00)) | ((store_byte <<< 'h8));
            end
            byte_addr=load_addr + 'h2;
            diff=byte_addr - store_addr;
            if ((byte_addr>=store_addr && (diff < 'h4)) && ((store_mask & (('h1 <<< diff))))) begin
                store_byte=((store_data >>> ((diff*'h8)))) & 'hFF;
                result=((result & ~'hFF0000)) | ((store_byte <<< 'h10));
            end
            byte_addr=load_addr + 'h3;
            diff=byte_addr - store_addr;
            if ((byte_addr>=store_addr && (diff < 'h4)) && ((store_mask & (('h1 <<< diff))))) begin
                store_byte=((store_data >>> ((diff*'h8)))) & 'hFF;
                result=((result & ~'hFF000000)) | ((store_byte <<< 'h18));
            end
        end
        if (store_forward_valid_reg['h0]) begin
            store_addr=store_forward_addr_reg['h0];
            store_data=store_forward_data_reg['h0];
            store_mask=store_forward_mask_reg['h0];
            byte_addr=load_addr;
            diff=byte_addr - store_addr;
            if ((byte_addr>=store_addr && (diff < 'h4)) && ((store_mask & (('h1 <<< diff))))) begin
                store_byte=((store_data >>> ((diff*'h8)))) & 'hFF;
                result=((result & ~'hFF)) | store_byte;
            end
            byte_addr=load_addr + 'h1;
            diff=byte_addr - store_addr;
            if ((byte_addr>=store_addr && (diff < 'h4)) && ((store_mask & (('h1 <<< diff))))) begin
                store_byte=((store_data >>> ((diff*'h8)))) & 'hFF;
                result=((result & ~'hFF00)) | ((store_byte <<< 'h8));
            end
            byte_addr=load_addr + 'h2;
            diff=byte_addr - store_addr;
            if ((byte_addr>=store_addr && (diff < 'h4)) && ((store_mask & (('h1 <<< diff))))) begin
                store_byte=((store_data >>> ((diff*'h8)))) & 'hFF;
                result=((result & ~'hFF0000)) | ((store_byte <<< 'h10));
            end
            byte_addr=load_addr + 'h3;
            diff=byte_addr - store_addr;
            if ((byte_addr>=store_addr && (diff < 'h4)) && ((store_mask & (('h1 <<< diff))))) begin
                store_byte=((store_data >>> ((diff*'h8)))) & 'hFF;
                result=((result & ~'hFF000000)) | ((store_byte <<< 'h18));
            end
        end
        load_raw_comb=result;
        disable load_raw_comb_func;
    end

    always_comb begin : load_result_comb_func  // load_result_comb_func
        logic[31:0] raw;
        raw=load_raw_comb;
        load_result_comb='h0;
        case (state_in.funct3)
        'h0: begin
            load_result_comb=unsigned'(32'(signed'(32'(signed'(8'(raw))))));
        end
        'h1: begin
            load_result_comb=unsigned'(32'(signed'(32'(signed'(16'(raw))))));
        end
        'h2: begin
            load_result_comb=raw;
        end
        'h4: begin
            load_result_comb=unsigned'(8'(raw));
        end
        'h5: begin
            load_result_comb=unsigned'(16'(raw));
        end
        endcase
        disable load_result_comb_func;
    end

    always_comb begin : wb_mem_data_comb_func  // wb_mem_data_comb_func
        if (split_load_in) begin
            wb_mem_data_comb=split_load_low_data_comb;
        end
        else begin
            wb_mem_data_comb=(load_data_valid_reg) ? (unsigned'(32'(load_data_reg))) : ((((((state_in.valid && (state_in.wb_op == Wb_pkg::MEM)) && dcache_read_valid_in) && (dcache_read_addr_in == alu_result_in))) ? (dcache_read_data_in) : (unsigned'(32'('h0)))));
        end
        disable wb_mem_data_comb_func;
    end

    always_comb begin : wb_mem_data_hi_comb_func  // wb_mem_data_hi_comb_func
        wb_mem_data_hi_comb=(split_load_in) ? (split_load_high_data_comb) : (unsigned'(32'('h0)));
        disable wb_mem_data_hi_comb_func;
    end

    task _work (input logic reset);
    begin: _work
        if (dcache_write_valid_in && dcache_write_mask_in) begin
            logic same_head; same_head = ((store_forward_valid_reg['h0] && (unsigned'(32'(store_forward_addr_reg['h0])) == dcache_write_addr_in)) && (unsigned'(32'(store_forward_data_reg['h0])) == dcache_write_data_in)) && (unsigned'(8'(store_forward_mask_reg['h0])) == dcache_write_mask_in);
            if (!same_head) begin
                store_forward_addr_reg_tmp['h1] = store_forward_addr_reg['h0];
                store_forward_data_reg_tmp['h1] = store_forward_data_reg['h0];
                store_forward_mask_reg_tmp['h1] = store_forward_mask_reg['h0];
                store_forward_valid_reg_tmp['h1] = store_forward_valid_reg['h0];
            end
            store_forward_addr_reg_tmp['h0] = dcache_write_addr_in;
            store_forward_data_reg_tmp['h0] = dcache_write_data_in;
            store_forward_mask_reg_tmp['h0] = dcache_write_mask_in;
            store_forward_valid_reg_tmp['h0] = 1;
        end
        if (hold_in) begin
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
                if (((state_in.valid && (state_in.wb_op == Wb_pkg::MEM)) && dcache_read_valid_in) && (dcache_read_addr_in == alu_result_in)) begin
                    load_data_reg_tmp = dcache_read_data_in;
                    load_data_valid_reg_tmp = 1;
                end
            end
        end
        else begin
            load_data_valid_reg_tmp = 0;
            split_load_low_valid_reg_tmp = 0;
            split_load_high_valid_reg_tmp = 0;
        end
        if (reset) begin
            load_data_reg_tmp = '0;
            load_data_valid_reg_tmp = '0;
            split_load_low_reg_tmp = '0;
            split_load_high_reg_tmp = '0;
            split_load_low_valid_reg_tmp = '0;
            split_load_high_valid_reg_tmp = '0;
            store_forward_addr_reg_tmp = '0;
            store_forward_data_reg_tmp = '0;
            store_forward_mask_reg_tmp = '0;
            store_forward_valid_reg_tmp = '0;
        end
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin
        load_data_reg_tmp = load_data_reg;
        load_data_valid_reg_tmp = load_data_valid_reg;
        split_load_low_reg_tmp = split_load_low_reg;
        split_load_high_reg_tmp = split_load_high_reg;
        split_load_low_valid_reg_tmp = split_load_low_valid_reg;
        split_load_high_valid_reg_tmp = split_load_high_valid_reg;
        store_forward_addr_reg_tmp = store_forward_addr_reg;
        store_forward_data_reg_tmp = store_forward_data_reg;
        store_forward_mask_reg_tmp = store_forward_mask_reg;
        store_forward_valid_reg_tmp = store_forward_valid_reg;

        _work(reset);

        load_data_reg <= load_data_reg_tmp;
        load_data_valid_reg <= load_data_valid_reg_tmp;
        split_load_low_reg <= split_load_low_reg_tmp;
        split_load_high_reg <= split_load_high_reg_tmp;
        split_load_low_valid_reg <= split_load_low_valid_reg_tmp;
        split_load_high_valid_reg <= split_load_high_valid_reg_tmp;
        store_forward_addr_reg <= store_forward_addr_reg_tmp;
        store_forward_data_reg <= store_forward_data_reg_tmp;
        store_forward_mask_reg <= store_forward_mask_reg_tmp;
        store_forward_valid_reg <= store_forward_valid_reg_tmp;
    end

    assign load_ready_out = load_ready_comb;

    assign load_raw_out = load_raw_comb;

    assign load_result_out = load_result_comb;

    assign wb_mem_data_out = wb_mem_data_comb;

    assign wb_mem_data_hi_out = wb_mem_data_hi_comb;


endmodule
