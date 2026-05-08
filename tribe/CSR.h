#pragma once

using namespace cpphdl;

class CSR: public Module
{
public:
    __PORT(State) state_in;
    __PORT(uint32_t) read_data_out = __VAR(read_data_comb_func());
    __PORT(uint32_t) trap_vector_out = __VAR(mtvec_reg);
    __PORT(uint32_t) epc_out = __VAR(mepc_reg);

private:
    static constexpr uint32_t MISA_RV32IMC = 0x40001104u;

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

    reg<u32> dcsr_reg;
    reg<u32> dpc_reg;
    reg<u32> dscratch0_reg;
    reg<u32> dscratch1_reg;

    reg<u64> cycle_reg;
    reg<u64> instret_reg;

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

        if (addr == 0x100) { return sstatus_reg; }
        if (addr == 0x104) { return sie_reg; }
        if (addr == 0x105) { return stvec_reg; }
        if (addr == 0x106) { return scounteren_reg; }
        if (addr == 0x140) { return sscratch_reg; }
        if (addr == 0x141) { return sepc_reg; }
        if (addr == 0x142) { return scause_reg; }
        if (addr == 0x143) { return stval_reg; }
        if (addr == 0x144) { return sip_reg; }
        if (addr == 0x180) { return 0; }                  // satp, excluded

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
        if (addr == 0x344) { return mip_reg; }
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

    bool csr_writes()
    {
        uint32_t op = state_in().csr_op;
        if (!state_in().valid || op == Csr::CNONE) {
            return false;
        }
        if (op == Csr::CSRRS || op == Csr::CSRRC) {
            return state_in().rs1 != 0;
        }
        if (op == Csr::CSRRSI || op == Csr::CSRRCI) {
            return state_in().csr_imm != 0;
        }
        return true;
    }

    void csr_write(uint32_t addr, uint32_t value)
    {
        switch (addr) {
            case 0x100: sstatus_reg._next = value; break;
            case 0x104: sie_reg._next = value; break;
            case 0x105: stvec_reg._next = value; break;
            case 0x106: scounteren_reg._next = value; break;
            case 0x140: sscratch_reg._next = value; break;
            case 0x141: sepc_reg._next = value & ~1u; break;
            case 0x142: scause_reg._next = value; break;
            case 0x143: stval_reg._next = value; break;
            case 0x144: sip_reg._next = value; break;

            case 0x300: mstatus_reg._next = value; break;
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

    __LAZY_COMB(read_data_comb, uint32_t)
        return read_data_comb = csr_read(state_in().csr_addr);
    }

public:
    void _work(bool reset)
    {
        bool inhibit_cycle;
        bool inhibit_instret;
        inhibit_cycle = mcountinhibit_reg & 1;
        inhibit_instret = (mcountinhibit_reg >> 2) & 1;

        cycle_reg._next = inhibit_cycle ? cycle_reg : uint64_t(cycle_reg) + 1;
        instret_reg._next = (inhibit_instret || !state_in().valid) ? instret_reg : uint64_t(instret_reg) + 1;

        if (csr_writes()) {
            csr_write(state_in().csr_addr, csr_write_value(read_data_comb_func()));
        }
        if (state_in().valid && state_in().sys_op == Sys::ECALL) {
            mepc_reg._next = state_in().pc;
            mcause_reg._next = 11;
        }

        if (reset) {
            mstatus_reg.clr();
            mtvec_reg.clr();
            medeleg_reg.clr();
            mideleg_reg.clr();
            mie_reg.clr();
            mscratch_reg.clr();
            mepc_reg.clr();
            mcause_reg.clr();
            mtval_reg.clr();
            mip_reg.clr();
            mcounteren_reg.clr();
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
            dcsr_reg.clr();
            dpc_reg.clr();
            dscratch0_reg.clr();
            dscratch1_reg.clr();
            cycle_reg.clr();
            instret_reg.clr();
        }
    }

    void _strobe()
    {
        mstatus_reg.strobe();
        mtvec_reg.strobe();
        medeleg_reg.strobe();
        mideleg_reg.strobe();
        mie_reg.strobe();
        mscratch_reg.strobe();
        mepc_reg.strobe();
        mcause_reg.strobe();
        mtval_reg.strobe();
        mip_reg.strobe();
        mcounteren_reg.strobe();
        mcountinhibit_reg.strobe();
        mscratchcsw_reg.strobe();
        mscratchcswl_reg.strobe();
        sstatus_reg.strobe();
        stvec_reg.strobe();
        sie_reg.strobe();
        sscratch_reg.strobe();
        sepc_reg.strobe();
        scause_reg.strobe();
        stval_reg.strobe();
        sip_reg.strobe();
        scounteren_reg.strobe();
        dcsr_reg.strobe();
        dpc_reg.strobe();
        dscratch0_reg.strobe();
        dscratch1_reg.strobe();
        cycle_reg.strobe();
        instret_reg.strobe();
    }

    void _assign()
    {
    }
};
