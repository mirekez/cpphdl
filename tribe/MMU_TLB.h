#pragma once

#include "Config.h"

using namespace cpphdl;

template<size_t ENTRIES = 8>
class MMU_TLB : public Module
{
    static_assert(ENTRIES > 0, "MMU_TLB needs at least one entry");

    static constexpr uint32_t PTE_V = 1u << 0;
    static constexpr uint32_t PTE_R = 1u << 1;
    static constexpr uint32_t PTE_W = 1u << 2;
    static constexpr uint32_t PTE_X = 1u << 3;
    static constexpr uint32_t PTE_U = 1u << 4;
    static constexpr uint32_t PTE_A = 1u << 6;
    static constexpr uint32_t PTE_D = 1u << 7;

    static constexpr uint64_t ST_IDLE = 0;
    static constexpr uint64_t ST_READ_L1 = 1;
    static constexpr uint64_t ST_READ_L0 = 2;
    static constexpr uint64_t ST_FAULT = 3;

public:
    _PORT(uint32_t) vaddr_in;
    _PORT(bool)     read_in;
    _PORT(bool)     write_in;
    _PORT(bool)     execute_in;
    _PORT(uint32_t) satp_in;
    _PORT(u<2>)     priv_in;
    _PORT(bool)     sum_in = _ASSIGN(false);
    _PORT(bool)     mxr_in = _ASSIGN(false);
    _PORT(uint32_t) direct_base_in = _ASSIGN((uint32_t)0);
    _PORT(uint32_t) direct_size_in = _ASSIGN((uint32_t)0);

    _PORT(bool)     fill_in;
    _PORT(u<clog2(ENTRIES)>) fill_index_in;
    _PORT(uint32_t) fill_vpn_in;
    _PORT(uint32_t) fill_ppn_in;
    _PORT(uint8_t)  fill_flags_in;
    _PORT(bool)     sfence_in;

    _PORT(bool)     mem_read_out = _ASSIGN_COMB(mem_read_comb_func());
    _PORT(uint32_t) mem_addr_out = _ASSIGN_COMB(mem_addr_comb_func());
    _PORT(uint32_t) mem_read_data_in;
    _PORT(bool)     mem_wait_in;

    _PORT(uint32_t) paddr_out = _ASSIGN_COMB(paddr_comb_func());
    _PORT(bool)     translated_out = _ASSIGN_COMB(translation_enabled_comb_func());
    _PORT(bool)     hit_out = _ASSIGN_COMB(hit_comb_func());
    _PORT(bool)     fault_out = _ASSIGN_COMB(fault_comb_func());
    _PORT(bool)     miss_out = _ASSIGN_COMB(miss_comb_func());
    _PORT(bool)     busy_out = _ASSIGN_COMB(busy_comb_func());
    _PORT(uint32_t) debug_last_pte_out = _ASSIGN_REG(debug_last_pte_reg);
    _PORT(uint32_t) debug_last_addr_out = _ASSIGN_REG(debug_last_addr_reg);

private:
    reg<array<u1, ENTRIES>> valid_reg;
    reg<array<u32, ENTRIES>> vpn_reg;
    reg<array<u32, ENTRIES>> ppn_reg;
    reg<array<u8, ENTRIES>> flags_reg;
    reg<array<u1, ENTRIES>> level_reg;
    reg<array<u32, ENTRIES>> satp_tag_reg;
    reg<u<clog2(ENTRIES)>> victim_reg;

    reg<u<2>> state_reg;
    reg<u32> req_vaddr_reg;
    reg<u1> req_read_reg;
    reg<u1> req_write_reg;
    reg<u1> req_execute_reg;
    reg<u1> req_sum_reg;
    reg<u1> req_mxr_reg;
    reg<u32> req_satp_reg;
    reg<u<2>> req_priv_reg;
    reg<u32> l1_pte_reg;
    reg<u1> fault_reg;
    reg<u32> debug_last_pte_reg;
    reg<u32> debug_last_addr_reg;

    // Sv32 translation is active outside M-mode unless the access is in the direct MMIO window.
    _LAZY_COMB(translation_enabled_comb, bool)
        uint32_t mode;
        mode = satp_in() >> 31;
        translation_enabled_comb = mode == 1 && priv_in() != 3 && !direct_mapping_comb_func();
        return translation_enabled_comb;
    }

    // Direct window keeps MMIO and boot-mapped regions out of the page-table walker.
    _LAZY_COMB(direct_mapping_comb, bool)
        uint32_t addr;
        uint32_t base;
        uint32_t size;
        uint32_t offset;
        addr = vaddr_in();
        base = direct_base_in();
        size = direct_size_in();
        offset = addr - base;
        direct_mapping_comb = size != 0 && addr >= base && offset < size;
        return direct_mapping_comb;
    }

    // Full Sv32 virtual page number used by leaf level-0 TLB entries.
    _LAZY_COMB(vpn_comb, uint32_t)
        return vpn_comb = vaddr_in() >> 12;
    }

    // Top-level Sv32 VPN field used for superpage TLB matches.
    _LAZY_COMB(vpn1_comb, uint32_t)
        return vpn1_comb = (vaddr_in() >> 22) & 0x3ffu;
    }

    // Page offset is carried unchanged through translation.
    _LAZY_COMB(page_offset_comb, uint32_t)
        return page_offset_comb = vaddr_in() & 0xfffu;
    }

    // Registered top-level VPN field for the active page-table walk.
    _LAZY_COMB(req_vpn1_comb, uint32_t)
        return req_vpn1_comb = ((uint32_t)req_vaddr_reg >> 22) & 0x3ffu;
    }

    // Registered second-level VPN field for the active page-table walk.
    _LAZY_COMB(req_vpn0_comb, uint32_t)
        return req_vpn0_comb = ((uint32_t)req_vaddr_reg >> 12) & 0x3ffu;
    }

    // TLB match index, accepting either exact level-0 entries or level-1 superpages.
    // Sv32 entries are tagged by satp so address-space switches cannot reuse stale translations.
    _LAZY_COMB(hit_index_comb, u<clog2(ENTRIES)>)
        size_t i;
        hit_index_comb = 0;
        for (i = 0; i < ENTRIES; ++i) {
            if (valid_reg[i] && (uint32_t)satp_tag_reg[i] == satp_in() &&
                (((bool)level_reg[i] && ((uint32_t)vpn_reg[i] >> 10) == vpn1_comb_func()) ||
                 (!(bool)level_reg[i] && (uint32_t)vpn_reg[i] == vpn_comb_func()))) {
                hit_index_comb = (u<clog2(ENTRIES)>)i;
            }
        }
        return hit_index_comb;
    }

    // Translation hit is trivially true when translation is disabled.
    _LAZY_COMB(hit_comb, bool)
        size_t i;
        hit_comb = false;
        if (!translation_enabled_comb_func()) {
            hit_comb = true;
        }
        else {
            for (i = 0; i < ENTRIES; ++i) {
                if (valid_reg[i] && (uint32_t)satp_tag_reg[i] == satp_in() &&
                    (((bool)level_reg[i] && ((uint32_t)vpn_reg[i] >> 10) == vpn1_comb_func()) ||
                     (!(bool)level_reg[i] && (uint32_t)vpn_reg[i] == vpn_comb_func()))) {
                    hit_comb = true;
                }
            }
        }
        return hit_comb;
    }

    // Permission bits of the selected cached PTE.
    _LAZY_COMB(entry_flags_comb, uint32_t)
        entry_flags_comb = hit_comb_func() ? (uint8_t)flags_reg[hit_index_comb_func()] : 0;
        return entry_flags_comb;
    }

    bool pte_invalid(uint32_t pte)
    {
        return (pte & PTE_V) == 0 || ((pte & PTE_W) != 0 && (pte & PTE_R) == 0);
    }

    bool pte_leaf(uint32_t pte)
    {
        return (pte & (PTE_R | PTE_X)) != 0;
    }

    bool pte_permission_fault(uint32_t pte, bool read, bool write, bool execute, uint32_t priv, bool sum, bool mxr)
    {
        bool user_page;
        user_page = (pte & PTE_U) != 0;
        if ((pte & PTE_A) == 0) { return true; }
        if (write && (pte & PTE_D) == 0) { return true; }
        if (read && (pte & PTE_R) == 0 && !(mxr && (pte & PTE_X) != 0)) { return true; }
        if (write && (pte & PTE_W) == 0) { return true; }
        if (execute && (pte & PTE_X) == 0) { return true; }
        if (priv == 0 && !user_page) { return true; }
        if (priv == 1 && user_page && (execute || !sum)) { return true; }
        return false;
    }

    // Check cached PTE access permissions for load, store, or instruction fetch.
    _LAZY_COMB(permission_fault_comb, bool)
        uint32_t flags;
        bool access;
        permission_fault_comb = false;
        access = read_in() || write_in() || execute_in();
        flags = entry_flags_comb_func();
        if (translation_enabled_comb_func() && access && hit_comb_func()) {
            permission_fault_comb = pte_permission_fault(flags, read_in(), write_in(), execute_in(), priv_in(), sum_in(), mxr_in());
        }
        return permission_fault_comb;
    }

    // New translation request that needs the hardware page-table walker.
    _LAZY_COMB(miss_comb, bool)
        miss_comb = translation_enabled_comb_func() && (read_in() || write_in() || execute_in()) &&
            !hit_comb_func() && !fault_reg;
        return miss_comb;
    }

    // Visible page fault from either a walker failure or cached PTE permission failure.
    // A latched walker fault belongs only to the original request; after a trap redirect
    // the stale fault must not be re-reported for the trap-vector fetch.
    _LAZY_COMB(fault_comb, bool)
        bool access;
        access = read_in() || write_in() || execute_in();
        fault_comb = translation_enabled_comb_func() &&
            access &&
            ((fault_reg && (uint32_t)req_vaddr_reg == vaddr_in()) || permission_fault_comb_func());
        return fault_comb;
    }

    // Physical address from bypass, level-0 page, or level-1 superpage translation.
    _LAZY_COMB(paddr_comb, uint32_t)
        if (!translation_enabled_comb_func()) {
            paddr_comb = vaddr_in();
        }
        else if (hit_comb_func() && !permission_fault_comb_func()) {
            if (level_reg[hit_index_comb_func()]) {
                paddr_comb = (((uint32_t)ppn_reg[hit_index_comb_func()] & ~0x3ffu) << 12) |
                    (vaddr_in() & 0x3fffffu);
            }
            else {
                paddr_comb = ((uint32_t)ppn_reg[hit_index_comb_func()] << 12) | page_offset_comb_func();
            }
        }
        else {
            paddr_comb = vaddr_in();
        }
        return paddr_comb;
    }

    // Page-table walker read request for the active PTE level.
    _LAZY_COMB(mem_read_comb, bool)
        return mem_read_comb = state_reg == ST_READ_L1 || state_reg == ST_READ_L0;
    }

    // PTE address generated from satp root for level 1, then from the level-1 PPN for level 0.
    _LAZY_COMB(mem_addr_comb, uint32_t)
        mem_addr_comb = (((uint32_t)req_satp_reg & 0x3fffffu) << 12) + req_vpn1_comb_func() * 4u;
        if (state_reg == ST_READ_L0) {
            mem_addr_comb = (((uint32_t)l1_pte_reg >> 10) << 12) + req_vpn0_comb_func() * 4u;
        }
        return mem_addr_comb;
    }

    // Stall the requester while a TLB miss is being walked through memory.
    _LAZY_COMB(busy_comb, bool)
        busy_comb = false;
        if (translation_enabled_comb_func() && (read_in() || write_in() || execute_in()) &&
            !hit_comb_func() && !fault_reg) {
            busy_comb = true;
        }
        if (state_reg == ST_READ_L1 || state_reg == ST_READ_L0) {
            busy_comb = true;
        }
        return busy_comb;
    }

    void fill_entry(uint32_t vpn, uint32_t ppn, uint32_t flags, bool level)
    {
#ifndef SYNTHESIS
        if (std::getenv("TRIBE_TRACE_MMU_FILL") != nullptr) {
            std::print("trace-mmu-fill {} vpn={:05x} ppn={:05x} flags={:02x} satp={:08x}\n",
                level ? "l1" : "l0", vpn, ppn, flags & 0xffu, (uint32_t)req_satp_reg);
        }
#endif
        valid_reg._next[victim_reg] = true;
        vpn_reg._next[victim_reg] = vpn;
        ppn_reg._next[victim_reg] = ppn;
        flags_reg._next[victim_reg] = flags;
        level_reg._next[victim_reg] = level;
        satp_tag_reg._next[victim_reg] = req_satp_reg;
        victim_reg._next = victim_reg + 1;
    }

    void handle_pte(uint32_t pte, bool level1)
    {
        uint32_t ppn;
        fault_reg._next = false;
        if (pte_invalid(pte)) {
            fault_reg._next = true;
            state_reg._next = ST_FAULT;
            return;
        }
        if (pte_leaf(pte)) {
            if ((level1 && ((pte >> 10) & 0x3ffu) != 0) ||
                pte_permission_fault(pte, req_read_reg, req_write_reg, req_execute_reg, req_priv_reg, req_sum_reg, req_mxr_reg)) {
                fault_reg._next = true;
                state_reg._next = ST_FAULT;
                return;
            }
            ppn = pte >> 10;
            if (level1) {
                ppn &= ~0x3ffu;
            }
            fill_entry(level1 ? (((uint32_t)req_vaddr_reg >> 12) & ~0x3ffu) : ((uint32_t)req_vaddr_reg >> 12),
                ppn, pte & 0xffu, level1);
            state_reg._next = ST_IDLE;
            return;
        }
        if (level1) {
            l1_pte_reg._next = pte;
            state_reg._next = ST_READ_L0;
            return;
        }
        fault_reg._next = true;
        state_reg._next = ST_FAULT;
    }

public:
    void _work(bool reset)
    {
        if (sfence_in()) {
            valid_reg.clr();
            fault_reg.clr();
            state_reg._next = ST_IDLE;
        }
        if (fill_in()) {
            valid_reg._next[fill_index_in()] = true;
            vpn_reg._next[fill_index_in()] = fill_vpn_in();
            ppn_reg._next[fill_index_in()] = fill_ppn_in();
            flags_reg._next[fill_index_in()] = fill_flags_in();
            level_reg._next[fill_index_in()] = false;
            satp_tag_reg._next[fill_index_in()] = satp_in();
        }
        if (state_reg == ST_IDLE) {
            if (!miss_comb_func()) {
                fault_reg.clr();
            }
            if (miss_comb_func()) {
                req_vaddr_reg._next = vaddr_in();
                req_read_reg._next = read_in();
                req_write_reg._next = write_in();
                req_execute_reg._next = execute_in();
                req_sum_reg._next = sum_in();
                req_mxr_reg._next = mxr_in();
                req_satp_reg._next = satp_in();
                req_priv_reg._next = priv_in();
                fault_reg.clr();
                state_reg._next = ST_READ_L1;
            }
        }
        else if (state_reg == ST_READ_L1) {
            if (!mem_wait_in()) {
                debug_last_addr_reg._next = mem_addr_out();
                debug_last_pte_reg._next = mem_read_data_in();
                handle_pte(mem_read_data_in(), true);
            }
        }
        else if (state_reg == ST_READ_L0) {
            if (!mem_wait_in()) {
                debug_last_addr_reg._next = mem_addr_out();
                debug_last_pte_reg._next = mem_read_data_in();
                handle_pte(mem_read_data_in(), false);
            }
        }
        else if (state_reg == ST_FAULT) {
            if (sfence_in() || !translation_enabled_comb_func() ||
                !(read_in() || write_in() || execute_in()) || vaddr_in() != (uint32_t)req_vaddr_reg) {
                fault_reg.clr();
                state_reg._next = ST_IDLE;
            }
        }
        if (reset) {
            valid_reg.clr();
            vpn_reg.clr();
            ppn_reg.clr();
            flags_reg.clr();
            level_reg.clr();
            satp_tag_reg.clr();
            victim_reg.clr();
            state_reg.clr();
            req_vaddr_reg.clr();
            req_read_reg.clr();
            req_write_reg.clr();
            req_execute_reg.clr();
            req_sum_reg.clr();
            req_mxr_reg.clr();
            req_satp_reg.clr();
            req_priv_reg.clr();
            l1_pte_reg.clr();
            fault_reg.clr();
            debug_last_pte_reg.clr();
            debug_last_addr_reg.clr();
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        valid_reg.strobe(checkpoint_fd);
        vpn_reg.strobe(checkpoint_fd);
        ppn_reg.strobe(checkpoint_fd);
        flags_reg.strobe(checkpoint_fd);
        level_reg.strobe(checkpoint_fd);
        satp_tag_reg.strobe(checkpoint_fd);
        victim_reg.strobe(checkpoint_fd);
        state_reg.strobe(checkpoint_fd);
        req_vaddr_reg.strobe(checkpoint_fd);
        req_read_reg.strobe(checkpoint_fd);
        req_write_reg.strobe(checkpoint_fd);
        req_execute_reg.strobe(checkpoint_fd);
        req_sum_reg.strobe(checkpoint_fd);
        req_mxr_reg.strobe(checkpoint_fd);
        req_satp_reg.strobe(checkpoint_fd);
        req_priv_reg.strobe(checkpoint_fd);
        l1_pte_reg.strobe(checkpoint_fd);
        fault_reg.strobe(checkpoint_fd);
        debug_last_pte_reg.strobe(checkpoint_fd);
        debug_last_addr_reg.strobe(checkpoint_fd);
    }
};
