#pragma once

#include "Config.h"

using namespace cpphdl;

class CSR: public Module
{
public:
    _PORT(State) state_in;
    _PORT(State) trap_check_state_in;
    _PORT(u<2>) reset_priv_in = _ASSIGN((u<2>)3);
    _PORT(bool) interrupt_valid_in;
    _PORT(uint32_t) interrupt_cause_in;
    _PORT(bool) interrupt_to_supervisor_in;
    _PORT(uint32_t) irq_pending_bits_in;
    _PORT(uint32_t) read_data_out = _ASSIGN_COMB(read_data_comb_func());
    _PORT(uint32_t) trap_vector_out = _ASSIGN_COMB(trap_vector_comb_func());
    _PORT(uint32_t) epc_out = _ASSIGN_COMB(epc_comb_func());
    _PORT(uint32_t) mepc_out = _ASSIGN_REG(mepc_reg);
    _PORT(uint32_t) mtvec_out = _ASSIGN_REG(mtvec_reg);
    _PORT(uint32_t) mcause_out = _ASSIGN_REG(mcause_reg);
    _PORT(uint32_t) mtval_out = _ASSIGN_REG(mtval_reg);
    _PORT(uint32_t) sepc_out = _ASSIGN_REG(sepc_reg);
    _PORT(uint32_t) stvec_out = _ASSIGN_REG(stvec_reg);
    _PORT(uint32_t) scause_out = _ASSIGN_REG(scause_reg);
    _PORT(uint32_t) stval_out = _ASSIGN_REG(stval_reg);
    _PORT(bool) illegal_trap_out = _ASSIGN_COMB(illegal_trap_comb_func());
    _PORT(uint32_t) mstatus_out = _ASSIGN_REG(mstatus_reg);
    _PORT(uint32_t) mie_out = _ASSIGN_REG(mie_reg);
    _PORT(uint32_t) mideleg_out = _ASSIGN_REG(mideleg_reg);
    _PORT(uint32_t) mip_sw_out = _ASSIGN_REG(mip_reg);
#ifdef ENABLE_MMU_TLB
    _PORT(uint32_t) satp_out = _ASSIGN_REG(satp_reg);
#else
    _PORT(uint32_t) satp_out = _ASSIGN((uint32_t)0);
#endif
#ifdef ENABLE_TRAPS
    _PORT(u<2>) priv_out = _ASSIGN_REG(priv_reg);
#else
    _PORT(u<2>) priv_out = _ASSIGN((u<2>)3);
#endif

private:
    static constexpr uint32_t MISA_RV32IMC =
        0x40000000u | (1u << ('I' - 'A')) | (1u << ('M' - 'A')) | (1u << ('C' - 'A'))
#ifdef ENABLE_RV32IA
        | (1u << ('A' - 'A'))
#endif
        ;

    static constexpr uint32_t PRIV_U = 0;
    static constexpr uint32_t PRIV_S = 1;
    static constexpr uint32_t PRIV_M = 3;

    static constexpr uint32_t MSTATUS_UIE  = 1u << 0;
    static constexpr uint32_t MSTATUS_SIE  = 1u << 1;
    static constexpr uint32_t MSTATUS_MIE  = 1u << 3;
    static constexpr uint32_t MSTATUS_UPIE = 1u << 4;
    static constexpr uint32_t MSTATUS_SPIE = 1u << 5;
    static constexpr uint32_t MSTATUS_MPIE = 1u << 7;
    static constexpr uint32_t MSTATUS_SPP  = 1u << 8;
    static constexpr uint32_t MSTATUS_MPP_SHIFT = 11;
    static constexpr uint32_t MSTATUS_MPP_MASK = 3u << MSTATUS_MPP_SHIFT;
    static constexpr uint32_t MSTATUS_WRITABLE =
        MSTATUS_UIE | MSTATUS_SIE | MSTATUS_MIE |
        MSTATUS_UPIE | MSTATUS_SPIE | MSTATUS_MPIE |
        MSTATUS_SPP | MSTATUS_MPP_MASK |
        (3u << 13) | (3u << 15) | (3u << 17) | // FS/XS/MPRV-ish writable enough for tests
        (1u << 18) | (1u << 19);
    static constexpr uint32_t SSTATUS_MASK =
        MSTATUS_UIE | MSTATUS_SIE | MSTATUS_UPIE | MSTATUS_SPIE |
        MSTATUS_SPP | (3u << 13) | (3u << 15) | (1u << 18);

    reg<u32> mstatus_reg;
    reg<u32> mtvec_reg;
    reg<u32> medeleg_reg;
    reg<u32> mideleg_reg;
    reg<u32> mie_reg;
    reg<u32> mscratch_reg;
    reg<u32> mepc_reg;
    reg<u32> mcause_reg;
    reg<u32> mtval_reg;
    reg<u32> mip_reg;
    reg<u32> mcounteren_reg;
    reg<u32> mcountinhibit_reg;
    reg<u32> mscratchcsw_reg;
    reg<u32> mscratchcswl_reg;

    reg<u32> sstatus_reg;
    reg<u32> stvec_reg;
    reg<u32> sie_reg;
    reg<u32> sscratch_reg;
    reg<u32> sepc_reg;
    reg<u32> scause_reg;
    reg<u32> stval_reg;
    reg<u32> sip_reg;
    reg<u32> scounteren_reg;
#ifdef ENABLE_MMU_TLB
    reg<u32> satp_reg;
#endif

    reg<u32> dcsr_reg;
    reg<u32> dpc_reg;
    reg<u32> dscratch0_reg;
    reg<u32> dscratch1_reg;

    reg<u64> cycle_reg;
    reg<u64> instret_reg;
#ifdef ENABLE_TRAPS
    reg<u<2>> priv_reg;
#endif

    uint32_t sanitize_mstatus(uint32_t value)
    {
        return value & MSTATUS_WRITABLE;
    }

    uint32_t trap_cause_code()
    {
#ifdef ENABLE_ISR
        if (interrupt_valid_in()) {
            return interrupt_cause_in();
        }
#endif
        if (state_in().sys_op == Sys::ECALL) {
#ifdef ENABLE_TRAPS
            if (priv_reg == PRIV_U) { return 8; }
            if (priv_reg == PRIV_S) { return 9; }
#endif
            return 11;
        }
        if (state_in().sys_op == Sys::EBREAK) { return 3; }
        switch (state_in().trap_op) {
            case Trap::TNONE: return 2;
            case Trap::INST_MISALIGNED: return 0;
            case Trap::ILLEGAL_INST: return 2;
            case Trap::BREAKPOINT: return 3;
            case Trap::LOAD_MISALIGNED: return 4;
            case Trap::STORE_MISALIGNED: return 6;
            case Trap::INST_PAGE_FAULT: return 12;
            case Trap::LOAD_PAGE_FAULT: return 13;
            case Trap::STORE_PAGE_FAULT: return 15;
            case Trap::ECALL_U: return 8;
            case Trap::ECALL_S: return 9;
            case Trap::ECALL_M: return 11;
            default: return 2;
        }
        return 2;
    }

    bool trap_to_supervisor(uint32_t cause)
    {
#ifdef ENABLE_ISR
        if (interrupt_valid_in()) {
            return interrupt_to_supervisor_in();
        }
#endif
#ifdef ENABLE_TRAPS
        return priv_reg != PRIV_M && ((medeleg_reg >> cause) & 1u);
#else
        return false;
#endif
    }

    _LAZY_COMB(trap_vector_comb, uint32_t)
        uint32_t cause;
        uint32_t tvec;
        cause = trap_cause_code();
        tvec = trap_to_supervisor(cause) ? (uint32_t)stvec_reg : (uint32_t)mtvec_reg;
        trap_vector_comb = tvec & ~3u;
#ifdef ENABLE_ISR
        if (interrupt_valid_in() && ((tvec & 3u) == 1u)) {
            trap_vector_comb = (tvec & ~3u) + cause * 4u;
        }
#endif
        return trap_vector_comb;
    }

    _LAZY_COMB(epc_comb, uint32_t)
        epc_comb = mepc_reg;
#ifdef ENABLE_TRAPS
        if (state_in().sys_op == Sys::SRET) {
            epc_comb = sepc_reg;
        }
#endif
        return epc_comb;
    }

    _LAZY_COMB(illegal_trap_comb, bool)
        illegal_trap_comb = state_causes_illegal_trap(trap_check_state_in());
        return illegal_trap_comb;
    }

    uint32_t csr_read(uint32_t addr)
    {
        uint64_t cycle_value;
        uint64_t instret_value;
        cycle_value = cycle_reg;
        instret_value = instret_reg;

        if (addr == 0x001) { return 0; }                  // fflags, excluded
        if (addr == 0x002) { return 0; }                  // frm, excluded
        if (addr == 0x003) { return 0; }                  // fcsr, excluded

        if (addr == 0xC00 || addr == 0xB00) { return (uint32_t)cycle_value; }
        if (addr == 0xC80 || addr == 0xB80) { return (uint32_t)(cycle_value >> 32); }
        if (addr == 0xC01) { return (uint32_t)cycle_value; }
        if (addr == 0xC81) { return (uint32_t)(cycle_value >> 32); }
        if (addr == 0xC02 || addr == 0xB02) { return (uint32_t)instret_value; }
        if (addr == 0xC82 || addr == 0xB82) { return (uint32_t)(instret_value >> 32); }
        if ((addr >= 0xC03 && addr <= 0xC1F) || (addr >= 0xC83 && addr <= 0xC9F) ||
            (addr >= 0xB03 && addr <= 0xB1F) || (addr >= 0xB83 && addr <= 0xB9F)) {
            return 0;
        }

        if (addr == 0x100) { return mstatus_reg & SSTATUS_MASK; }
        if (addr == 0x104) { return sie_reg; }
        if (addr == 0x105) { return stvec_reg; }
        if (addr == 0x106) { return scounteren_reg; }
        if (addr == 0x140) { return sscratch_reg; }
        if (addr == 0x141) { return sepc_reg; }
        if (addr == 0x142) { return scause_reg; }
        if (addr == 0x143) { return stval_reg; }
        if (addr == 0x144) { return (sip_reg | irq_pending_bits_in()) & ((1u << 1) | (1u << 5) | (1u << 9)); }
        if (addr == 0x180) {
#ifdef ENABLE_MMU_TLB
            return satp_reg;
#else
            return 0;
#endif
        }

        if (addr == 0x300) { return mstatus_reg; }
        if (addr == 0x301) { return MISA_RV32IMC; }
        if (addr == 0x302) { return medeleg_reg; }
        if (addr == 0x303) { return mideleg_reg; }
        if (addr == 0x304) { return mie_reg; }
        if (addr == 0x305) { return mtvec_reg; }
        if (addr == 0x306) { return mcounteren_reg; }
        if (addr == 0x310) { return mstatus_reg >> 32; }  // mstatush on RV32
        if (addr == 0x320) { return mcountinhibit_reg; }
        if (addr == 0x340) { return mscratch_reg; }
        if (addr == 0x341) { return mepc_reg; }
        if (addr == 0x342) { return mcause_reg; }
        if (addr == 0x343) { return mtval_reg; }
        if (addr == 0x344) { return mip_reg | irq_pending_bits_in(); }
        if (addr == 0x348) { return mscratchcsw_reg; }
        if (addr == 0x349) { return mscratchcswl_reg; }
        if (addr == 0xF11) { return 0; }                  // mvendorid
        if (addr == 0xF12) { return 0; }                  // marchid
        if (addr == 0xF13) { return 0; }                  // mimpid
        if (addr == 0xF14) { return 0; }                  // mhartid
        if (addr == 0xF15) { return 0; }                  // mconfigptr

        if (addr == 0x7B0) { return dcsr_reg; }
        if (addr == 0x7B1) { return dpc_reg; }
        if (addr == 0x7B2) { return dscratch0_reg; }
        if (addr == 0x7B3) { return dscratch1_reg; }

        if ((addr >= 0x323 && addr <= 0x33F) ||           // mhpmevent3-31
            (addr >= 0x3A0 && addr <= 0x3EF) ||           // pmpcfg/pmpaddr, excluded
            (addr >= 0x5A8 && addr <= 0x5AF) ||           // sstateen, excluded
            (addr >= 0x7C0 && addr <= 0x7FF) ||           // custom/debug window
            (addr >= 0x9C0 && addr <= 0x9FF) ||           // supervisor custom
            (addr >= 0xA00 && addr <= 0xAFF)) {           // hypervisor, excluded
            return 0;
        }

        return 0;
    }

    uint32_t csr_write_value(uint32_t old_value)
    {
        uint32_t mask = state_in().rs1_val;
        if (state_in().csr_op == Csr::CSRRWI ||
            state_in().csr_op == Csr::CSRRSI ||
            state_in().csr_op == Csr::CSRRCI) {
            mask = state_in().csr_imm;
        }

        if (state_in().csr_op == Csr::CSRRW || state_in().csr_op == Csr::CSRRWI) {
            return mask;
        }
        if (state_in().csr_op == Csr::CSRRS || state_in().csr_op == Csr::CSRRSI) {
            return old_value | mask;
        }
        if (state_in().csr_op == Csr::CSRRC || state_in().csr_op == Csr::CSRRCI) {
            return old_value & ~mask;
        }
        return old_value;
    }

    bool csr_state_writes(State st)
    {
        uint32_t op = st.csr_op;
        if (!st.valid || op == Csr::CNONE) {
            return false;
        }
        if (op == Csr::CSRRS || op == Csr::CSRRC) {
            return st.rs1 != 0;
        }
        if (op == Csr::CSRRSI || op == Csr::CSRRCI) {
            return st.csr_imm != 0;
        }
        return true;
    }

    bool csr_supported(uint32_t addr)
    {
        if (addr == 0x001 || addr == 0x002 || addr == 0x003) { return true; }

        if (addr == 0xC00 || addr == 0xC80 || addr == 0xC01 || addr == 0xC81 ||
            addr == 0xC02 || addr == 0xC82 || addr == 0xB00 || addr == 0xB80 ||
            addr == 0xB02 || addr == 0xB82) {
            return true;
        }
        if ((addr >= 0xC03 && addr <= 0xC1F) || (addr >= 0xC83 && addr <= 0xC9F) ||
            (addr >= 0xB03 && addr <= 0xB1F) || (addr >= 0xB83 && addr <= 0xB9F)) {
            return true;
        }

        if ((addr >= 0x100 && addr <= 0x106) || (addr >= 0x140 && addr <= 0x144) ||
            addr == 0x180) {
            return true;
        }

        if ((addr >= 0x300 && addr <= 0x306) || addr == 0x310 || addr == 0x320 ||
            (addr >= 0x340 && addr <= 0x344) || addr == 0x348 || addr == 0x349 ||
            (addr >= 0x323 && addr <= 0x33F) || (addr >= 0x3A0 && addr <= 0x3EF)) {
            return true;
        }

        if (addr >= 0xF11 && addr <= 0xF15) { return true; }
        if (addr >= 0x7B0 && addr <= 0x7B3) { return true; }
        if ((addr >= 0x5A8 && addr <= 0x5AF) || (addr >= 0x7C0 && addr <= 0x7FF) ||
            (addr >= 0x9C0 && addr <= 0x9FF) || (addr >= 0xA00 && addr <= 0xAFF)) {
            return true;
        }

        return false;
    }

public:
    bool state_causes_illegal_trap(State st)
    {
        uint32_t addr;
        uint32_t csr_priv;
        uint32_t index;
#ifdef ENABLE_TRAPS
        if (!st.valid) {
            return false;
        }
        if (st.sys_op == Sys::MRET) {
            return priv_reg != PRIV_M;
        }
        if (st.sys_op == Sys::SRET) {
            return priv_reg < PRIV_S;
        }
#ifdef ENABLE_MMU_TLB
        if (st.sys_op == Sys::SFENCE_VMA) {
            return priv_reg < PRIV_S;
        }
#endif
        if (st.csr_op == Csr::CNONE) {
            return false;
        }

        addr = st.csr_addr;
        csr_priv = (addr >> 8) & 3u;
        if (csr_priv > priv_reg) {
            return true;
        }
        if (((addr >> 10) & 3u) == 3u && csr_state_writes(st)) {
            return true;
        }
        if (!csr_supported(addr)) {
            return true;
        }
        if (priv_reg != PRIV_M && addr >= 0xC00 && addr <= 0xC9F) {
            index = addr & 0x1fu;
            if (((mcounteren_reg >> index) & 1u) == 0) {
                return true;
            }
        }
#endif
        return false;
    }

private:
    bool sync_trap()
    {
        return state_in().valid && (
#ifdef ENABLE_ISR
            interrupt_valid_in() ||
#endif
            state_in().sys_op == Sys::ECALL ||
            state_in().sys_op == Sys::EBREAK ||
            state_in().sys_op == Sys::TRAP ||
            state_in().trap_op != Trap::TNONE ||
            state_causes_illegal_trap(state_in()));
    }

    bool csr_writes()
    {
        if (state_causes_illegal_trap(state_in())) {
            return false;
        }
        return csr_state_writes(state_in());
    }

    void csr_write(uint32_t addr, uint32_t value)
    {
        switch (addr) {
            case 0x100: mstatus_reg._next = (mstatus_reg & ~SSTATUS_MASK) | (value & SSTATUS_MASK); sstatus_reg._next = value & SSTATUS_MASK; break;
            case 0x104: sie_reg._next = value; break;
            case 0x105: stvec_reg._next = value; break;
            case 0x106: scounteren_reg._next = value; break;
            case 0x140: sscratch_reg._next = value; break;
            case 0x141: sepc_reg._next = value & ~1u; break;
            case 0x142: scause_reg._next = value; break;
            case 0x143: stval_reg._next = value; break;
            case 0x144: sip_reg._next = value; break;
#ifdef ENABLE_MMU_TLB
            case 0x180: satp_reg._next = value & 0x803fffffu; break;
#endif

            case 0x300: mstatus_reg._next = sanitize_mstatus(value); sstatus_reg._next = value & SSTATUS_MASK; break;
            case 0x302: medeleg_reg._next = value; break;
            case 0x303: mideleg_reg._next = value; break;
            case 0x304: mie_reg._next = value; break;
            case 0x305: mtvec_reg._next = value; break;
            case 0x306: mcounteren_reg._next = value; break;
            case 0x320: mcountinhibit_reg._next = value; break;
            case 0x340: mscratch_reg._next = value; break;
            case 0x341: mepc_reg._next = value & ~1u; break;
            case 0x342: mcause_reg._next = value; break;
            case 0x343: mtval_reg._next = value; break;
            case 0x344: mip_reg._next = value; break;
            case 0x348: mscratchcsw_reg._next = value; break;
            case 0x349: mscratchcswl_reg._next = value; break;

            case 0xB00: cycle_reg._next = (uint64_t(cycle_reg) & 0xffffffff00000000ull) | value; break;
            case 0xB80: cycle_reg._next = (uint64_t(value) << 32) | (uint32_t)cycle_reg; break;
            case 0xB02: instret_reg._next = (uint64_t(instret_reg) & 0xffffffff00000000ull) | value; break;
            case 0xB82: instret_reg._next = (uint64_t(value) << 32) | (uint32_t)instret_reg; break;

            case 0x7B0: dcsr_reg._next = value; break;
            case 0x7B1: dpc_reg._next = value & ~1u; break;
            case 0x7B2: dscratch0_reg._next = value; break;
            case 0x7B3: dscratch1_reg._next = value; break;
        }
    }

    _LAZY_COMB(read_data_comb, uint32_t)
        return read_data_comb = csr_read(state_in().csr_addr);
    }

public:
    void _work(bool reset)
    {
        bool inhibit_cycle;
        bool inhibit_instret;
        uint32_t cause;
        uint32_t tval;
        bool is_interrupt;
        bool to_s;
        bool trace_csr_events;
        inhibit_cycle = mcountinhibit_reg & 1;
        inhibit_instret = (mcountinhibit_reg >> 2) & 1;
        trace_csr_events = std::getenv("TRIBE_TRACE_CSR_EVENTS") != nullptr;

        cycle_reg._next = inhibit_cycle ? cycle_reg : uint64_t(cycle_reg) + 1;
        instret_reg._next = (inhibit_instret || !state_in().valid) ? instret_reg : uint64_t(instret_reg) + 1;

        if (csr_writes()) {
            if (trace_csr_events &&
                (state_in().csr_addr == 0x100 || state_in().csr_addr == 0x141 || state_in().csr_addr == 0x180)) {
                std::print("trace-csr-write pc={:08x} addr={:03x} old={:08x} new={:08x} priv={}\n",
                    state_in().pc, (uint32_t)state_in().csr_addr, read_data_comb_func(),
                    csr_write_value(read_data_comb_func()), (uint32_t)priv_reg);
            }
            csr_write(state_in().csr_addr, csr_write_value(read_data_comb_func()));
        }

#ifdef ENABLE_TRAPS
        if (sync_trap()) {
            cause = trap_cause_code();
            is_interrupt = false;
#ifdef ENABLE_ISR
            is_interrupt = interrupt_valid_in();
#endif
            tval = (!is_interrupt && (cause == 2 || cause == 12 || cause == 13 || cause == 15)) ? state_in().imm : 0;
            to_s = trap_to_supervisor(cause);

            if (to_s) {
                if (trace_csr_events) {
                    std::print("trace-trap-to-s pc={:08x} cause={} tval={:08x} priv={} stvec={:08x}\n",
                        state_in().pc, cause, tval, (uint32_t)priv_reg, (uint32_t)stvec_reg);
                }
                sepc_reg._next = state_in().pc & ~1u;
                scause_reg._next = is_interrupt ? (cause | 0x80000000u) : cause;
                stval_reg._next = tval;
                mstatus_reg._next =
                    (mstatus_reg & ~(MSTATUS_SPIE | MSTATUS_SIE | MSTATUS_SPP)) |
                    ((mstatus_reg & MSTATUS_SIE) ? MSTATUS_SPIE : 0) |
                    ((priv_reg == PRIV_S) ? MSTATUS_SPP : 0);
                priv_reg._next = PRIV_S;
            }
            else {
                mepc_reg._next = state_in().pc & ~1u;
                mcause_reg._next = is_interrupt ? (cause | 0x80000000u) : cause;
                mtval_reg._next = tval;
                mstatus_reg._next =
                    (mstatus_reg & ~(MSTATUS_MPIE | MSTATUS_MIE | MSTATUS_MPP_MASK)) |
                    ((mstatus_reg & MSTATUS_MIE) ? MSTATUS_MPIE : 0) |
                    ((uint32_t)priv_reg << MSTATUS_MPP_SHIFT);
                priv_reg._next = PRIV_M;
            }
        }
        if (state_in().valid && state_in().sys_op == Sys::MRET) {
            uint32_t mpp;
            uint32_t mie_restore;
            mpp = (mstatus_reg & MSTATUS_MPP_MASK) >> MSTATUS_MPP_SHIFT;
            mie_restore = (mstatus_reg & MSTATUS_MPIE) ? MSTATUS_MIE : 0;
            priv_reg._next = mpp;
            mstatus_reg._next =
                ((mstatus_reg & ~MSTATUS_MIE) | mie_restore | MSTATUS_MPIE) & ~MSTATUS_MPP_MASK;
        }
        if (state_in().valid && state_in().sys_op == Sys::SRET) {
            uint32_t spp;
            uint32_t sie_restore;
            spp = (mstatus_reg & MSTATUS_SPP) ? PRIV_S : PRIV_U;
            sie_restore = (mstatus_reg & MSTATUS_SPIE) ? MSTATUS_SIE : 0;
            if (trace_csr_events) {
                std::print("trace-sret pc={:08x} sepc={:08x} mstatus={:08x} next_priv={}\n",
                    state_in().pc, (uint32_t)sepc_reg, (uint32_t)mstatus_reg, spp);
            }
            priv_reg._next = spp;
            mstatus_reg._next = ((mstatus_reg & ~MSTATUS_SIE) | sie_restore | MSTATUS_SPIE) & ~MSTATUS_SPP;
        }
#else
        if (state_in().valid && state_in().sys_op == Sys::ECALL) {
            mepc_reg._next = state_in().pc;
            mcause_reg._next = 11;
        }
#endif

        if (reset) {
            mstatus_reg.clr();
            mtvec_reg.clr();
            if (reset_priv_in() == PRIV_S) {
                medeleg_reg._next =
                    (1u << 0) |  // instruction address misaligned
                    (1u << 1) |  // instruction access fault
                    (1u << 3) |  // breakpoint
                    (1u << 4) |  // load address misaligned
                    (1u << 5) |  // load access fault
                    (1u << 6) |  // store address misaligned
                    (1u << 7) |  // store access fault
                    (1u << 8) |  // ecall from U-mode
                    (1u << 12) | // instruction page fault
                    (1u << 13) | // load page fault
                    (1u << 15);  // store page fault
                mideleg_reg._next = (1u << 1) | (1u << 5) | (1u << 9);
                mcounteren_reg._next = 0xffffffffu;
            }
            else {
                medeleg_reg.clr();
                mideleg_reg.clr();
                mcounteren_reg.clr();
            }
            mie_reg.clr();
            mscratch_reg.clr();
            mepc_reg.clr();
            mcause_reg.clr();
            mtval_reg.clr();
            mip_reg.clr();
            mcountinhibit_reg.clr();
            mscratchcsw_reg.clr();
            mscratchcswl_reg.clr();
            sstatus_reg.clr();
            stvec_reg.clr();
            sie_reg.clr();
            sscratch_reg.clr();
            sepc_reg.clr();
            scause_reg.clr();
            stval_reg.clr();
            sip_reg.clr();
            scounteren_reg.clr();
#ifdef ENABLE_MMU_TLB
            satp_reg.clr();
#endif
            dcsr_reg.clr();
            dpc_reg.clr();
            dscratch0_reg.clr();
            dscratch1_reg.clr();
            cycle_reg.clr();
            instret_reg.clr();
#ifdef ENABLE_TRAPS
            priv_reg._next = reset_priv_in();
#endif
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        mstatus_reg.strobe(checkpoint_fd);
        mtvec_reg.strobe(checkpoint_fd);
        medeleg_reg.strobe(checkpoint_fd);
        mideleg_reg.strobe(checkpoint_fd);
        mie_reg.strobe(checkpoint_fd);
        mscratch_reg.strobe(checkpoint_fd);
        mepc_reg.strobe(checkpoint_fd);
        mcause_reg.strobe(checkpoint_fd);
        mtval_reg.strobe(checkpoint_fd);
        mip_reg.strobe(checkpoint_fd);
        mcounteren_reg.strobe(checkpoint_fd);
        mcountinhibit_reg.strobe(checkpoint_fd);
        mscratchcsw_reg.strobe(checkpoint_fd);
        mscratchcswl_reg.strobe(checkpoint_fd);
        sstatus_reg.strobe(checkpoint_fd);
        stvec_reg.strobe(checkpoint_fd);
        sie_reg.strobe(checkpoint_fd);
        sscratch_reg.strobe(checkpoint_fd);
        sepc_reg.strobe(checkpoint_fd);
        scause_reg.strobe(checkpoint_fd);
        stval_reg.strobe(checkpoint_fd);
        sip_reg.strobe(checkpoint_fd);
        scounteren_reg.strobe(checkpoint_fd);
#ifdef ENABLE_MMU_TLB
        satp_reg.strobe(checkpoint_fd);
#endif
        dcsr_reg.strobe(checkpoint_fd);
        dpc_reg.strobe(checkpoint_fd);
        dscratch0_reg.strobe(checkpoint_fd);
        dscratch1_reg.strobe(checkpoint_fd);
        cycle_reg.strobe(checkpoint_fd);
        instret_reg.strobe(checkpoint_fd);
#ifdef ENABLE_TRAPS
        priv_reg.strobe(checkpoint_fd);
#endif
    }

    void _assign()
    {
    }
};
