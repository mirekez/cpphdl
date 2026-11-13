`default_nettype none

import Predef_pkg::*;
import State_pkg::*;
import Amo_pkg::*;
import Mem_pkg::*;


module ExecuteMem (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire State state_in
,   input wire[31:0] alu_result_in
,   input wire dcache_read_valid_in
,   input wire[31:0] dcache_read_addr_in
,   input wire[31:0] dcache_read_expected_addr_in
,   input wire[31:0] dcache_read_data_in
,   input wire mem_stall_in
,   input wire hold_in
,   input wire transaction_owner_valid_in
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
,   output wire atomic_busy_out
,   output wire[31:0] atomic_sc_result_out
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
    reg reservation_valid_reg;
    reg[32-1:0] reservation_addr_reg;
    reg[32-1:0] reservation_physical_addr_reg;
    reg atomic_pending_reg;
    reg[32-1:0] atomic_addr_reg;
    reg[32-1:0] atomic_operand_reg;
    reg[4-1:0] atomic_op_reg;
    logic[31:0] mem_size_comb;
    logic mem_split_comb;
    logic atomic_read_ready_comb;
    logic[31:0] atomic_write_data_comb;
    logic atomic_sc_success_comb;
    logic[31:0] atomic_sc_result_comb;
    logic atomic_busy_comb;
    logic[7:0] first_split_mask_comb;
    logic[7:0] second_split_mask_comb;

    // members

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
    logic reservation_valid_reg_tmp;
    logic[32-1:0] reservation_addr_reg_tmp;
    logic[32-1:0] reservation_physical_addr_reg_tmp;
    logic atomic_pending_reg_tmp;
    logic[32-1:0] atomic_addr_reg_tmp;
    logic[32-1:0] atomic_operand_reg_tmp;
    logic[4-1:0] atomic_op_reg_tmp;


    always_comb begin : mem_size_comb_func  // mem_size_comb_func
        if (signed'(32'(state_in.amo_op)) != signed'(32'(Amo_pkg::AMONONE))) begin
            mem_size_comb=32'h4;
        end
        else begin
            mem_size_comb=32'h0;
            case (signed'(32'(state_in.funct3)))
            'h0: begin
                mem_size_comb=32'h1;
            end
            'h1: begin
                mem_size_comb=32'h2;
            end
            'h2: begin
                mem_size_comb=32'h4;
            end
            'h4: begin
                mem_size_comb=32'h1;
            end
            'h5: begin
                mem_size_comb=32'h2;
            end
            default: begin
            end
            endcase
        end
    end

    always_comb begin : mem_split_comb_func  // mem_split_comb_func
        logic[7:0] addr_offset;
        logic[31:0] size;
        addr_offset=(((((state_in.rs1_val & 'h1F)) + ((state_in.imm & 'h1F)))) & 'h1F);
        size=mem_size_comb;
        mem_split_comb=(((((state_in.valid) != '0) && (((signed'(32'(state_in.mem_op)) == signed'(32'(Mem_pkg::LOAD))) || (signed'(32'(state_in.mem_op)) == signed'(32'(Mem_pkg::STORE)))))) && (signed'(32'(state_in.amo_op)) == signed'(32'(Amo_pkg::AMONONE)))) && (size != 32'h0)) && (((32'(addr_offset) + size) > 32'h20));
    end

    always_comb begin : atomic_busy_comb_func  // atomic_busy_comb_func
        atomic_busy_comb=((atomic_pending_reg) != '0);
    end

    always_comb begin : atomic_sc_success_comb_func  // atomic_sc_success_comb_func
        atomic_sc_success_comb=((reservation_valid_reg) != '0) && ((unsigned'(32'(reservation_addr_reg)) == ((alu_result_in & ~'h3))));
    end

    always_comb begin : atomic_sc_result_comb_func  // atomic_sc_result_comb_func
        atomic_sc_result_comb=(atomic_sc_success_comb) ? ('h0) : ('h1);
    end

    always_comb begin : atomic_read_ready_comb_func  // atomic_read_ready_comb_func
        atomic_read_ready_comb=dcache_read_valid_in && (dcache_read_addr_in == dcache_read_expected_addr_in);
    end

    always_comb begin : atomic_write_data_comb_func  // atomic_write_data_comb_func
        logic[31:0] old_value;
        logic[31:0] operand;
        old_value=dcache_read_data_in;
        operand=atomic_operand_reg;
        atomic_write_data_comb=old_value;
        case (signed'(32'(unsigned'(8'(atomic_op_reg)))))
        signed'(32'(Amo_pkg::AMOSWAP_W)): begin
            atomic_write_data_comb=operand;
        end
        signed'(32'(Amo_pkg::AMOADD_W)): begin
            atomic_write_data_comb=old_value + operand;
        end
        signed'(32'(Amo_pkg::AMOXOR_W)): begin
            atomic_write_data_comb=old_value ^ operand;
        end
        signed'(32'(Amo_pkg::AMOAND_W)): begin
            atomic_write_data_comb=old_value & operand;
        end
        signed'(32'(Amo_pkg::AMOOR_W)): begin
            atomic_write_data_comb=old_value | operand;
        end
        signed'(32'(Amo_pkg::AMOMIN_W)): begin
            atomic_write_data_comb=(signed'(32'(old_value)) < signed'(32'(operand))) ? (old_value) : (operand);
        end
        signed'(32'(Amo_pkg::AMOMAX_W)): begin
            atomic_write_data_comb=(signed'(32'(old_value)) > signed'(32'(operand))) ? (old_value) : (operand);
        end
        signed'(32'(Amo_pkg::AMOMINU_W)): begin
            atomic_write_data_comb=(old_value < operand) ? (old_value) : (operand);
        end
        signed'(32'(Amo_pkg::AMOMAXU_W)): begin
            atomic_write_data_comb=(old_value > operand) ? (old_value) : (operand);
        end
        default: begin
        end
        endcase
    end

    always_comb begin : first_split_mask_comb_func  // first_split_mask_comb_func
        logic[31:0] size;
        logic[31:0] offset;
        logic[31:0] low_size;
        size=mem_size_comb;
        offset=alu_result_in & 'h3;
        low_size='h4 - offset;
        if (signed'(32'(state_in.mem_op)) == signed'(32'(Mem_pkg::STORE))) begin
            first_split_mask_comb=((('h1 <<< low_size)) - 'h1);
        end
        else begin
            first_split_mask_comb=((((((('h1 <<< size)) - 'h1)) <<< offset)) & 'hF);
        end
    end

    always_comb begin : second_split_mask_comb_func  // second_split_mask_comb_func
        logic[31:0] overflow;
        overflow=(unsigned'(32'(mem_split_offset_reg)) + unsigned'(32'(mem_split_size_reg))) - 'h4;
        second_split_mask_comb=((('h1 <<< overflow)) - 'h1);
    end

    task do_memory ();
    begin: do_memory
        mem_write_reg_tmp = (('h0) != '0);
        mem_read_reg_tmp = (('h0) != '0);
        mem_mask_reg_tmp = 8'h0;
        if (!transaction_owner_valid_in && ((((mem_split_pending_reg) != '0) || ((atomic_pending_reg) != '0)))) begin
            mem_split_pending_reg_tmp = 0;
            mem_split_write_reg_tmp = 0;
            mem_split_read_reg_tmp = 0;
            split_load_reg_tmp = 0;
            atomic_pending_reg_tmp = 0;
            atomic_op_reg_tmp = Amo_pkg::AMONONE;
            disable do_memory;
        end
        if (((atomic_pending_reg) != '0) && atomic_read_ready_comb) begin
            if (signed'(32'(unsigned'(8'(atomic_op_reg)))) == signed'(32'(Amo_pkg::LR_W))) begin
                reservation_valid_reg_tmp = 1;
                reservation_addr_reg_tmp = atomic_addr_reg;
                reservation_physical_addr_reg_tmp = 32'(dcache_read_expected_addr_in & ~'h3);
            end
            else begin
                mem_addr_reg_tmp = atomic_addr_reg;
                mem_data_reg_tmp = atomic_write_data_comb;
                mem_write_reg_tmp = 1;
                mem_mask_reg_tmp = 8'hF;
                reservation_valid_reg_tmp = 0;
            end
            atomic_pending_reg_tmp = 0;
            disable do_memory;
        end
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
            reservation_valid_reg_tmp = reservation_valid_reg;
            reservation_addr_reg_tmp = reservation_addr_reg;
            reservation_physical_addr_reg_tmp = reservation_physical_addr_reg;
            atomic_pending_reg_tmp = atomic_pending_reg;
            atomic_addr_reg_tmp = atomic_addr_reg;
            atomic_operand_reg_tmp = atomic_operand_reg;
            atomic_op_reg_tmp = atomic_op_reg;
            disable do_memory;
        end
        if (((atomic_pending_reg) != '0)) begin
            mem_addr_reg_tmp = atomic_addr_reg;
            mem_read_reg_tmp = 1;
            disable do_memory;
        end
        if (((mem_split_pending_reg) != '0)) begin
            logic[31:0] overflow; overflow = (unsigned'(32'(mem_split_offset_reg)) + unsigned'(32'(mem_split_size_reg))) - 'h4;
            mem_addr_reg_tmp = 32'(((unsigned'(32'(mem_split_addr_reg)) & ~'h3)) + 32'h4);
            mem_data_reg_tmp = 32'(unsigned'(32'(mem_split_data_reg)) >>> ((((unsigned'(32'(mem_split_size_reg)) - overflow))*'h8)));
            mem_write_reg_tmp = mem_split_write_reg;
            mem_read_reg_tmp = mem_split_read_reg;
            mem_mask_reg_tmp = second_split_mask_comb;
            mem_split_pending_reg_tmp = 0;
            if (((mem_split_read_reg) != '0)) begin
                split_load_low_addr_reg_tmp = 32'(unsigned'(32'(mem_split_addr_reg)) & ~'h3);
                split_load_high_addr_reg_tmp = 32'(((unsigned'(32'(mem_split_addr_reg)) & ~'h3)) + 'h4);
            end
            disable do_memory;
        end
        if (hold_in) begin
            mem_addr_reg_tmp = mem_addr_reg;
            mem_data_reg_tmp = mem_data_reg;
            mem_write_reg_tmp = mem_write_reg;
            mem_read_reg_tmp = mem_read_reg;
            mem_mask_reg_tmp = mem_mask_reg;
            split_load_reg_tmp = split_load_reg;
            split_load_low_addr_reg_tmp = split_load_low_addr_reg;
            split_load_high_addr_reg_tmp = split_load_high_addr_reg;
            disable do_memory;
        end
        mem_addr_reg_tmp = unsigned'(32'((signed'(32'(state_in.amo_op)) != signed'(32'(Amo_pkg::AMONONE))) ? ((alu_result_in & ~'h3)) : (alu_result_in)));
        mem_data_reg_tmp = state_in.rs2_val;
        split_load_reg_tmp = (((state_in.valid) != '0) && (signed'(32'(state_in.mem_op)) == signed'(32'(Mem_pkg::LOAD)))) && mem_split_comb;
        if (((state_in.valid) != '0) && (signed'(32'(state_in.amo_op)) != signed'(32'(Amo_pkg::AMONONE)))) begin
            if (signed'(32'(state_in.amo_op)) == signed'(32'(Amo_pkg::SC_W))) begin
                if (atomic_sc_success_comb) begin
                    mem_write_reg_tmp = 1;
                    mem_mask_reg_tmp = 8'hF;
                end
                reservation_valid_reg_tmp = 0;
            end
            else begin
                mem_read_reg_tmp = 1;
                atomic_pending_reg_tmp = 1;
                atomic_addr_reg_tmp = 32'(alu_result_in & ~'h3);
                atomic_operand_reg_tmp = state_in.rs2_val;
                atomic_op_reg_tmp = state_in.amo_op;
            end
            disable do_memory;
        end
        if (mem_split_comb) begin
            logic[31:0] offset; offset = alu_result_in & 'h3;
            mem_addr_reg_tmp = unsigned'(32'((signed'(32'(state_in.mem_op)) == signed'(32'(Mem_pkg::STORE))) ? (alu_result_in) : ((alu_result_in & ~'h3))));
            mem_data_reg_tmp = unsigned'(32'((signed'(32'(state_in.mem_op)) == signed'(32'(Mem_pkg::STORE))) ? (state_in.rs2_val) : ((state_in.rs2_val <<< ((offset*'h8))))));
            mem_write_reg_tmp = signed'(32'(state_in.mem_op)) == signed'(32'(Mem_pkg::STORE));
            mem_read_reg_tmp = signed'(32'(state_in.mem_op)) == signed'(32'(Mem_pkg::LOAD));
            mem_mask_reg_tmp = unsigned'(8'((signed'(32'(state_in.mem_op)) == signed'(32'(Mem_pkg::STORE))) ? (first_split_mask_comb) : (8'h0)));
            mem_split_pending_reg_tmp = 1;
            mem_split_addr_reg_tmp = alu_result_in;
            mem_split_data_reg_tmp = state_in.rs2_val;
            mem_split_offset_reg_tmp = 32'(alu_result_in & 'h3);
            mem_split_size_reg_tmp = mem_size_comb;
            mem_split_write_reg_tmp = signed'(32'(state_in.mem_op)) == signed'(32'(Mem_pkg::STORE));
            mem_split_read_reg_tmp = signed'(32'(state_in.mem_op)) == signed'(32'(Mem_pkg::LOAD));
            disable do_memory;
        end
        if ((signed'(32'(state_in.mem_op)) == signed'(32'(Mem_pkg::STORE))) && ((state_in.valid) != '0)) begin
            case (signed'(32'(state_in.funct3)))
            'h0: begin
                mem_write_reg_tmp = ((state_in.valid) != '0);
                mem_mask_reg_tmp = 8'h1;
            end
            'h1: begin
                mem_write_reg_tmp = ((state_in.valid) != '0);
                mem_mask_reg_tmp = 8'h3;
            end
            'h2: begin
                mem_write_reg_tmp = ((state_in.valid) != '0);
                mem_mask_reg_tmp = 8'hF;
            end
            endcase
        end
        if ((signed'(32'(state_in.mem_op)) == signed'(32'(Mem_pkg::LOAD))) && ((state_in.valid) != '0)) begin
            case (signed'(32'(state_in.funct3)))
            'h0: begin
                mem_read_reg_tmp = (('h1) != '0);
            end
            'h1: begin
                mem_read_reg_tmp = (('h1) != '0);
            end
            'h2: begin
                mem_read_reg_tmp = (('h1) != '0);
            end
            'h4: begin
                mem_read_reg_tmp = (('h1) != '0);
            end
            'h5: begin
                mem_read_reg_tmp = (('h1) != '0);
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
            reservation_valid_reg_tmp = '0;
            atomic_pending_reg_tmp = '0;
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
        reservation_valid_reg_tmp = reservation_valid_reg;
        reservation_addr_reg_tmp = reservation_addr_reg;
        reservation_physical_addr_reg_tmp = reservation_physical_addr_reg;
        atomic_pending_reg_tmp = atomic_pending_reg;
        atomic_addr_reg_tmp = atomic_addr_reg;
        atomic_operand_reg_tmp = atomic_operand_reg;
        atomic_op_reg_tmp = atomic_op_reg;

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
        reservation_valid_reg <= reservation_valid_reg_tmp;
        reservation_addr_reg <= reservation_addr_reg_tmp;
        reservation_physical_addr_reg <= reservation_physical_addr_reg_tmp;
        atomic_pending_reg <= atomic_pending_reg_tmp;
        atomic_addr_reg <= atomic_addr_reg_tmp;
        atomic_operand_reg <= atomic_operand_reg_tmp;
        atomic_op_reg <= atomic_op_reg_tmp;
    end

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

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

    assign atomic_busy_out = atomic_busy_comb;

    assign atomic_sc_result_out = atomic_sc_result_comb;


endmodule
