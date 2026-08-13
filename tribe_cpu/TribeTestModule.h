#pragma once

#include "Tribe.h"
#include "common/Axi4Cdc.h"
#include "cache/L1MemCdc.h"
#include "cache/l2/L2Cache.h"

struct L1PeerStoreState
{
    u1 valid; // Delays one accepted shared-L2 store into a one-cycle snoop event.
    u32 addr; // Identifies the private-cache line affected by that committed store.
};

struct L1PeerInvalidateComb
{
    u1 valid; // Requests one targeted peer L1 invalidation this cycle.
    u1 full;  // Falls back to a full private-cache invalidate when stores collide.
    u32 addr; // Supplies the store address used to select the peer tag set.
};

// Composes one or more Tribe CPU cores around one coherent shared L2 cache.
template<size_t CPU_CORES = 1>
class TribeTest : public Module
{
    static_assert(CPU_CORES >= 1, "TribeTest requires at least one CPU core");
    static_assert(CPU_CORES <= 8, "TribeTest is limited by the L2 CPU-port response table");

    static constexpr size_t L2_TOTAL_SIZE = L2_CACHE_SIZE; // Fixes the shared L2 capacity in generated SV.
    static constexpr size_t L2_PORT_WIDTH = TRIBE_L2_AXI_WIDTH; // Fixes each L2 data-port width.
    static constexpr size_t L2_LINE_SIZE = CACHE_LINE_SIZE; // Fixes the shared cache-line size.
    static constexpr size_t L2_WAYS = L2_CACHE_ASSOCIATIONS; // Fixes the shared L2 associativity.
    static constexpr size_t L2_ADDRESS_BITS = ADDR_BITS; // Fixes the CPU-visible address width.
    static constexpr size_t L2_RAM_ADDRESS_BITS = clog2(MAX_RAM_SIZE); // Sizes downstream RAM addresses.
    static constexpr size_t L2_PORT_COUNT = L2_MEM_PORTS; // Counts external memory and device routes.
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
    static constexpr size_t ATOMIC_OWNER_BITS = CPU_CORES > 1 ? clog2(CPU_CORES) : 1; // Identifies the core holding the shared AMO transaction.
#endif

public:
    Tribe cores[CPU_CORES];
    L1MemFastToSlowCdc<L2_PORT_WIDTH> i_mem_cdc[CPU_CORES];
    L1MemFastToSlowCdc<L2_PORT_WIDTH> d_mem_cdc[CPU_CORES];
    L2Cache<L2_TOTAL_SIZE, L2_PORT_WIDTH, L2_LINE_SIZE, L2_WAYS,
        L2_ADDRESS_BITS, L2_RAM_ADDRESS_BITS, L2_PORT_COUNT, CPU_CORES> l2cache;
    Axi4FastToSlowCdc<L2_ADDRESS_BITS, 4, L2_PORT_WIDTH> axi_in_cdc[L2_PORT_COUNT];
    Axi4SlowToFastCdc<L2_RAM_ADDRESS_BITS, 4, L2_PORT_WIDTH> axi_out_cdc[L2_PORT_COUNT];

protected:
    reg<L1PeerStoreState> peer_store_reg[CPU_CORES]; // Holds each store and address through L2 completion.

private:
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
    reg<u1> atomic_owner_valid_reg; // Holds the arbiter while one core completes an AMO read-modify-write.
    reg<u<ATOMIC_OWNER_BITS>> atomic_owner_reg; // Selects the only core allowed onto the data-side L2 path during an AMO.
    reg<u<ATOMIC_OWNER_BITS>> atomic_rr_reg; // Starts the next AMO grant after the previous owner.
#endif

public:

    _PORT(bool) dmem_write_out = _ASSIGN_COMB(cores[0].dmem_write_out());
    _PORT(uint32_t) dmem_write_data_out = _ASSIGN_COMB(cores[0].dmem_write_data_out());
    _PORT(uint8_t) dmem_write_mask_out = _ASSIGN_COMB(cores[0].dmem_write_mask_out());
    _PORT(bool) dmem_read_out = _ASSIGN_COMB(cores[0].dmem_read_out());
    _PORT(uint32_t) dmem_addr_out = _ASSIGN_COMB(cores[0].dmem_addr_out());
    _PORT(uint32_t) imem_read_addr_out = _ASSIGN_COMB(cores[0].imem_read_addr_out());
#ifdef ENABLE_MMU_TLB
    _PORT(TribeCoreDebug) debug_core_out = _ASSIGN_COMB(cores[0].debug_core_out());
    _PORT(TribeMmuDebug) debug_mmu_out = _ASSIGN_COMB(cores[0].debug_mmu_out());
    _PORT(TribeCacheDebug) debug_cache_out = _ASSIGN_COMB(cores[0].debug_cache_out());
    _PORT(TribeWritebackDebug) debug_wb_out = _ASSIGN_COMB(cores[0].debug_wb_out());
    _PORT(TribeCsrDebug) debug_csr_out = _ASSIGN_COMB(cores[0].debug_csr_out());
    _PORT(TribeIrqDebug) debug_irq_out = _ASSIGN_COMB(cores[0].debug_irq_out());
    _PORT(TribeRegsDebug) debug_regs_out = _ASSIGN_COMB(cores[0].debug_regs_out());
    _PORT(TribeBranchDebug) debug_branch_out = _ASSIGN_COMB(cores[0].debug_branch_out());
    _PORT(TribeDecodeDebug) debug_decode_out = _ASSIGN_COMB(cores[0].debug_decode_out());
#endif
    _PORT(bool) sbi_set_timer_out = _ASSIGN_COMB(cores[0].sbi_set_timer_out());
    _PORT(uint32_t) sbi_timer_lo_out = _ASSIGN_COMB(cores[0].sbi_timer_lo_out());
    _PORT(uint32_t) sbi_timer_hi_out = _ASSIGN_COMB(cores[0].sbi_timer_hi_out());
#if defined(MULTICORE) && defined(ENABLE_ISR)
    _PORT(bool) sbi_set_timer_per_core_out[CPU_CORES];
    _PORT(uint32_t) sbi_timer_lo_per_core_out[CPU_CORES];
    _PORT(uint32_t) sbi_timer_hi_per_core_out[CPU_CORES];
#endif
    _PORT(TribeSbiDebug) debug_sbi_out = _ASSIGN_COMB(cores[0].debug_sbi_out());
    _PORT(TribePerf) perf_out = _ASSIGN_COMB(cores[0].perf_out());

    _PORT(uint32_t) reset_pc_in;
    _PORT(uint32_t) boot_hartid_in;
    _PORT(uint32_t) boot_dtb_addr_in;
    _PORT(u<2>) boot_priv_in;
    _PORT(bool) external_cache_invalidate_in;
    _PORT(uint32_t) memory_base_in;
    _PORT(uint32_t) memory_size_in;
    _PORT(uint32_t) mem_region_size_in[L2_MEM_PORTS];
#if defined(ENABLE_ZICSR) && defined(ENABLE_ISR)
    _PORT(bool) clint_msip_in;
    _PORT(bool) clint_mtip_in;
    _PORT(uint32_t) time_lo_in = _ASSIGN((uint32_t)0);
    _PORT(uint32_t) time_hi_in = _ASSIGN((uint32_t)0);
    _PORT(bool) external_irq_in = _ASSIGN(false);
#ifdef MULTICORE
    _PORT(bool) clint_msip_per_core_in[CPU_CORES];
    _PORT(bool) clint_mtip_per_core_in[CPU_CORES];
    _PORT(bool) external_irq_per_core_in[CPU_CORES];
#endif
#endif

    // External coherent masters and memory/device ports retain the former Tribe contract.
    Axi4If<32, 4, L2_PORT_WIDTH> axi_in[L2_PORT_COUNT];
    Axi4If<L2_RAM_ADDRESS_BITS, 4, L2_PORT_WIDTH> axi_out[L2_PORT_COUNT];
    bool debugen_in;

    // Selects one committed store per target so each affected peer set is invalidated once.
    L1PeerInvalidateComb peer_invalidate_comb[CPU_CORES];
#ifndef SYNTHESIS
    long prev_peer_invalidate_comb_clock = -1;
#endif
    L1PeerInvalidateComb (&peer_invalidate_comb_func())[CPU_CORES]
    {
        uint32_t target;
        uint32_t source;
#ifndef SYNTHESIS
        if (prev_peer_invalidate_comb_clock == _system_clock) {
            return peer_invalidate_comb;
        }
        prev_peer_invalidate_comb_clock = _system_clock;
#endif
        for (target = 0; target < CPU_CORES; ++target) {
            peer_invalidate_comb[target] = {};
            for (source = 0; source < CPU_CORES; ++source) {
                if (target != source && peer_store_reg[source].valid) {
                    if (peer_invalidate_comb[target].valid) {
                        // One line-invalidate lane cannot represent two different
                        // peer stores in the same cycle. Invalidate the complete
                        // private D-cache instead of silently losing one store.
                        peer_invalidate_comb[target].valid = false;
                        peer_invalidate_comb[target].full = true;
                    }
                    else if (!peer_invalidate_comb[target].full) {
                        peer_invalidate_comb[target].valid = true;
                        peer_invalidate_comb[target].addr =
                            (uint32_t)peer_store_reg[source].addr;
                    }
                }
            }
        }
        return peer_invalidate_comb;
    }

#if defined(MULTICORE) && defined(ENABLE_ISR)
    static bool sbi_hart_selected(uint32_t mask, uint32_t base, uint32_t target)
    {
        if (base == 0xffffffffu) {
            return true;
        }
        return target >= base && target - base < 32u && ((mask >> (target - base)) & 1u) != 0;
    }

    // Route SBI IPI requests from any source core to every selected target core.
    _LAZY_COMB(sbi_ipi_targets_comb, logic<CPU_CORES>)
        uint32_t source;
        uint32_t target;
        sbi_ipi_targets_comb = 0;
        for (source = 0; source < CPU_CORES; ++source) {
            for (target = 0; target < CPU_CORES; ++target) {
                if (cores[source].sbi_send_ipi_out() &&
                    sbi_hart_selected(cores[source].sbi_hart_mask_out(),
                        cores[source].sbi_hart_base_out(), target)) {
                    sbi_ipi_targets_comb[target] = 1;
                }
            }
        }
        return sbi_ipi_targets_comb;
    }

    // Route remote instruction-cache invalidation to the SBI-selected harts.
    _LAZY_COMB(sbi_fence_i_targets_comb, logic<CPU_CORES>)
        uint32_t source;
        uint32_t target;
        sbi_fence_i_targets_comb = 0;
        for (source = 0; source < CPU_CORES; ++source) {
            for (target = 0; target < CPU_CORES; ++target) {
                if (cores[source].sbi_remote_fence_i_out() &&
                    sbi_hart_selected(cores[source].sbi_hart_mask_out(),
                        cores[source].sbi_hart_base_out(), target)) {
                    sbi_fence_i_targets_comb[target] = 1;
                }
            }
        }
        return sbi_fence_i_targets_comb;
    }

    // Route remote address-translation invalidation to the SBI-selected harts.
#ifdef ENABLE_MMU_TLB
    _LAZY_COMB(sbi_sfence_targets_comb, logic<CPU_CORES>)
        uint32_t source;
        uint32_t target;
        sbi_sfence_targets_comb = 0;
        for (source = 0; source < CPU_CORES; ++source) {
            for (target = 0; target < CPU_CORES; ++target) {
                if (cores[source].sbi_remote_sfence_vma_out() &&
                    sbi_hart_selected(cores[source].sbi_hart_mask_out(),
                        cores[source].sbi_hart_base_out(), target)) {
                    sbi_sfence_targets_comb[target] = 1;
                }
            }
        }
        return sbi_sfence_targets_comb;
    }
#endif

#ifdef ENABLE_RV32IA
    // Grants an AMO only after the clocked arbiter has selected one requester.
    _LAZY_COMB(atomic_grant_comb, logic<CPU_CORES>)
        atomic_grant_comb = 0;
        if (atomic_owner_valid_reg) {
            atomic_grant_comb[(uint32_t)atomic_owner_reg] = 1;
        }
        return atomic_grant_comb;
    }

    // Allows ordinary data traffic while idle and only owner traffic during an AMO.
    _LAZY_COMB(atomic_data_access_comb, logic<CPU_CORES>)
        uint32_t i;
        atomic_data_access_comb = 0;
        if (atomic_owner_valid_reg) {
            atomic_data_access_comb[(uint32_t)atomic_owner_reg] = 1;
        }
        else {
            for (i = 0; i < CPU_CORES; ++i) {
                atomic_data_access_comb[i] = 1;
            }
        }
        return atomic_data_access_comb;
    }
#endif
#endif

    // Connects all cores to the shared L2 and preserves the legacy external AXI boundary.
    void _assign()
    {
        uint32_t i;
        uint32_t region;

        l2cache.memory_base_in = memory_base_in;
        l2cache.memory_size_in = memory_size_in;
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            l2cache.mem_region_size_in[i] = mem_region_size_in[i];
            AXI4_DRIVER_FROM_I(axi_in_cdc[i].fast_in, axi_in[i]);
            AXI4_RESPONDER_FROM_I(axi_in[i], axi_in_cdc[i].fast_in);
            AXI4_DRIVER_FROM_I(l2cache.axi_in[i], axi_in_cdc[i].slow_out);
            AXI4_RESPONDER_FROM_I(axi_in_cdc[i].slow_out, l2cache.axi_in[i]);
            AXI4_DRIVER_FROM_I(axi_out_cdc[i].slow_in, l2cache.axi_out[i]);
            AXI4_RESPONDER_FROM_I(l2cache.axi_out[i], axi_out_cdc[i].slow_in);
            AXI4_DRIVER_FROM_I(axi_out[i], axi_out_cdc[i].fast_out);
            AXI4_RESPONDER_FROM_I(axi_out_cdc[i].fast_out, axi_out[i]);
#ifndef SYNTHESIS
            axi_in_cdc[i].__inst_name = __inst_name + "/axi_in_cdc" + std::to_string(i);
            axi_out_cdc[i].__inst_name = __inst_name + "/axi_out_cdc" + std::to_string(i);
#endif
            axi_in_cdc[i]._assign();
            axi_out_cdc[i]._assign();
        }
        l2cache.mem_region_uncached_in[0] = _ASSIGN(false);
        l2cache.mem_region_uncached_in[1] = _ASSIGN(false);
        l2cache.mem_region_uncached_in[2] = _ASSIGN(false);
        l2cache.mem_region_uncached_in[3] = _ASSIGN(true);
        l2cache.debugen_in = debugen_in;
        l2cache.__inst_name = __inst_name + "/l2cache";
        l2cache._assign();

        for (i = 0; i < CPU_CORES; ++i) {
            i_mem_cdc[i]._assign();
            d_mem_cdc[i]._assign();
            cores[i].debugen_in = debugen_in;
            cores[i].reset_pc_in = reset_pc_in;
            cores[i].boot_hartid_in = _ASSIGN_I((uint32_t)boot_hartid_in() + (uint32_t)i);
            cores[i].boot_dtb_addr_in = boot_dtb_addr_in;
            cores[i].boot_priv_in = boot_priv_in;
            cores[i].external_cache_invalidate_in = _ASSIGN_I(
                external_cache_invalidate_in() || peer_invalidate_comb_func()[i].full);
#ifdef MULTICORE
            cores[i].peer_cache_invalidate_in = _ASSIGN_I(peer_invalidate_comb_func()[i].valid);
            cores[i].peer_cache_invalidate_addr_in = _ASSIGN_I(peer_invalidate_comb_func()[i].addr);
#endif
            cores[i].memory_base_in = memory_base_in;
            cores[i].memory_size_in = memory_size_in;
            for (region = 0; region < L2_MEM_PORTS; ++region) {
                cores[i].mem_region_size_in[region] = mem_region_size_in[region];
            }
#if defined(ENABLE_ZICSR) && defined(ENABLE_ISR)
#ifdef MULTICORE
            cores[i].clint_msip_in = clint_msip_per_core_in[i];
            cores[i].clint_mtip_in = clint_mtip_per_core_in[i];
            cores[i].external_irq_in = external_irq_per_core_in[i];
            cores[i].sbi_ipi_in = _ASSIGN_I(sbi_ipi_targets_comb_func()[i]);
            cores[i].remote_fence_i_in = _ASSIGN_I(sbi_fence_i_targets_comb_func()[i]);
            cores[i].remote_sfence_vma_in = _ASSIGN_I(sbi_sfence_targets_comb_func()[i]);
            sbi_set_timer_per_core_out[i] = cores[i].sbi_set_timer_out;
            sbi_timer_lo_per_core_out[i] = cores[i].sbi_timer_lo_out;
            sbi_timer_hi_per_core_out[i] = cores[i].sbi_timer_hi_out;
#else
            cores[i].clint_msip_in = clint_msip_in;
            cores[i].clint_mtip_in = clint_mtip_in;
            cores[i].external_irq_in = external_irq_in;
#endif
            cores[i].time_lo_in = time_lo_in;
            cores[i].time_hi_in = time_hi_in;
#endif
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
            cores[i].atomic_grant_in = _ASSIGN_I(atomic_grant_comb_func()[i]);
#endif
            cores[i].i_mem_out.read_data_out = i_mem_cdc[i].fast_in.read_data_out;
            cores[i].i_mem_out.wait_out = i_mem_cdc[i].fast_in.wait_out;
            cores[i].d_mem_out.read_data_out = d_mem_cdc[i].fast_in.read_data_out;
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
            cores[i].d_mem_out.wait_out = _ASSIGN_I(
                !atomic_data_access_comb_func()[i] || d_mem_cdc[i].fast_in.wait_out());
#else
            cores[i].d_mem_out.wait_out = d_mem_cdc[i].fast_in.wait_out;
#endif
#ifndef SYNTHESIS
            cores[i].__inst_name = __inst_name + "/core" + std::to_string(i);
            i_mem_cdc[i].__inst_name = __inst_name + "/i_mem_cdc" + std::to_string(i);
            d_mem_cdc[i].__inst_name = __inst_name + "/d_mem_cdc" + std::to_string(i);
#endif
            cores[i]._assign();
            i_mem_cdc[i].fast_in.read_in = cores[i].i_mem_out.read_in;
            i_mem_cdc[i].fast_in.write_in = cores[i].i_mem_out.write_in;
            i_mem_cdc[i].fast_in.addr_in = cores[i].i_mem_out.addr_in;
            i_mem_cdc[i].fast_in.write_data_in = cores[i].i_mem_out.write_data_in;
            i_mem_cdc[i].fast_in.write_mask_in = cores[i].i_mem_out.write_mask_in;
            i_mem_cdc[i].fast_in.cache_disable_in = cores[i].i_mem_out.cache_disable_in;
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
            d_mem_cdc[i].fast_in.read_in = _ASSIGN_I(
                atomic_data_access_comb_func()[i] && cores[i].d_mem_out.read_in());
            d_mem_cdc[i].fast_in.write_in = _ASSIGN_I(
                atomic_data_access_comb_func()[i] && cores[i].d_mem_out.write_in());
#else
            d_mem_cdc[i].fast_in.read_in = cores[i].d_mem_out.read_in;
            d_mem_cdc[i].fast_in.write_in = cores[i].d_mem_out.write_in;
#endif
            d_mem_cdc[i].fast_in.addr_in = cores[i].d_mem_out.addr_in;
            d_mem_cdc[i].fast_in.write_data_in = cores[i].d_mem_out.write_data_in;
            d_mem_cdc[i].fast_in.write_mask_in = cores[i].d_mem_out.write_mask_in;
            d_mem_cdc[i].fast_in.cache_disable_in = cores[i].d_mem_out.cache_disable_in;

            l2cache.i_mem_in[i].read_in = i_mem_cdc[i].slow_out.read_in;
            l2cache.i_mem_in[i].write_in = i_mem_cdc[i].slow_out.write_in;
            l2cache.i_mem_in[i].addr_in = i_mem_cdc[i].slow_out.addr_in;
            l2cache.i_mem_in[i].write_data_in = i_mem_cdc[i].slow_out.write_data_in;
            l2cache.i_mem_in[i].write_mask_in = i_mem_cdc[i].slow_out.write_mask_in;
            l2cache.i_mem_in[i].cache_disable_in = i_mem_cdc[i].slow_out.cache_disable_in;
            i_mem_cdc[i].slow_out.read_data_out = l2cache.i_mem_in[i].read_data_out;
            i_mem_cdc[i].slow_out.wait_out = l2cache.i_mem_in[i].wait_out;

            l2cache.d_mem_in[i].read_in = d_mem_cdc[i].slow_out.read_in;
            l2cache.d_mem_in[i].write_in = d_mem_cdc[i].slow_out.write_in;
            l2cache.d_mem_in[i].addr_in = d_mem_cdc[i].slow_out.addr_in;
            l2cache.d_mem_in[i].write_data_in = d_mem_cdc[i].slow_out.write_data_in;
            l2cache.d_mem_in[i].write_mask_in = d_mem_cdc[i].slow_out.write_mask_in;
            l2cache.d_mem_in[i].cache_disable_in = d_mem_cdc[i].slow_out.cache_disable_in;
            d_mem_cdc[i].slow_out.read_data_out = l2cache.d_mem_in[i].read_data_out;
            d_mem_cdc[i].slow_out.wait_out = l2cache.d_mem_in[i].wait_out;
        }
    }

    // Advances every core before the shared cache consumes their live requests.
    void work_clk_func(bool reset)
    {
        uint32_t i;
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
        bool atomic_selected;
        bool ordinary_data_pending;
        uint32_t atomic_candidate;
        uint32_t atomic_offset;
#endif
#ifdef SYNTHESIS
        // These calls are removed from the parent SV task, but keep each child
        // module's clocked implementation reachable during cpphdl conversion.
        cores[0]._work(reset);
        i_mem_cdc[0]._work_clk(reset);
        d_mem_cdc[0]._work_clk(reset);
        axi_in_cdc[0]._work_clk(reset);
        axi_out_cdc[0]._work_clk(reset);
#else
        for (i = 0; i < CPU_CORES; ++i) {
            cores[i]._work(reset);
            i_mem_cdc[i]._work(reset);
            d_mem_cdc[i]._work(reset);
        }
        for (i = 0; i < L2_PORT_COUNT; ++i) {
            axi_in_cdc[i]._work(reset);
            axi_out_cdc[i]._work(reset);
        }
#endif
        for (i = 0; i < CPU_CORES; ++i) {
            // Convert the L2 ready/write handshake into one delayed pulse. A
            // level held during L2 backpressure would repeatedly advance the
            // peer generation counter and could eventually alias stale tags.
            peer_store_reg[i]._next.valid = false;
            if (cores[i].dmem_write_out()
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
                && atomic_data_access_comb_func()[i]
#endif
                && !d_mem_cdc[i].fast_in.wait_out()) {
                peer_store_reg[i]._next.valid = true;
                peer_store_reg[i]._next.addr = cores[i].dmem_addr_out();
            }
        }
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
        atomic_selected = false;
        ordinary_data_pending = false;
        for (i = 0; i < CPU_CORES; ++i) {
            if ((cores[i].dmem_read_out() || cores[i].dmem_write_out()) &&
                !cores[i].atomic_data_request_out()) {
                ordinary_data_pending = true;
            }
        }
        if (atomic_owner_valid_reg) {
            if (cores[(uint32_t)atomic_owner_reg].atomic_complete_out() ||
                !cores[(uint32_t)atomic_owner_reg].atomic_request_out()) {
                atomic_owner_valid_reg._next = false;
                atomic_rr_reg._next = (uint32_t)atomic_owner_reg == CPU_CORES - 1 ?
                    (uint32_t)0 : (uint32_t)atomic_owner_reg + 1u;
            }
        }
        else if (!ordinary_data_pending) {
            // Tight lock polling keeps AMOs continuously present. Search from
            // the round-robin cursor so the failed poller cannot starve the
            // current lock holder's release operation.
            for (atomic_offset = 0; atomic_offset < CPU_CORES; ++atomic_offset) {
                atomic_candidate = (uint32_t)atomic_rr_reg + atomic_offset;
                if (atomic_candidate >= CPU_CORES) {
                    atomic_candidate -= CPU_CORES;
                }
                if (!atomic_selected && cores[atomic_candidate].atomic_request_out()) {
                    atomic_owner_valid_reg._next = true;
                    atomic_owner_reg._next = atomic_candidate;
                    atomic_selected = true;
                }
            }
        }
#endif
        if (reset) {
            for (i = 0; i < CPU_CORES; ++i) {
                // Indexed struct registers must reset through their fields so SV keeps
                // the array index after the flattened register name.
                peer_store_reg[i]._next.valid = false;
                peer_store_reg[i]._next.addr = 0;
            }
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
            atomic_owner_valid_reg.clr();
            atomic_owner_reg.clr();
            atomic_rr_reg.clr();
#endif
        }
    }

    void _work(bool reset)
    {
        work_clk_func(reset);
    }

    void _work_clk(bool reset)
    {
        work_clk_func(reset);
    }

#ifndef SYNTHESIS
    // Propagates the optional falling-edge phase to every CPU core.
    void _work_neg(bool reset)
    {
        for (size_t i = 0; i < CPU_CORES; ++i) {
            cores[i]._work_neg(reset);
        }
    }
#endif

    // Declares top-level primary-domain ownership for generated RTL. Child
    // lifecycle calls only force template instantiation; each child owns its
    // own clock block.
    void _strobe_clk()
    {
        uint32_t i;

#ifdef SYNTHESIS
        cores[0]._strobe();
        i_mem_cdc[0]._strobe_clk();
        d_mem_cdc[0]._strobe_clk();
        axi_in_cdc[0]._strobe_clk();
        axi_out_cdc[0]._strobe_clk();
#endif
        for (i = 0; i < CPU_CORES; ++i) {
            peer_store_reg[i].strobe();
        }
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
        atomic_owner_valid_reg.strobe();
        atomic_owner_reg.strobe();
        atomic_rr_reg.strobe();
#endif
    }

    // Commits CPU-domain state on every primary clock in native simulation.
    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        for (uint32_t i = 0; i < CPU_CORES; ++i) {
            cores[i]._strobe(checkpoint_fd);
            i_mem_cdc[i]._strobe();
            d_mem_cdc[i]._strobe();
        }
        for (uint32_t i = 0; i < L2_PORT_COUNT; ++i) {
            axi_in_cdc[i]._strobe();
            axi_out_cdc[i]._strobe();
        }
#ifndef SYNTHESIS
        if (checkpoint_fd) {
            l2cache.checkpoint_l2(checkpoint_fd);
        }
#endif
        for (uint32_t i = 0; i < CPU_CORES; ++i) {
            peer_store_reg[i].strobe(checkpoint_fd);
        }
#if defined(MULTICORE) && defined(ENABLE_RV32IA)
        atomic_owner_valid_reg.strobe(checkpoint_fd);
        atomic_owner_reg.strobe(checkpoint_fd);
        atomic_rr_reg.strobe(checkpoint_fd);
#endif
    }

    // The shared cache samples the level-held L1 requests only on divided
    // l2_clock edges. Its registered response remains visible until the next
    // L2 edge, giving the faster L1 domain time to retire the request.
    void _work_l2_clock(bool reset)
    {
#ifdef SYNTHESIS
        // Converter discovery calls: child clock blocks own the actual state.
        l2cache._work_l2_clock(reset);
        i_mem_cdc[0]._work_l2_clock(reset);
        d_mem_cdc[0]._work_l2_clock(reset);
        axi_in_cdc[0]._work_l2_clock(reset);
        axi_out_cdc[0]._work_l2_clock(reset);
#else
        l2cache._work_l2_clock(reset);
        for (uint32_t i = 0; i < CPU_CORES; ++i) {
            i_mem_cdc[i]._work_l2_clock(reset);
            d_mem_cdc[i]._work_l2_clock(reset);
        }
        for (uint32_t i = 0; i < L2_PORT_COUNT; ++i) {
            axi_in_cdc[i]._work_l2_clock(reset);
            axi_out_cdc[i]._work_l2_clock(reset);
        }
#endif
    }

    void _strobe_l2_clock()
    {
#ifdef SYNTHESIS
        // Converter discovery calls: omitted from the generated parent task.
        l2cache._strobe_l2_clock();
        i_mem_cdc[0]._strobe_l2_clock();
        d_mem_cdc[0]._strobe_l2_clock();
        axi_in_cdc[0]._strobe_l2_clock();
        axi_out_cdc[0]._strobe_l2_clock();
#else
        l2cache._strobe_l2_clock();
        for (uint32_t i = 0; i < CPU_CORES; ++i) {
            i_mem_cdc[i]._strobe_l2_clock();
            d_mem_cdc[i]._strobe_l2_clock();
        }
        for (uint32_t i = 0; i < L2_PORT_COUNT; ++i) {
            axi_in_cdc[i]._strobe_l2_clock();
            axi_out_cdc[i]._strobe_l2_clock();
        }
#endif
    }

#ifndef SYNTHESIS
    void checkpoint_axi_cdc(FILE* checkpoint_fd)
    {
        for (uint32_t i = 0; i < CPU_CORES; ++i) {
            i_mem_cdc[i].checkpoint(checkpoint_fd);
            d_mem_cdc[i].checkpoint(checkpoint_fd);
        }
        for (uint32_t i = 0; i < L2_PORT_COUNT; ++i) {
            axi_in_cdc[i].checkpoint(checkpoint_fd);
            axi_out_cdc[i].checkpoint(checkpoint_fd);
        }
    }
#endif
};

template class TribeTest<1>;
#ifdef MULTICORE
template class TribeTest<CPUS_PER_L2_CACHE>;
#endif
