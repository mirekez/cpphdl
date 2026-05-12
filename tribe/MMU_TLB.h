#pragma once

#include "Config.h"

using namespace cpphdl;

template<size_t ENTRIES = 8>
class MMU_TLB : public Module
{
    static_assert(ENTRIES > 0, "MMU_TLB needs at least one entry");
public:
    _PORT(uint32_t) vaddr_in;
    _PORT(bool)     read_in;
    _PORT(bool)     write_in;
    _PORT(bool)     execute_in;
    _PORT(uint32_t) satp_in;
    _PORT(u<2>)     priv_in;

    _PORT(bool)     fill_in;
    _PORT(u<clog2(ENTRIES)>) fill_index_in;
    _PORT(uint32_t) fill_vpn_in;
    _PORT(uint32_t) fill_ppn_in;
    _PORT(uint8_t)  fill_flags_in;
    _PORT(bool)     sfence_in;

    _PORT(uint32_t) paddr_out = _BIND_VAR(paddr_comb_func());
    _PORT(bool)     translated_out = _BIND_VAR(translation_enabled_comb_func());
    _PORT(bool)     hit_out = _BIND_VAR(hit_comb_func());
    _PORT(bool)     fault_out = _BIND_VAR(fault_comb_func());
    _PORT(bool)     miss_out = _BIND_VAR(miss_comb_func());

private:
    reg<array<u1, ENTRIES>> valid_reg;
    reg<array<u32, ENTRIES>> vpn_reg;
    reg<array<u32, ENTRIES>> ppn_reg;
    reg<array<u8, ENTRIES>> flags_reg;

    _LAZY_COMB(translation_enabled_comb, bool)
        uint32_t mode;
        mode = satp_in() >> 31;
        translation_enabled_comb = mode == 1 && priv_in() != 3;
        return translation_enabled_comb;
    }

    _LAZY_COMB(vpn_comb, uint32_t)
        return vpn_comb = vaddr_in() >> 12;
    }

    _LAZY_COMB(page_offset_comb, uint32_t)
        return page_offset_comb = vaddr_in() & 0xfffu;
    }

    _LAZY_COMB(hit_index_comb, u<clog2(ENTRIES)>)
        size_t i;
        hit_index_comb = 0;
        for (i = 0; i < ENTRIES; ++i) {
            if (valid_reg[i] && (uint32_t)vpn_reg[i] == vpn_comb_func()) {
                hit_index_comb = (u<clog2(ENTRIES)>)i;
            }
        }
        return hit_index_comb;
    }

    _LAZY_COMB(hit_comb, bool)
        size_t i;
        hit_comb = false;
        if (!translation_enabled_comb_func()) {
            hit_comb = true;
        }
        else {
            for (i = 0; i < ENTRIES; ++i) {
                if (valid_reg[i] && (uint32_t)vpn_reg[i] == vpn_comb_func()) {
                    hit_comb = true;
                }
            }
        }
        return hit_comb;
    }

    _LAZY_COMB(entry_flags_comb, uint32_t)
        entry_flags_comb = hit_comb_func() ? (uint8_t)flags_reg[hit_index_comb_func()] : 0;
        return entry_flags_comb;
    }

    _LAZY_COMB(permission_fault_comb, bool)
        uint32_t flags;
        bool user_page;
        flags = entry_flags_comb_func();
        user_page = (flags & (1u << 4)) != 0;
        permission_fault_comb = false;
        if (translation_enabled_comb_func() && hit_comb_func()) {
            if ((flags & (1u << 0)) == 0 || ((flags & (1u << 1)) == 0 && (flags & (1u << 2)) != 0)) {
                permission_fault_comb = true;
            }
            if ((flags & (1u << 6)) == 0) {
                permission_fault_comb = true;
            }
            if (write_in() && (flags & (1u << 7)) == 0) {
                permission_fault_comb = true;
            }
            if (read_in() && (flags & (1u << 1)) == 0) {
                permission_fault_comb = true;
            }
            if (write_in() && (flags & (1u << 2)) == 0) {
                permission_fault_comb = true;
            }
            if (execute_in() && (flags & (1u << 3)) == 0) {
                permission_fault_comb = true;
            }
            if (priv_in() == 0 && !user_page) {
                permission_fault_comb = true;
            }
            if (priv_in() == 1 && user_page) {
                permission_fault_comb = true;
            }
        }
        return permission_fault_comb;
    }

    _LAZY_COMB(miss_comb, bool)
        miss_comb = translation_enabled_comb_func() && (read_in() || write_in() || execute_in()) && !hit_comb_func();
        return miss_comb;
    }

    _LAZY_COMB(fault_comb, bool)
        fault_comb = miss_comb_func() || permission_fault_comb_func();
        return fault_comb;
    }

    _LAZY_COMB(paddr_comb, uint32_t)
        if (!translation_enabled_comb_func()) {
            paddr_comb = vaddr_in();
        }
        else if (hit_comb_func() && !permission_fault_comb_func()) {
            paddr_comb = ((uint32_t)ppn_reg[hit_index_comb_func()] << 12) | page_offset_comb_func();
        }
        else {
            paddr_comb = vaddr_in();
        }
        return paddr_comb;
    }

public:
    void _work(bool reset)
    {
        if (sfence_in()) {
            valid_reg.clr();
        }
        if (fill_in()) {
            valid_reg._next[fill_index_in()] = true;
            vpn_reg._next[fill_index_in()] = fill_vpn_in();
            ppn_reg._next[fill_index_in()] = fill_ppn_in();
            flags_reg._next[fill_index_in()] = fill_flags_in();
        }
        if (reset) {
            valid_reg.clr();
            vpn_reg.clr();
            ppn_reg.clr();
            flags_reg.clr();
        }
    }

    void _strobe()
    {
        valid_reg.strobe();
        vpn_reg.strobe();
        ppn_reg.strobe();
        flags_reg.strobe();
    }
};
