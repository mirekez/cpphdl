#pragma once

#include "cpphdl.h"
#include "Config.h"

using namespace cpphdl;

class InterruptController : public Module
{
    static constexpr uint32_t MSTATUS_SIE = 1u << 1;
    static constexpr uint32_t MSTATUS_MIE = 1u << 3;
    static constexpr uint32_t PRIV_S = 1;
    static constexpr uint32_t PRIV_M = 3;

    static constexpr uint32_t IRQ_SSIP = 1;
    static constexpr uint32_t IRQ_MSIP = 3;
    static constexpr uint32_t IRQ_STIP = 5;
    static constexpr uint32_t IRQ_MTIP = 7;
    static constexpr uint32_t IRQ_SEIP = 9;
    static constexpr uint32_t IRQ_MEIP = 11;

public:
    __PORT(uint32_t) mstatus_in;
    __PORT(uint32_t) mie_in;
    __PORT(uint32_t) mideleg_in;
    __PORT(uint32_t) mip_sw_in;
    __PORT(u<2>) priv_in;
    __PORT(bool) clint_msip_in;
    __PORT(bool) clint_mtip_in;

    __PORT(uint32_t) mip_out = __VAR(mip_comb_func());
    __PORT(bool) interrupt_valid_out = __VAR(interrupt_valid_comb_func());
    __PORT(uint32_t) interrupt_cause_out = __VAR(interrupt_cause_comb_func());
    __PORT(bool) interrupt_to_supervisor_out = __VAR(interrupt_to_supervisor_comb_func());

private:
    // Hardware interrupt pending bits merged with writable software MIP bits.
    __LAZY_COMB(mip_comb, uint32_t)
        mip_comb = mip_sw_in();
#ifdef ENABLE_ISR
        if (clint_msip_in()) {
            mip_comb |= 1u << IRQ_MSIP;
        }
        if (clint_mtip_in()) {
            mip_comb |= 1u << IRQ_MTIP;
        }
#endif
        return mip_comb;
    }

    // Interrupts currently pending and enabled at the CSR level.
    __LAZY_COMB(enabled_pending_comb, uint32_t)
        return enabled_pending_comb = mip_comb_func() & mie_in();
    }

    // Pick one pending interrupt cause. Machine timer/software are enough for CLINT;
    // supervisor and external bits are routed if tests set them through MIP.
    __LAZY_COMB(interrupt_cause_comb, uint32_t)
        uint32_t pending;
        pending = enabled_pending_comb_func();
        interrupt_cause_comb = 0;
        if (pending & (1u << IRQ_MEIP)) {
            interrupt_cause_comb = IRQ_MEIP;
        }
        else if (pending & (1u << IRQ_MSIP)) {
            interrupt_cause_comb = IRQ_MSIP;
        }
        else if (pending & (1u << IRQ_MTIP)) {
            interrupt_cause_comb = IRQ_MTIP;
        }
        else if (pending & (1u << IRQ_SEIP)) {
            interrupt_cause_comb = IRQ_SEIP;
        }
        else if (pending & (1u << IRQ_SSIP)) {
            interrupt_cause_comb = IRQ_SSIP;
        }
        else if (pending & (1u << IRQ_STIP)) {
            interrupt_cause_comb = IRQ_STIP;
        }
        return interrupt_cause_comb;
    }

    // Delegated interrupt causes are taken in S-mode when the current privilege allows it.
    __LAZY_COMB(interrupt_to_supervisor_comb, bool)
        uint32_t cause;
        cause = interrupt_cause_comb_func();
        interrupt_to_supervisor_comb = false;
#ifdef ENABLE_ISR
        if (cause != 0 && priv_in() != PRIV_M && ((mideleg_in() >> cause) & 1u)) {
            interrupt_to_supervisor_comb = true;
        }
#endif
        return interrupt_to_supervisor_comb;
    }

    // Global enable is privilege-aware: higher-privilege interrupts can preempt lower modes.
    __LAZY_COMB(interrupt_valid_comb, bool)
        uint32_t cause;
        bool to_s;
        bool global_enable;
        cause = interrupt_cause_comb_func();
        to_s = interrupt_to_supervisor_comb_func();
        global_enable = false;
#ifdef ENABLE_ISR
        if (cause != 0) {
            if (to_s) {
                global_enable = priv_in() < PRIV_S || (mstatus_in() & MSTATUS_SIE);
            }
            else {
                global_enable = priv_in() < PRIV_M || (mstatus_in() & MSTATUS_MIE);
            }
        }
#endif
        return interrupt_valid_comb = cause != 0 && global_enable;
    }

public:
    void _work(bool reset) {}
    void _strobe() {}
};
