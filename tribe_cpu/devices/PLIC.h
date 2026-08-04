#pragma once

#include "cpphdl.h"
#include "Axi4.h"
#include <cstdlib>
#include <cstdio>
#include <print>

using namespace cpphdl;

extern long _system_clock;

template<size_t ADDR_WIDTH = 32, size_t ID_WIDTH = 4, size_t DATA_WIDTH = 256, size_t SOURCES = 32>
class PLIC : public Module
{
public:
    static constexpr uint32_t PRIORITY_BASE = 0x000000;
    static constexpr uint32_t PENDING_BASE = 0x001000;
    static constexpr uint32_t ENABLE_BASE = 0x002000;
    static constexpr uint32_t CONTEXT_BASE = 0x200000;
    static constexpr uint32_t CONTEXT_STRIDE = 0x1000;
    static constexpr uint32_t ENABLE_STRIDE = 0x80;
    static constexpr uint32_t CLAIM_OFFSET = 0x4;
#ifdef MULTICORE
    static constexpr size_t HART_COUNT = CPUS_PER_L2_CACHE;
#else
    static constexpr size_t HART_COUNT = 1;
#endif

    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> axi_in;

    _PORT(bool) source_irq_in[SOURCES];
    _PORT(bool) external_irq_out = _ASSIGN_COMB(external_irq_comb_func());
#ifdef MULTICORE
    _PORT(bool) external_irq_per_hart_out[HART_COUNT];
#endif

private:
    reg<u<ADDR_WIDTH>> read_addr_reg;
    reg<u<ID_WIDTH>> read_id_reg;
    reg<u1> read_valid_reg;
    reg<logic<DATA_WIDTH>> read_data_reg;
    reg<u<ADDR_WIDTH>> write_addr_reg;
    reg<u<ID_WIDTH>> write_id_reg;
    reg<u1> write_addr_valid_reg;
    reg<u1> write_resp_valid_reg;

    reg<u32> priority_reg[SOURCES];
    reg<u32> enable_reg[HART_COUNT];
    reg<u32> threshold_reg[HART_COUNT];
    reg<u32> pending_reg;
    reg<u32> gateway_busy_reg;

    // Current level-sensitive source state. The gateway logic below turns this
    // level into one pending request and blocks repeats until claim completion.
    _LAZY_COMB(source_bits_comb, uint32_t)
        size_t i;
        source_bits_comb = 0;
        for (i = 1; i < SOURCES && i < 32; ++i) {
            if (source_irq_in[i]()) {
                source_bits_comb |= 1u << i;
            }
        }
        return source_bits_comb;
    }

    _LAZY_COMB(pending_bits_comb, uint32_t)
        return pending_bits_comb = pending_reg;
    }

    // Pick the lowest enabled pending source with priority above one context's threshold.
    uint32_t claim_for_context(uint32_t context)
    {
        size_t i;
        uint32_t pending;
        uint32_t claim;
        claim = 0;
        pending = pending_bits_comb_func() & enable_reg[context];
        for (i = 1; i < SOURCES && i < 32; ++i) {
            if (claim == 0 && ((pending >> i) & 1u) &&
                (uint32_t)priority_reg[i] > (uint32_t)threshold_reg[context]) {
                claim = i;
            }
        }
        return claim;
    }

    _LAZY_COMB(claim_comb, uint32_t)
        return claim_comb = claim_for_context(0);
    }

    // One hart/context PLIC model. Linux sees this as the S-mode external IRQ line.
    _LAZY_COMB(external_irq_comb, bool)
        return external_irq_comb = claim_comb_func() != 0;
    }

    // Sparse PLIC register read map used by debug traces. The actual AXI read
    // response is latched on AR accept so a claim read returns the same IRQ ID
    // that it consumes.
    _LAZY_COMB(read_data_comb, logic<DATA_WIDTH>)
        uint32_t addr;
        uint32_t data;
        uint32_t lane;
        uint32_t source;
        read_data_comb = 0;
        addr = (uint32_t)read_addr_reg;
        data = 0;
        if (addr >= PRIORITY_BASE && addr < PENDING_BASE) {
            source = (addr - PRIORITY_BASE) / 4;
            if (source < SOURCES) {
                data = priority_reg[source];
            }
        }
        else if (addr == PENDING_BASE) {
            data = pending_bits_comb_func();
        }
        else if (addr == ENABLE_BASE) {
            data = enable_reg[0];
        }
        else if (addr == CONTEXT_BASE) {
            data = threshold_reg[0];
        }
        else if (addr == CONTEXT_BASE + CLAIM_OFFSET) {
            data = claim_comb_func();
        }
        lane = ((uint32_t)read_addr_reg % (DATA_WIDTH / 8));
        read_data_comb.bits(lane * 8 + 31, lane * 8) = data;
        return read_data_comb;
    }

public:
    void _assign()
    {
#ifdef MULTICORE
        size_t i;
#endif
        axi_in.awready_out = _ASSIGN(!write_addr_valid_reg && !write_resp_valid_reg);
        axi_in.wready_out = _ASSIGN(write_addr_valid_reg && !write_resp_valid_reg);
        axi_in.bvalid_out = _ASSIGN_REG(write_resp_valid_reg);
        axi_in.bid_out = _ASSIGN_REG(write_id_reg);
        axi_in.arready_out = _ASSIGN(!read_valid_reg);
        axi_in.rvalid_out = _ASSIGN_REG(read_valid_reg);
        axi_in.rdata_out = _ASSIGN_REG(read_data_reg);
        axi_in.rlast_out = _ASSIGN_REG(read_valid_reg);
        axi_in.rid_out = _ASSIGN_REG(read_id_reg);
#ifdef MULTICORE
        for (i = 0; i < HART_COUNT; ++i) {
            external_irq_per_hart_out[i] = _ASSIGN_I(claim_for_context(i) != 0);
        }
#endif
    }

    void _work(bool reset)
    {
        size_t i;
        uint32_t addr;
        uint32_t lane;
        uint32_t data;
        uint32_t source;
        uint32_t context;
        uint32_t source_bits;
        uint32_t pending_next;
        uint32_t pending_work;
        uint32_t claim_on_read;
        uint32_t read_word;
        uint32_t completion_mask;
        logic<DATA_WIDTH> read_data;
#ifndef SYNTHESIS
        bool trace;
        const char* trace_from_env;
        const char* trace_file_env;
        long trace_from;
        static uint32_t last_trace_signature = 0xffffffffu;
        static FILE* trace_file = nullptr;
        static bool trace_file_checked = false;
        FILE* trace_out;
        uint32_t trace_signature;
        trace_from_env = std::getenv("TRIBE_TRACE_PLIC_FROM");
        trace_from = trace_from_env ? std::strtol(trace_from_env, nullptr, 0) : 0;
        trace = std::getenv("TRIBE_TRACE_PLIC") != nullptr && _system_clock >= trace_from;
        if (!trace_file_checked) {
            trace_file_checked = true;
            trace_file_env = std::getenv("TRIBE_TRACE_PLIC_FILE");
            if (trace_file_env && trace_file_env[0] != '\0') {
                trace_file = std::fopen(trace_file_env, "w");
            }
        }
        trace_out = trace_file ? trace_file : stdout;
#endif
        completion_mask = 0;
        if (axi_in.wvalid_in() && axi_in.wready_out()) {
            addr = (uint32_t)write_addr_reg;
            lane = ((uint32_t)write_addr_reg % (DATA_WIDTH / 8));
            data = (uint32_t)axi_in.wdata_in().bits(lane * 8 + 31, lane * 8);
            context = 0;
            if (addr >= CONTEXT_BASE && addr < CONTEXT_BASE + HART_COUNT * CONTEXT_STRIDE) {
                context = (addr - CONTEXT_BASE) / CONTEXT_STRIDE;
            }
            if (context < HART_COUNT &&
                addr == CONTEXT_BASE + context * CONTEXT_STRIDE + CLAIM_OFFSET &&
                data > 0 && data < 32) {
                completion_mask = 1u << data;
            }
        }

        source_bits = source_bits_comb_func() & ~completion_mask;
        // PLIC gateways latch a device request until the hart claims it. This
        // keeps a source claimable even if the level drops before Linux reaches
        // the claim register. A completion write also masks the sampled level
        // for this cycle, avoiding a stale same-cycle device level at the
        // C++/Verilator boundary. If the level is truly still asserted, it
        // pends again on the following cycle.
        pending_next = (uint32_t)pending_reg | (source_bits & ~(uint32_t)gateway_busy_reg);
        pending_work = pending_next;
        pending_reg._next = pending_work;
#ifndef SYNTHESIS
        if (trace) {
            trace_signature = (pending_bits_comb_func() & 0xffffu) |
                (((uint32_t)enable_reg[0] & 0xffu) << 16) |
                ((claim_comb_func() & 0xffu) << 24);
            if (trace_signature != last_trace_signature) {
                last_trace_signature = trace_signature;
                std::print(trace_out, "plic-status cycle={} pending={:08x} enable={:08x} threshold={} prio1={} prio2={} claim={} irq={} source={:08x} gateway={:08x}\n",
                    _system_clock,
                    pending_bits_comb_func(), (uint32_t)enable_reg[0], (uint32_t)threshold_reg[0],
                    (uint32_t)priority_reg[1], (uint32_t)priority_reg[2],
                    claim_comb_func(), external_irq_comb_func(), source_bits, (uint32_t)gateway_busy_reg);
                std::fflush(trace_out);
            }
        }
#endif

        if (axi_in.arvalid_in() && axi_in.arready_out()) {
            addr = (uint32_t)axi_in.araddr_in();
            read_word = 0;
            if (addr >= PRIORITY_BASE && addr < PENDING_BASE) {
                source = (addr - PRIORITY_BASE) / 4;
                if (source < SOURCES) {
                    read_word = priority_reg[source];
                }
            }
            else if (addr == PENDING_BASE) {
                read_word = pending_work;
            }
            else if (addr >= ENABLE_BASE && addr < ENABLE_BASE + HART_COUNT * ENABLE_STRIDE &&
                ((addr - ENABLE_BASE) % ENABLE_STRIDE) == 0) {
                context = (addr - ENABLE_BASE) / ENABLE_STRIDE;
                read_word = enable_reg[context];
            }
            else if (addr >= CONTEXT_BASE && addr < CONTEXT_BASE + HART_COUNT * CONTEXT_STRIDE &&
                ((addr - CONTEXT_BASE) % CONTEXT_STRIDE) == 0) {
                context = (addr - CONTEXT_BASE) / CONTEXT_STRIDE;
                read_word = threshold_reg[context];
            }
            else if (addr >= CONTEXT_BASE + CLAIM_OFFSET &&
                addr < CONTEXT_BASE + HART_COUNT * CONTEXT_STRIDE + CLAIM_OFFSET &&
                ((addr - CONTEXT_BASE) % CONTEXT_STRIDE) == CLAIM_OFFSET) {
                context = (addr - CONTEXT_BASE) / CONTEXT_STRIDE;
                claim_on_read = claim_for_context(context);
                read_word = claim_on_read;
                if (claim_on_read > 0 && claim_on_read < 32) {
                    pending_work &= ~(1u << claim_on_read);
                    pending_reg._next = pending_work;
                    gateway_busy_reg._next = (uint32_t)gateway_busy_reg | (1u << claim_on_read);
                }
            }
            lane = addr % (DATA_WIDTH / 8);
            read_data = 0;
            read_data.bits(lane * 8 + 31, lane * 8) = read_word;
            read_data_reg._next = read_data;
            read_addr_reg._next = axi_in.araddr_in();
            read_id_reg._next = axi_in.arid_in();
            read_valid_reg._next = true;
#ifndef SYNTHESIS
            if (trace) {
                std::print(trace_out, "plic-read-addr cycle={} addr={:x} data={:08x} pending={:08x} enable={:08x} gateway={:08x} source={:08x} claim={}\n",
                    _system_clock, addr, read_word, pending_work, (uint32_t)enable_reg[0],
                    (uint32_t)gateway_busy_reg, source_bits, claim_comb_func());
                std::fflush(trace_out);
            }
#endif
        }
        if (read_valid_reg && axi_in.rready_in()) {
#ifndef SYNTHESIS
            if (trace) {
                std::print(trace_out, "plic-read-data cycle={} addr={:x} data={:08x} pending={:08x} enable={:08x} gateway={:08x} source={:08x} claim={}\n",
                    _system_clock, (uint32_t)read_addr_reg,
                    (uint32_t)((logic<DATA_WIDTH>)read_data_reg).bits(((uint32_t)read_addr_reg % (DATA_WIDTH / 8)) * 8 + 31, ((uint32_t)read_addr_reg % (DATA_WIDTH / 8)) * 8),
                    pending_bits_comb_func(), (uint32_t)enable_reg[0],
                    (uint32_t)gateway_busy_reg, source_bits, claim_comb_func());
                std::fflush(trace_out);
            }
#endif
            read_valid_reg._next = false;
        }

        if (axi_in.awvalid_in() && axi_in.awready_out()) {
            write_addr_reg._next = axi_in.awaddr_in();
            write_id_reg._next = axi_in.awid_in();
            write_addr_valid_reg._next = true;
        }
        if (axi_in.wvalid_in() && axi_in.wready_out()) {
            addr = (uint32_t)write_addr_reg;
            lane = ((uint32_t)write_addr_reg % (DATA_WIDTH / 8));
            data = (uint32_t)axi_in.wdata_in().bits(lane * 8 + 31, lane * 8);
            write_addr_valid_reg._next = false;
            write_resp_valid_reg._next = true;
            if (addr >= PRIORITY_BASE && addr < PENDING_BASE) {
                source = (addr - PRIORITY_BASE) / 4;
                if (source < SOURCES) {
                    priority_reg[source]._next = data;
                }
            }
            else if (addr >= ENABLE_BASE && addr < ENABLE_BASE + HART_COUNT * ENABLE_STRIDE &&
                ((addr - ENABLE_BASE) % ENABLE_STRIDE) == 0) {
                context = (addr - ENABLE_BASE) / ENABLE_STRIDE;
                enable_reg[context]._next = data;
            }
            else if (addr >= CONTEXT_BASE && addr < CONTEXT_BASE + HART_COUNT * CONTEXT_STRIDE &&
                ((addr - CONTEXT_BASE) % CONTEXT_STRIDE) == 0) {
                context = (addr - CONTEXT_BASE) / CONTEXT_STRIDE;
                threshold_reg[context]._next = data;
            }
            else if (addr >= CONTEXT_BASE + CLAIM_OFFSET &&
                addr < CONTEXT_BASE + HART_COUNT * CONTEXT_STRIDE + CLAIM_OFFSET &&
                ((addr - CONTEXT_BASE) % CONTEXT_STRIDE) == CLAIM_OFFSET) {
                if (completion_mask != 0) {
                    gateway_busy_reg._next = (uint32_t)gateway_busy_reg & ~completion_mask;
                }
            }
#ifndef SYNTHESIS
            if (trace) {
                std::print(trace_out, "plic-write cycle={} addr={:x} data={:08x} pending={:08x} enable={:08x} gateway={:08x} source={:08x} claim={}\n",
                    _system_clock, addr, data, pending_bits_comb_func(), (uint32_t)enable_reg[0],
                    (uint32_t)gateway_busy_reg, source_bits, claim_comb_func());
                std::fflush(trace_out);
            }
#endif
            // Completion releases the gateway only. If the device is still
            // asserting on the next cycle, the normal gateway logic above will
            // re-pend it. This avoids sampling a stale same-cycle device level
            // while the device side effect that cleared the IRQ is also
            // strobing.
        }
        if (write_resp_valid_reg && axi_in.bready_in()) {
            write_resp_valid_reg._next = false;
        }

        if (reset) {
            read_addr_reg.clr();
            read_id_reg.clr();
            read_valid_reg.clr();
            read_data_reg.clr();
            write_addr_reg.clr();
            write_id_reg.clr();
            write_addr_valid_reg.clr();
            write_resp_valid_reg.clr();
            pending_reg.clr();
            gateway_busy_reg.clr();
            for (i = 0; i < HART_COUNT; ++i) {
                enable_reg[i].clr();
                threshold_reg[i].clr();
            }
            for (i = 0; i < SOURCES; ++i) {
                priority_reg[i]._next = 0;
            }
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        size_t i;
        read_addr_reg.strobe(checkpoint_fd);
        read_id_reg.strobe(checkpoint_fd);
        read_valid_reg.strobe(checkpoint_fd);
        read_data_reg.strobe(checkpoint_fd);
        write_addr_reg.strobe(checkpoint_fd);
        write_id_reg.strobe(checkpoint_fd);
        write_addr_valid_reg.strobe(checkpoint_fd);
        write_resp_valid_reg.strobe(checkpoint_fd);
        for (i = 0; i < HART_COUNT; ++i) {
            enable_reg[i].strobe(checkpoint_fd);
            threshold_reg[i].strobe(checkpoint_fd);
        }
        pending_reg.strobe(checkpoint_fd);
        gateway_busy_reg.strobe(checkpoint_fd);
        for (i = 0; i < SOURCES; ++i) {
            priority_reg[i].strobe(checkpoint_fd);
        }
    }
};
