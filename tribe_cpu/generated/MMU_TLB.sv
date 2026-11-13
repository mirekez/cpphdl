`default_nettype none

import Predef_pkg::*;


module MMU_TLB #(
    parameter ENTRIES = 'h8
 )
 (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire[31:0] vaddr_in
,   input wire read_in
,   input wire write_in
,   input wire execute_in
,   input wire[31:0] satp_in
,   input wire[2-1:0] priv_in
,   input wire sum_in
,   input wire mxr_in
,   input wire[31:0] direct_base_in
,   input wire[31:0] direct_size_in
,   input wire fill_in
,   input wire[unsigned'(64'($clog2(unsigned'(32'(ENTRIES)))))-1:0] fill_index_in
,   input wire[31:0] fill_vpn_in
,   input wire[31:0] fill_ppn_in
,   input wire[7:0] fill_flags_in
,   input wire sfence_in
,   output wire mem_read_out
,   output wire[31:0] mem_addr_out
,   input wire[31:0] mem_read_data_in
,   input wire mem_wait_in
,   output wire[31:0] paddr_out
,   output wire translated_out
,   output wire hit_out
,   output wire fault_out
,   output wire miss_out
,   output wire busy_out
,   output wire[31:0] debug_last_pte_out
,   output wire[31:0] debug_last_addr_out
);
    localparam  PTE_V = 'h1;
    localparam  PTE_R = 'h2;
    localparam  PTE_W = 'h4;
    localparam  PTE_X = 'h8;
    localparam  PTE_U = 'h10;
    localparam  PTE_A = 'h40;
    localparam  PTE_D = 'h80;
    localparam  ST_IDLE = 64'h0;
    localparam  ST_READ_L1 = 64'h1;
    localparam  ST_READ_L0 = 64'h2;
    localparam  ST_FAULT = 64'h3;


    // regs and combs
    reg[ENTRIES-1:0] valid_reg;
    reg[ENTRIES-1:0][32-1:0] vpn_reg;
    reg[ENTRIES-1:0][32-1:0] ppn_reg;
    reg[ENTRIES-1:0][8-1:0] flags_reg;
    reg[ENTRIES-1:0] level_reg;
    reg[ENTRIES-1:0][32-1:0] satp_tag_reg;
    reg[unsigned'(64'($clog2(unsigned'(32'(ENTRIES)))))-1:0] victim_reg;
    reg[2-1:0] state_reg;
    reg[32-1:0] req_vaddr_reg;
    reg req_read_reg;
    reg req_write_reg;
    reg req_execute_reg;
    reg req_sum_reg;
    reg req_mxr_reg;
    reg[32-1:0] req_satp_reg;
    reg[2-1:0] req_priv_reg;
    reg[32-1:0] l1_pte_reg;
    reg fault_reg;
    reg[32-1:0] debug_last_pte_reg;
    reg[32-1:0] debug_last_addr_reg;
    logic translation_enabled_comb;
    logic direct_mapping_comb;
    logic[31:0] vpn_comb;
    logic[31:0] vpn1_comb;
    logic[31:0] page_offset_comb;
    logic[31:0] req_vpn1_comb;
    logic[31:0] req_vpn0_comb;
    logic[unsigned'(64'($clog2(unsigned'(32'(ENTRIES)))))-1:0] hit_index_comb;
    logic hit_comb;
    logic[31:0] entry_flags_comb;
    logic permission_fault_comb;
    logic miss_comb;
    logic fault_comb;
    logic[31:0] paddr_comb;
    logic mem_read_comb;
    logic[31:0] mem_addr_comb;
    logic busy_comb;

    // members

    // tmp variables
    logic[ENTRIES-1:0] valid_reg_tmp;
    logic[ENTRIES-1:0][32-1:0] vpn_reg_tmp;
    logic[ENTRIES-1:0][32-1:0] ppn_reg_tmp;
    logic[ENTRIES-1:0][8-1:0] flags_reg_tmp;
    logic[ENTRIES-1:0] level_reg_tmp;
    logic[ENTRIES-1:0][32-1:0] satp_tag_reg_tmp;
    logic[unsigned'(64'($clog2(unsigned'(32'(ENTRIES)))))-1:0] victim_reg_tmp;
    logic[2-1:0] state_reg_tmp;
    logic[32-1:0] req_vaddr_reg_tmp;
    logic req_read_reg_tmp;
    logic req_write_reg_tmp;
    logic req_execute_reg_tmp;
    logic req_sum_reg_tmp;
    logic req_mxr_reg_tmp;
    logic[32-1:0] req_satp_reg_tmp;
    logic[2-1:0] req_priv_reg_tmp;
    logic[32-1:0] l1_pte_reg_tmp;
    logic fault_reg_tmp;
    logic[32-1:0] debug_last_pte_reg_tmp;
    logic[32-1:0] debug_last_addr_reg_tmp;


    always_comb begin : mem_read_comb_func  // mem_read_comb_func
        mem_read_comb=(state_reg == unsigned'(64'(ST_READ_L1))) || (state_reg == unsigned'(64'(ST_READ_L0)));
    end

    always_comb begin : req_vpn1_comb_func  // req_vpn1_comb_func
        req_vpn1_comb=((unsigned'(32'(req_vaddr_reg)) >>> 32'sh16)) & 'h3FF;
    end

    always_comb begin : req_vpn0_comb_func  // req_vpn0_comb_func
        req_vpn0_comb=((unsigned'(32'(req_vaddr_reg)) >>> 32'shC)) & 'h3FF;
    end

    always_comb begin : mem_addr_comb_func  // mem_addr_comb_func
        mem_addr_comb=((((unsigned'(32'(req_satp_reg)) & 'h3FFFFF)) <<< 'hC)) + (req_vpn1_comb*'h4);
        if (state_reg == unsigned'(64'(ST_READ_L0))) begin
            mem_addr_comb=((((unsigned'(32'(l1_pte_reg)) >>> 32'shA)) <<< 'hC)) + (req_vpn0_comb*'h4);
        end
    end

    always_comb begin : direct_mapping_comb_func  // direct_mapping_comb_func
        logic[31:0] addr;
        logic[31:0] base;
        logic[31:0] size;
        logic[31:0] offset;
        addr=vaddr_in;
        base=direct_base_in;
        size=direct_size_in;
        offset=addr - base;
        direct_mapping_comb=((size != 32'h0) && addr>=base) && (offset < size);
    end

    always_comb begin : translation_enabled_comb_func  // translation_enabled_comb_func
        logic[31:0] mode;
        mode=satp_in >>> 32'sh1F;
        translation_enabled_comb=((mode == 32'h1) && (priv_in != 64'h3)) && !direct_mapping_comb;
    end

    always_comb begin : vpn1_comb_func  // vpn1_comb_func
        vpn1_comb=((vaddr_in >>> 32'sh16)) & 'h3FF;
    end

    always_comb begin : vpn_comb_func  // vpn_comb_func
        vpn_comb=vaddr_in >>> 32'shC;
    end

    always_comb begin : hit_comb_func  // hit_comb_func
        logic[63:0] i;
        hit_comb=0;
        if (!translation_enabled_comb) begin
            hit_comb=1;
        end
        else begin
            for (i=64'h0;i < ENTRIES;i=i+1) begin
                if ((((valid_reg[i]) != '0) && (unsigned'(32'(satp_tag_reg[i])) == satp_in)) && ((((((level_reg[i]) != '0) && (((unsigned'(32'(vpn_reg[i])) >>> 32'shA)) == vpn1_comb))) || ((!((level_reg[i]) != '0) && (unsigned'(32'(vpn_reg[i])) == vpn_comb)))))) begin
                    hit_comb=1;
                end
            end
        end
    end

    always_comb begin : hit_index_comb_func  // hit_index_comb_func
        logic[63:0] i;
        hit_index_comb = 64'h0;
        for (i=64'h0;i < ENTRIES;i=i+1) begin
            if ((((valid_reg[i]) != '0) && (unsigned'(32'(satp_tag_reg[i])) == satp_in)) && ((((((level_reg[i]) != '0) && (((unsigned'(32'(vpn_reg[i])) >>> 32'shA)) == vpn1_comb))) || ((!((level_reg[i]) != '0) && (unsigned'(32'(vpn_reg[i])) == vpn_comb)))))) begin
                hit_index_comb = (unsigned'(64'($clog2(unsigned'(32'(ENTRIES))))))'(i);
            end
        end
    end

    always_comb begin : entry_flags_comb_func  // entry_flags_comb_func
        entry_flags_comb=(hit_comb) ? (signed'(32'(unsigned'(8'(flags_reg[hit_index_comb]))))) : ('h0);
    end

    function logic pte_permission_fault (
        input logic[31:0] pte
,       input logic read
,       input logic write
,       input logic execute
,       input logic[31:0] priv
,       input logic sum
,       input logic mxr
    );
        logic user_page;
        user_page=((pte & PTE_U)) != 32'h0;
        if (((pte & PTE_A)) == 32'h0) begin
            return 1;
        end
        if (write && (((pte & PTE_D)) == 32'h0)) begin
            return 1;
        end
        if ((read && (((pte & PTE_R)) == 32'h0)) && !((mxr && (((pte & PTE_X)) != 32'h0)))) begin
            return 1;
        end
        if (write && (((pte & PTE_W)) == 32'h0)) begin
            return 1;
        end
        if (execute && (((pte & PTE_X)) == 32'h0)) begin
            return 1;
        end
        if ((priv == 32'h0) && !user_page) begin
            return 1;
        end
        if (((priv == 32'h1) && user_page) && ((execute || !sum))) begin
            return 1;
        end
        return 0;
    endfunction

    always_comb begin : permission_fault_comb_func  // permission_fault_comb_func
        logic[31:0] flags;
        logic access;
        permission_fault_comb=0;
        access=(read_in || write_in) || execute_in;
        flags=entry_flags_comb;
        if ((translation_enabled_comb && access) && hit_comb) begin
            permission_fault_comb=pte_permission_fault(flags, read_in, write_in, execute_in, unsigned'(32'(priv_in)), sum_in, mxr_in);
        end
    end

    always_comb begin : page_offset_comb_func  // page_offset_comb_func
        page_offset_comb=vaddr_in & 'hFFF;
    end

    always_comb begin : paddr_comb_func  // paddr_comb_func
        if (!translation_enabled_comb) begin
            paddr_comb=vaddr_in;
        end
        else begin
            if (hit_comb && !permission_fault_comb) begin
                if (((level_reg[hit_index_comb]) != '0)) begin
                    paddr_comb=((((unsigned'(32'(ppn_reg[hit_index_comb])) & ~'h3FF)) <<< 'hC)) | ((vaddr_in & 'h3FFFFF));
                end
                else begin
                    paddr_comb=((unsigned'(32'(ppn_reg[hit_index_comb])) <<< 'hC)) | page_offset_comb;
                end
            end
            else begin
                paddr_comb=vaddr_in;
            end
        end
    end

    always_comb begin : fault_comb_func  // fault_comb_func
        logic access;
        access=(read_in || write_in) || execute_in;
        fault_comb=(translation_enabled_comb && access) && ((((((fault_reg) != '0) && (unsigned'(32'(req_vaddr_reg)) == vaddr_in))) || permission_fault_comb));
    end

    always_comb begin : miss_comb_func  // miss_comb_func
        miss_comb=((translation_enabled_comb && (((read_in || write_in) || execute_in))) && !hit_comb) && !((fault_reg) != '0);
    end

    always_comb begin : busy_comb_func  // busy_comb_func
        logic current_walker_fault;
        busy_comb=0;
        current_walker_fault=((fault_reg) != '0) && (unsigned'(32'(req_vaddr_reg)) == vaddr_in);
        if (((translation_enabled_comb && (((read_in || write_in) || execute_in))) && !hit_comb) && !current_walker_fault) begin
            busy_comb=1;
        end
        if ((state_reg == unsigned'(64'(ST_READ_L1))) || (state_reg == unsigned'(64'(ST_READ_L0)))) begin
            busy_comb=1;
        end
    end

    function logic pte_invalid (input logic[31:0] pte);
        return (((pte & PTE_V)) == 32'h0) || (((((pte & PTE_W)) != 32'h0) && (((pte & PTE_R)) == 32'h0)));
    endfunction

    function logic pte_leaf (input logic[31:0] pte);
        return ((pte & ((PTE_R | PTE_X)))) != 32'h0;
    endfunction

    task fill_entry (
        input logic[31:0] vpn
,       input logic[31:0] ppn
,       input logic[31:0] flags
,       input logic level
    );
    begin: fill_entry
        valid_reg_tmp[victim_reg] = 1;
        vpn_reg_tmp[victim_reg] = vpn;
        ppn_reg_tmp[victim_reg] = ppn;
        flags_reg_tmp[victim_reg] = flags;
        level_reg_tmp[victim_reg] = level;
        satp_tag_reg_tmp[victim_reg] = req_satp_reg;
        victim_reg_tmp = victim_reg + 64'h1;
    end
    endtask

    task handle_pte (
        input logic[31:0] pte
,       input logic level1
    );
    begin: handle_pte
        logic[31:0] ppn;
        fault_reg_tmp = 0;
        if (pte_invalid(pte)) begin
            fault_reg_tmp = 1;
            state_reg_tmp = ST_FAULT;
            disable handle_pte;
        end
        if (pte_leaf(pte)) begin
            if (((level1 && (((((pte >>> 32'shA)) & 'h3FF)) != 32'h0))) || pte_permission_fault(pte, ((req_read_reg) != '0), ((req_write_reg) != '0), ((req_execute_reg) != '0), unsigned'(32'(req_priv_reg)), ((req_sum_reg) != '0), ((req_mxr_reg) != '0))) begin
                fault_reg_tmp = 1;
                state_reg_tmp = ST_FAULT;
                disable handle_pte;
            end
            ppn=pte >>> 32'shA;
            if (level1) begin
                ppn&=~'h3FF;
            end
            fill_entry((level1) ? ((((unsigned'(32'(req_vaddr_reg)) >>> 32'shC)) & ~'h3FF)) : ((unsigned'(32'(req_vaddr_reg)) >>> 32'shC)), ppn, pte & 'hFF, level1);
            state_reg_tmp = ST_IDLE;
            disable handle_pte;
        end
        if (level1) begin
            l1_pte_reg_tmp = pte;
            state_reg_tmp = ST_READ_L0;
            disable handle_pte;
        end
        fault_reg_tmp = 1;
        state_reg_tmp = ST_FAULT;
    end
    endtask

    task _work (input logic reset);
    begin: _work
        if (sfence_in) begin
            valid_reg_tmp = '0;
            fault_reg_tmp = '0;
            state_reg_tmp = ST_IDLE;
        end
        else begin
            if (fill_in) begin
                valid_reg_tmp[fill_index_in] = 1;
                vpn_reg_tmp[fill_index_in] = fill_vpn_in;
                ppn_reg_tmp[fill_index_in] = fill_ppn_in;
                flags_reg_tmp[fill_index_in] = fill_flags_in;
                level_reg_tmp[fill_index_in] = 0;
                satp_tag_reg_tmp[fill_index_in] = satp_in;
            end
            if (state_reg == unsigned'(64'(ST_IDLE))) begin
                if (!miss_comb) begin
                    fault_reg_tmp = '0;
                end
                if (miss_comb) begin
                    req_vaddr_reg_tmp = vaddr_in;
                    req_read_reg_tmp = read_in;
                    req_write_reg_tmp = write_in;
                    req_execute_reg_tmp = execute_in;
                    req_sum_reg_tmp = sum_in;
                    req_mxr_reg_tmp = mxr_in;
                    req_satp_reg_tmp = satp_in;
                    req_priv_reg_tmp = priv_in;
                    fault_reg_tmp = '0;
                    state_reg_tmp = ST_READ_L1;
                end
            end
            else begin
                if (state_reg == unsigned'(64'(ST_READ_L1))) begin
                    if (!mem_wait_in) begin
                        debug_last_addr_reg_tmp = mem_addr_out;
                        debug_last_pte_reg_tmp = mem_read_data_in;
                        handle_pte(mem_read_data_in, 1);
                    end
                end
                else begin
                    if (state_reg == unsigned'(64'(ST_READ_L0))) begin
                        if (!mem_wait_in) begin
                            debug_last_addr_reg_tmp = mem_addr_out;
                            debug_last_pte_reg_tmp = mem_read_data_in;
                            handle_pte(mem_read_data_in, 0);
                        end
                    end
                    else begin
                        if (state_reg == unsigned'(64'(ST_FAULT))) begin
                            if ((!translation_enabled_comb || !(((read_in || write_in) || execute_in))) || (vaddr_in != unsigned'(32'(req_vaddr_reg)))) begin
                                fault_reg_tmp = '0;
                                state_reg_tmp = ST_IDLE;
                            end
                        end
                    end
                end
            end
        end
        if (reset) begin
            valid_reg_tmp = '0;
            vpn_reg_tmp = '0;
            ppn_reg_tmp = '0;
            flags_reg_tmp = '0;
            level_reg_tmp = '0;
            satp_tag_reg_tmp = '0;
            victim_reg_tmp = '0;
            state_reg_tmp = '0;
            req_vaddr_reg_tmp = '0;
            req_read_reg_tmp = '0;
            req_write_reg_tmp = '0;
            req_execute_reg_tmp = '0;
            req_sum_reg_tmp = '0;
            req_mxr_reg_tmp = '0;
            req_satp_reg_tmp = '0;
            req_priv_reg_tmp = '0;
            l1_pte_reg_tmp = '0;
            fault_reg_tmp = '0;
            debug_last_pte_reg_tmp = '0;
            debug_last_addr_reg_tmp = '0;
        end
    end
    endtask

    task _work_l2_clock (input logic reset);
    begin: _work_l2_clock
    end
    endtask

    always_ff @(posedge clk) begin
        valid_reg_tmp = valid_reg;
        vpn_reg_tmp = vpn_reg;
        ppn_reg_tmp = ppn_reg;
        flags_reg_tmp = flags_reg;
        level_reg_tmp = level_reg;
        satp_tag_reg_tmp = satp_tag_reg;
        victim_reg_tmp = victim_reg;
        state_reg_tmp = state_reg;
        req_vaddr_reg_tmp = req_vaddr_reg;
        req_read_reg_tmp = req_read_reg;
        req_write_reg_tmp = req_write_reg;
        req_execute_reg_tmp = req_execute_reg;
        req_sum_reg_tmp = req_sum_reg;
        req_mxr_reg_tmp = req_mxr_reg;
        req_satp_reg_tmp = req_satp_reg;
        req_priv_reg_tmp = req_priv_reg;
        l1_pte_reg_tmp = l1_pte_reg;
        fault_reg_tmp = fault_reg;
        debug_last_pte_reg_tmp = debug_last_pte_reg;
        debug_last_addr_reg_tmp = debug_last_addr_reg;

            _work(reset);

        valid_reg <= valid_reg_tmp;
        vpn_reg <= vpn_reg_tmp;
        ppn_reg <= ppn_reg_tmp;
        flags_reg <= flags_reg_tmp;
        level_reg <= level_reg_tmp;
        satp_tag_reg <= satp_tag_reg_tmp;
        victim_reg <= victim_reg_tmp;
        state_reg <= state_reg_tmp;
        req_vaddr_reg <= req_vaddr_reg_tmp;
        req_read_reg <= req_read_reg_tmp;
        req_write_reg <= req_write_reg_tmp;
        req_execute_reg <= req_execute_reg_tmp;
        req_sum_reg <= req_sum_reg_tmp;
        req_mxr_reg <= req_mxr_reg_tmp;
        req_satp_reg <= req_satp_reg_tmp;
        req_priv_reg <= req_priv_reg_tmp;
        l1_pte_reg <= l1_pte_reg_tmp;
        fault_reg <= fault_reg_tmp;
        debug_last_pte_reg <= debug_last_pte_reg_tmp;
        debug_last_addr_reg <= debug_last_addr_reg_tmp;
    end

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

    end

    assign mem_read_out = mem_read_comb;

    assign mem_addr_out = mem_addr_comb;

    assign paddr_out = paddr_comb;

    assign translated_out = translation_enabled_comb;

    assign hit_out = hit_comb;

    assign fault_out = fault_comb;

    assign miss_out = miss_comb;

    assign busy_out = busy_comb;

    assign debug_last_pte_out = debug_last_pte_reg;

    assign debug_last_addr_out = debug_last_addr_reg;


endmodule
