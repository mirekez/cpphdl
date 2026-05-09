`default_nettype none

import Predef_pkg::*;
import State_pkg::*;
import Mem_pkg::*;


module ExecuteMem (
    input wire clk
,   input wire reset
,   input State state_in
,   input wire[31:0] alu_result_in
,   input wire mem_stall_in
,   input wire hold_in
,   output wire mem_write_out
,   output wire[31:0] mem_write_addr_out
,   output wire[31:0] mem_write_data_out
,   output wire[7:0] mem_write_mask_out
,   output wire mem_read_out
,   output wire[31:0] mem_read_addr_out
,   output wire mem_split_out
,   output wire mem_split_busy_out
,   output wire split_load_out
,   output wire[31:0] split_load_low_out
,   output wire[31:0] split_load_high_out
);


    // regs and combs
    reg[32-1:0] mem_addr_reg;
    reg[32-1:0] mem_data_reg;
    reg[8-1:0] mem_mask_reg;
    reg mem_write_reg;
    reg mem_read_reg;
    reg mem_split_pending_reg;
    reg[32-1:0] mem_split_addr_reg;
    reg[32-1:0] mem_split_data_reg;
    reg[2-1:0] mem_split_offset_reg;
    reg[3-1:0] mem_split_size_reg;
    reg mem_split_write_reg;
    reg mem_split_read_reg;
    reg split_load_reg;
    reg[32-1:0] split_load_low_addr_reg;
    reg[32-1:0] split_load_high_addr_reg;
    logic[31:0] mem_size_comb;
;
    logic mem_split_comb;
;
    logic[7:0] first_split_mask_comb;
;
    logic[7:0] second_split_mask_comb;
;
    logic[31:0] split_load_low_addr_comb;
;
    logic[31:0] split_load_high_addr_comb;
;

    // members
    genvar gi, gj, gk;

    // tmp variables
    logic[32-1:0] mem_addr_reg_tmp;
    logic[32-1:0] mem_data_reg_tmp;
    logic[8-1:0] mem_mask_reg_tmp;
    logic mem_write_reg_tmp;
    logic mem_read_reg_tmp;
    logic mem_split_pending_reg_tmp;
    logic[32-1:0] mem_split_addr_reg_tmp;
    logic[32-1:0] mem_split_data_reg_tmp;
    logic[2-1:0] mem_split_offset_reg_tmp;
    logic[3-1:0] mem_split_size_reg_tmp;
    logic mem_split_write_reg_tmp;
    logic mem_split_read_reg_tmp;
    logic split_load_reg_tmp;
    logic[32-1:0] split_load_low_addr_reg_tmp;
    logic[32-1:0] split_load_high_addr_reg_tmp;


    always_comb begin : mem_size_comb_func  // mem_size_comb_func
        mem_size_comb='h0;
        case (state_in.funct3)
        'h0: begin
            mem_size_comb='h1;
        end
        'h1: begin
            mem_size_comb='h2;
        end
        'h2: begin
            mem_size_comb='h4;
        end
        'h4: begin
            mem_size_comb='h1;
        end
        'h5: begin
            mem_size_comb='h2;
        end
        default: begin
        end
        endcase
        disable mem_size_comb_func;
    end

    always_comb begin : mem_split_comb_func  // mem_split_comb_func
        logic[31:0] addr;
        logic[31:0] size;
        addr=alu_result_in;
        size=mem_size_comb;
        mem_split_comb=((state_in.valid && (((state_in.mem_op == Mem_pkg::LOAD) || (state_in.mem_op == Mem_pkg::STORE)))) && (size != 'h0)) && (((((addr & 'h1F)) + size) > 'h20));
        disable mem_split_comb_func;
    end

    always_comb begin : first_split_mask_comb_func  // first_split_mask_comb_func
        logic[31:0] size;
        logic[31:0] offset;
        logic[31:0] low_size;
        size=mem_size_comb;
        offset=alu_result_in & 'h3;
        low_size='h4 - offset;
        if (state_in.mem_op == Mem_pkg::STORE) begin
            first_split_mask_comb=unsigned'(8'(((('h1 <<< low_size)) - 'h1)));
        end
        else begin
            first_split_mask_comb=unsigned'(8'(((((((('h1 <<< size)) - 'h1)) <<< offset)) & 'hF)));
        end
        disable first_split_mask_comb_func;
    end

    always_comb begin : second_split_mask_comb_func  // second_split_mask_comb_func
        logic[31:0] overflow;
        overflow=(unsigned'(32'(mem_split_offset_reg)) + unsigned'(32'(mem_split_size_reg))) - 'h4;
        second_split_mask_comb=unsigned'(8'(((('h1 <<< overflow)) - 'h1)));
        disable second_split_mask_comb_func;
    end

    always_comb begin : split_load_low_addr_comb_func  // split_load_low_addr_comb_func
        split_load_low_addr_comb=alu_result_in & ~'h3;
        disable split_load_low_addr_comb_func;
    end

    always_comb begin : split_load_high_addr_comb_func  // split_load_high_addr_comb_func
        split_load_high_addr_comb=split_load_low_addr_comb + 'h4;
        disable split_load_high_addr_comb_func;
    end

    task do_memory ();
    begin: do_memory
        mem_write_reg_tmp = 'h0;
        mem_read_reg_tmp = 'h0;
        mem_mask_reg_tmp = 'h0;
        if (mem_stall_in) begin
            mem_addr_reg_tmp = mem_addr_reg;
            mem_data_reg_tmp = mem_data_reg;
            mem_write_reg_tmp = mem_write_reg;
            mem_read_reg_tmp = mem_read_reg;
            mem_mask_reg_tmp = mem_mask_reg;
            mem_split_pending_reg_tmp = mem_split_pending_reg;
            mem_split_addr_reg_tmp = mem_split_addr_reg;
            mem_split_data_reg_tmp = mem_split_data_reg;
            mem_split_offset_reg_tmp = mem_split_offset_reg;
            mem_split_size_reg_tmp = mem_split_size_reg;
            mem_split_write_reg_tmp = mem_split_write_reg;
            mem_split_read_reg_tmp = mem_split_read_reg;
            split_load_reg_tmp = split_load_reg;
            split_load_low_addr_reg_tmp = split_load_low_addr_reg;
            split_load_high_addr_reg_tmp = split_load_high_addr_reg;
            disable do_memory;
        end
        if (mem_split_pending_reg) begin
            logic[31:0] overflow; overflow = (unsigned'(32'(mem_split_offset_reg)) + unsigned'(32'(mem_split_size_reg))) - 'h4;
            mem_addr_reg_tmp = ((unsigned'(32'(mem_split_addr_reg)) & ~'h3)) + 'h4;
            mem_data_reg_tmp = unsigned'(32'(mem_split_data_reg)) >>> ((((unsigned'(32'(mem_split_size_reg)) - overflow))*'h8));
            mem_write_reg_tmp = mem_split_write_reg;
            mem_read_reg_tmp = mem_split_read_reg;
            mem_mask_reg_tmp = second_split_mask_comb;
            mem_split_pending_reg_tmp = 0;
            disable do_memory;
        end
        if (hold_in) begin
            split_load_reg_tmp = split_load_reg;
            split_load_low_addr_reg_tmp = split_load_low_addr_reg;
            split_load_high_addr_reg_tmp = split_load_high_addr_reg;
            disable do_memory;
        end
        mem_addr_reg_tmp = alu_result_in;
        mem_data_reg_tmp = state_in.rs2_val;
        split_load_reg_tmp = (state_in.valid && (state_in.mem_op == Mem_pkg::LOAD)) && mem_split_comb;
        split_load_low_addr_reg_tmp = split_load_low_addr_comb;
        split_load_high_addr_reg_tmp = split_load_high_addr_comb;
        if (mem_split_comb) begin
            logic[31:0] offset; offset = alu_result_in & 'h3;
            mem_addr_reg_tmp = (state_in.mem_op == Mem_pkg::STORE) ? (alu_result_in) : ((alu_result_in & ~'h3));
            mem_data_reg_tmp = (state_in.mem_op == Mem_pkg::STORE) ? (state_in.rs2_val) : ((state_in.rs2_val <<< ((offset*'h8))));
            mem_write_reg_tmp = state_in.mem_op == Mem_pkg::STORE;
            mem_read_reg_tmp = state_in.mem_op == Mem_pkg::LOAD;
            mem_mask_reg_tmp = (state_in.mem_op == Mem_pkg::STORE) ? (first_split_mask_comb) : (unsigned'(8'('h0)));
            mem_split_pending_reg_tmp = 1;
            mem_split_addr_reg_tmp = alu_result_in;
            mem_split_data_reg_tmp = state_in.rs2_val;
            mem_split_offset_reg_tmp = alu_result_in & 'h3;
            mem_split_size_reg_tmp = mem_size_comb;
            mem_split_write_reg_tmp = state_in.mem_op == Mem_pkg::STORE;
            mem_split_read_reg_tmp = state_in.mem_op == Mem_pkg::LOAD;
            disable do_memory;
        end
        if ((state_in.mem_op == Mem_pkg::STORE) && state_in.valid) begin
            case (state_in.funct3)
            'h0: begin
                mem_write_reg_tmp = state_in.valid;
                mem_mask_reg_tmp = 'h1;
            end
            'h1: begin
                mem_write_reg_tmp = state_in.valid;
                mem_mask_reg_tmp = 'h3;
            end
            'h2: begin
                mem_write_reg_tmp = state_in.valid;
                mem_mask_reg_tmp = 'hF;
            end
            endcase
        end
        if ((state_in.mem_op == Mem_pkg::LOAD) && state_in.valid) begin
            case (state_in.funct3)
            'h0: begin
                mem_read_reg_tmp = 'h1;
            end
            'h1: begin
                mem_read_reg_tmp = 'h1;
            end
            'h2: begin
                mem_read_reg_tmp = 'h1;
            end
            'h4: begin
                mem_read_reg_tmp = 'h1;
            end
            'h5: begin
                mem_read_reg_tmp = 'h1;
            end
            default: begin
            end
            endcase
        end
    end
    endtask

    task _work (input logic reset);
    begin: _work
        do_memory();
        if (reset) begin
            mem_write_reg_tmp = '0;
            mem_read_reg_tmp = '0;
            mem_split_pending_reg_tmp = '0;
            split_load_reg_tmp = '0;
        end
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin
        mem_addr_reg_tmp = mem_addr_reg;
        mem_data_reg_tmp = mem_data_reg;
        mem_mask_reg_tmp = mem_mask_reg;
        mem_write_reg_tmp = mem_write_reg;
        mem_read_reg_tmp = mem_read_reg;
        mem_split_pending_reg_tmp = mem_split_pending_reg;
        mem_split_addr_reg_tmp = mem_split_addr_reg;
        mem_split_data_reg_tmp = mem_split_data_reg;
        mem_split_offset_reg_tmp = mem_split_offset_reg;
        mem_split_size_reg_tmp = mem_split_size_reg;
        mem_split_write_reg_tmp = mem_split_write_reg;
        mem_split_read_reg_tmp = mem_split_read_reg;
        split_load_reg_tmp = split_load_reg;
        split_load_low_addr_reg_tmp = split_load_low_addr_reg;
        split_load_high_addr_reg_tmp = split_load_high_addr_reg;

        _work(reset);

        mem_addr_reg <= mem_addr_reg_tmp;
        mem_data_reg <= mem_data_reg_tmp;
        mem_mask_reg <= mem_mask_reg_tmp;
        mem_write_reg <= mem_write_reg_tmp;
        mem_read_reg <= mem_read_reg_tmp;
        mem_split_pending_reg <= mem_split_pending_reg_tmp;
        mem_split_addr_reg <= mem_split_addr_reg_tmp;
        mem_split_data_reg <= mem_split_data_reg_tmp;
        mem_split_offset_reg <= mem_split_offset_reg_tmp;
        mem_split_size_reg <= mem_split_size_reg_tmp;
        mem_split_write_reg <= mem_split_write_reg_tmp;
        mem_split_read_reg <= mem_split_read_reg_tmp;
        split_load_reg <= split_load_reg_tmp;
        split_load_low_addr_reg <= split_load_low_addr_reg_tmp;
        split_load_high_addr_reg <= split_load_high_addr_reg_tmp;
    end

    assign mem_write_out = mem_write_reg;

    assign mem_write_addr_out = mem_addr_reg;

    assign mem_write_data_out = mem_data_reg;

    assign mem_write_mask_out = mem_mask_reg;

    assign mem_read_out = mem_read_reg;

    assign mem_read_addr_out = mem_addr_reg;

    assign mem_split_out = mem_split_comb;

    assign mem_split_busy_out = mem_split_pending_reg;

    assign split_load_out = split_load_reg;

    assign split_load_low_out = split_load_low_addr_reg;

    assign split_load_high_out = split_load_high_addr_reg;


endmodule
