#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED
#if defined(VERILATOR) && !defined(SYNTHESIS)
#define SYNTHESIS
#define SOC_UNDEF_SYNTHESIS_AFTER_MAIN
#endif

#include "../main.cpp"
#ifdef SOC_UNDEF_SYNTHESIS_AFTER_MAIN
#undef SYNTHESIS
#endif
#include "../common/Axi4Ram.h"
#if !defined(SYNTHESIS)
#include "../verif/SDCardVerif.h"
#endif

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef VERILATOR
#ifndef STRINGIFY
#define STRINGIFY_DETAIL(x) #x
#define STRINGIFY(x) STRINGIFY_DETAIL(x)
#endif
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)

static TribePerf verilator_tribe_perf(uint64_t bits)
{
    uint64_t storage = bits;
    return *reinterpret_cast<TribePerf*>(&storage);
}

template<size_t WORDS>
static logic<WORDS * 32> verilator_wide_to_logic(const VlWide<WORDS>& bits)
{
    logic<WORDS * 32> out = 0;
    memcpy(out.bytes, bits.m_storage, sizeof(out.bytes));
    return out;
}

template<size_t WORDS>
static logic<WORDS * 32> verilator_wide_to_logic(const WData (&bits)[WORDS])
{
    logic<WORDS * 32> out = 0;
    memcpy(out.bytes, bits, sizeof(out.bytes));
    return out;
}

static logic<64> verilator_wide_to_logic(const QData& bits)
{
    return (uint64_t)bits;
}

template<size_t WIDTH, size_t WORDS>
static void verilator_logic_to_wide(VlWide<WORDS>& out, const logic<WIDTH>& bits)
{
    static_assert(WIDTH == WORDS * 32);
    memcpy(out.m_storage, bits.bytes, sizeof(bits.bytes));
}

template<size_t WIDTH, size_t WORDS>
static void verilator_logic_to_wide(WData (&out)[WORDS], const logic<WIDTH>& bits)
{
    static_assert(WIDTH == WORDS * 32);
    memcpy(out, bits.bytes, sizeof(bits.bytes));
}

static void verilator_logic_to_wide(QData& out, const logic<64>& bits)
{
    out = (uint64_t)bits;
}
#endif

class System : public Module
{
#define SOC_MEM_REGION2_SIZE (TRIBE_RAM_BYTES_CONFIG - (TRIBE_RAM_BYTES_CONFIG / 2) - (TRIBE_RAM_BYTES_CONFIG / 4))
#ifdef TRIBE_L2_AXI_WIDTH_IS_64
#define SOC_AXI_RAM2_DEPTH (SOC_MEM_REGION2_SIZE / 8)
#elif defined(TRIBE_L2_AXI_WIDTH_IS_128)
#define SOC_AXI_RAM2_DEPTH (SOC_MEM_REGION2_SIZE / 16)
#else
#define SOC_AXI_RAM2_DEPTH (SOC_MEM_REGION2_SIZE / 32)
#endif

public:
    Tribe tribe;
    Axi4Ram<clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH, SOC_AXI_RAM2_DEPTH> mem2;
    Axi4RegionMux<5, clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH> iospace;
    NS16550A<clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH> uart;
    CLINT<clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH> clint;
    PLIC<clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH> plic;
    Accelerator<clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH> accelerator;
    SDController<clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH> sdcard;
    reg<u1> sd_dma_cache_invalidate_reg;

    bool debugen_in = false;

    _PORT(uint32_t) reset_pc_in;
    _PORT(uint32_t) boot_hartid_in;
    _PORT(uint32_t) boot_dtb_addr_in;
    _PORT(u<2>) boot_priv_in;
    _PORT(uint32_t) memory_base_in;
    _PORT(uint32_t) memory_size_in;
    _PORT(uint32_t) mem_region_size_in[L2_MEM_PORTS];

    _PORT(bool) uart_rx_valid_in;
    _PORT(uint8_t) uart_rx_data_in;
    _PORT(bool) uart_rx_ready_out = _ASSIGN_COMB(uart.uart_rx_ready_out());
    _PORT(bool) uart_tx_valid_out = _ASSIGN_COMB(uart.uart_valid_out());
    _PORT(uint8_t) uart_tx_data_out = _ASSIGN_COMB(uart.uart_data_out());

    _PORT(bool) sd_cmd_valid_out = _ASSIGN_COMB(sdcard.sd_cmd_valid_out());
    _PORT(u<8>) sd_cmd_data_out = _ASSIGN_COMB(sdcard.sd_cmd_data_out());
    _PORT(bool) sd_cmd_last_out = _ASSIGN_COMB(sdcard.sd_cmd_last_out());
    _PORT(bool) sd_cmd_ready_in;
    _PORT(bool) sd_rsp_valid_in;
    _PORT(u<8>) sd_rsp_data_in;
    _PORT(bool) sd_rsp_last_in;
    _PORT(bool) sd_rsp_ready_out = _ASSIGN_COMB(sdcard.sd_rsp_ready_out());

    _PORT(TribePerf) perf_out = _ASSIGN_COMB(tribe.perf_out());
    _PORT(bool) dmem_write_out = _ASSIGN_COMB(tribe.dmem_write_out());
    _PORT(uint32_t) dmem_write_data_out = _ASSIGN_COMB(tribe.dmem_write_data_out());
    _PORT(uint8_t) dmem_write_mask_out = _ASSIGN_COMB(tribe.dmem_write_mask_out());
    _PORT(bool) dmem_read_out = _ASSIGN_COMB(tribe.dmem_read_out());
    _PORT(uint32_t) dmem_addr_out = _ASSIGN_COMB(tribe.dmem_addr_out());
    _PORT(uint32_t) imem_read_addr_out = _ASSIGN_COMB(tribe.imem_read_addr_out());

#ifdef ENABLE_MMU_TLB
    _PORT(bool) debug_immu_ptw_read_out = _ASSIGN_COMB(tribe.debug_immu_ptw_read_out());
    _PORT(uint32_t) debug_immu_ptw_addr_out = _ASSIGN_COMB(tribe.debug_immu_ptw_addr_out());
    _PORT(bool) debug_immu_busy_out = _ASSIGN_COMB(tribe.debug_immu_busy_out());
    _PORT(bool) debug_immu_fault_out = _ASSIGN_COMB(tribe.debug_immu_fault_out());
    _PORT(uint32_t) debug_immu_paddr_out = _ASSIGN_COMB(tribe.debug_immu_paddr_out());
    _PORT(uint32_t) debug_immu_last_addr_out = _ASSIGN_COMB(tribe.debug_immu_last_addr_out());
    _PORT(uint32_t) debug_immu_last_pte_out = _ASSIGN_COMB(tribe.debug_immu_last_pte_out());
    _PORT(bool) debug_icache_read_valid_out = _ASSIGN_COMB(tribe.debug_icache_read_valid_out());
    _PORT(uint32_t) debug_icache_read_addr_out = _ASSIGN_COMB(tribe.debug_icache_read_addr_out());
    _PORT(bool) debug_fetch_valid_out = _ASSIGN_COMB(tribe.debug_fetch_valid_out());
    _PORT(bool) debug_memory_wait_out = _ASSIGN_COMB(tribe.debug_memory_wait_out());
    _PORT(bool) debug_wb_load_ready_out = _ASSIGN_COMB(tribe.debug_wb_load_ready_out());
    _PORT(bool) debug_wb_mem_wait_out = _ASSIGN_COMB(tribe.debug_wb_mem_wait_out());
    _PORT(bool) debug_icache_read_in_out = _ASSIGN_COMB(tribe.debug_icache_read_in_out());
    _PORT(bool) debug_icache_stall_in_out = _ASSIGN_COMB(tribe.debug_icache_stall_in_out());
    _PORT(bool) debug_dmmu_ptw_read_out = _ASSIGN_COMB(tribe.debug_dmmu_ptw_read_out());
    _PORT(uint32_t) debug_dmmu_ptw_addr_out = _ASSIGN_COMB(tribe.debug_dmmu_ptw_addr_out());
    _PORT(bool) debug_dmmu_busy_out = _ASSIGN_COMB(tribe.debug_dmmu_busy_out());
    _PORT(bool) debug_dmmu_fault_out = _ASSIGN_COMB(tribe.debug_dmmu_fault_out());
    _PORT(uint32_t) debug_mmu_ptw_word_out = _ASSIGN_COMB(tribe.debug_mmu_ptw_word_out());
    _PORT(uint32_t) debug_pc_out = _ASSIGN_COMB(tribe.debug_pc_out());
    _PORT(uint32_t) debug_satp_out = _ASSIGN_COMB(tribe.debug_satp_out());
    _PORT(uint32_t) debug_mstatus_out = _ASSIGN_COMB(tribe.debug_mstatus_out());
    _PORT(uint32_t) debug_mtvec_out = _ASSIGN_COMB(tribe.debug_mtvec_out());
    _PORT(uint32_t) debug_mepc_out = _ASSIGN_COMB(tribe.debug_mepc_out());
    _PORT(uint32_t) debug_mcause_out = _ASSIGN_COMB(tribe.debug_mcause_out());
    _PORT(uint32_t) debug_mtval_out = _ASSIGN_COMB(tribe.debug_mtval_out());
    _PORT(uint32_t) debug_sepc_out = _ASSIGN_COMB(tribe.debug_sepc_out());
    _PORT(uint32_t) debug_stvec_out = _ASSIGN_COMB(tribe.debug_stvec_out());
    _PORT(uint32_t) debug_scause_out = _ASSIGN_COMB(tribe.debug_scause_out());
    _PORT(uint32_t) debug_stval_out = _ASSIGN_COMB(tribe.debug_stval_out());
    _PORT(u<2>) debug_priv_out = _ASSIGN_COMB(tribe.debug_priv_out());
    _PORT(uint32_t) debug_ra_out = _ASSIGN_COMB(tribe.debug_ra_out());
    _PORT(bool) debug_regs_write_out = _ASSIGN_COMB(tribe.debug_regs_write_out());
    _PORT(bool) debug_regs_write_actual_out = _ASSIGN_COMB(tribe.debug_regs_write_actual_out());
    _PORT(uint8_t) debug_regs_wr_id_out = _ASSIGN_COMB(tribe.debug_regs_wr_id_out());
    _PORT(uint32_t) debug_regs_data_out = _ASSIGN_COMB(tribe.debug_regs_data_out());
    _PORT(bool) debug_branch_taken_now_out = _ASSIGN_COMB(tribe.debug_branch_taken_now_out());
    _PORT(uint32_t) debug_branch_target_now_out = _ASSIGN_COMB(tribe.debug_branch_target_now_out());
    _PORT(uint32_t) debug_decode_instr_out = _ASSIGN_COMB(tribe.debug_decode_instr_out());
    _PORT(uint32_t) debug_decode_pc_out = _ASSIGN_COMB(tribe.debug_decode_pc_out());
    _PORT(uint8_t) debug_decode_br_out = _ASSIGN_COMB(tribe.debug_decode_br_out());
    _PORT(uint32_t) debug_decode_imm_out = _ASSIGN_COMB(tribe.debug_decode_imm_out());
#endif

    Axi4If<clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH> axi_out[2];

    void _assign()
    {
        size_t i;
        tribe.debugen_in = debugen_in;
        tribe.reset_pc_in = reset_pc_in;
        tribe.boot_hartid_in = boot_hartid_in;
        tribe.boot_dtb_addr_in = boot_dtb_addr_in;
        tribe.boot_priv_in = boot_priv_in;
        tribe.external_cache_invalidate_in =
#ifdef ENABLE_MMU_TLB
            _ASSIGN((bool)sd_dma_cache_invalidate_reg &&
                !tribe.debug_memory_wait_out() && !tribe.dmem_read_out() && !tribe.dmem_write_out());
#else
            _ASSIGN((bool)sd_dma_cache_invalidate_reg);
#endif
        tribe.memory_base_in = memory_base_in;
        tribe.memory_size_in = memory_size_in;
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            tribe.mem_region_size_in[i] = mem_region_size_in[i];
            tribe.axi_in[i].awvalid_in = _ASSIGN(false);
            tribe.axi_in[i].awaddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)0);
            tribe.axi_in[i].awid_in = _ASSIGN((u<4>)0);
            tribe.axi_in[i].wvalid_in = _ASSIGN(false);
            tribe.axi_in[i].wdata_in = _ASSIGN((logic<TRIBE_L2_AXI_WIDTH>)0);
            tribe.axi_in[i].wstrb_in = _ASSIGN((logic<TRIBE_L2_AXI_WIDTH / 8>)0);
            tribe.axi_in[i].wlast_in = _ASSIGN(false);
            tribe.axi_in[i].bready_in = _ASSIGN(false);
            tribe.axi_in[i].arvalid_in = _ASSIGN(false);
            tribe.axi_in[i].araddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)0);
            tribe.axi_in[i].arid_in = _ASSIGN((u<4>)0);
            tribe.axi_in[i].rready_in = _ASSIGN(false);
        }
        tribe.axi_in[0].awvalid_in = _ASSIGN(accelerator.dma_out.awvalid_in());
        tribe.axi_in[0].awaddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)(uint32_t)accelerator.dma_out.awaddr_in());
        tribe.axi_in[0].awid_in = _ASSIGN((u<4>)(uint32_t)accelerator.dma_out.awid_in());
        tribe.axi_in[0].wvalid_in = _ASSIGN(accelerator.dma_out.wvalid_in());
        tribe.axi_in[0].wdata_in = _ASSIGN(accelerator.dma_out.wdata_in());
        tribe.axi_in[0].wstrb_in = _ASSIGN(accelerator.dma_out.wstrb_in());
        tribe.axi_in[0].wlast_in = _ASSIGN(accelerator.dma_out.wlast_in());
        tribe.axi_in[0].bready_in = _ASSIGN(accelerator.dma_out.bready_in());
        tribe.axi_in[0].arvalid_in = _ASSIGN(accelerator.dma_out.arvalid_in());
        tribe.axi_in[0].araddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)(uint32_t)accelerator.dma_out.araddr_in());
        tribe.axi_in[0].arid_in = _ASSIGN((u<4>)(uint32_t)accelerator.dma_out.arid_in());
        tribe.axi_in[0].rready_in = _ASSIGN(accelerator.dma_out.rready_in());
        tribe.axi_in[1].awvalid_in = _ASSIGN(sdcard.dma_out.awvalid_in());
        tribe.axi_in[1].awaddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)(uint32_t)sdcard.dma_out.awaddr_in());
        tribe.axi_in[1].awid_in = _ASSIGN((u<4>)(uint32_t)sdcard.dma_out.awid_in());
        tribe.axi_in[1].wvalid_in = _ASSIGN(sdcard.dma_out.wvalid_in());
        tribe.axi_in[1].wdata_in = _ASSIGN(sdcard.dma_out.wdata_in());
        tribe.axi_in[1].wstrb_in = _ASSIGN(sdcard.dma_out.wstrb_in());
        tribe.axi_in[1].wlast_in = _ASSIGN(sdcard.dma_out.wlast_in());
        tribe.axi_in[1].bready_in = _ASSIGN(sdcard.dma_out.bready_in());
        tribe.axi_in[1].arvalid_in = _ASSIGN(sdcard.dma_out.arvalid_in());
        tribe.axi_in[1].araddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)(uint32_t)sdcard.dma_out.araddr_in());
        tribe.axi_in[1].arid_in = _ASSIGN((u<4>)(uint32_t)sdcard.dma_out.arid_in());
        tribe.axi_in[1].rready_in = _ASSIGN(sdcard.dma_out.rready_in());
#if defined(ENABLE_ZICSR) && defined(ENABLE_ISR)
        tribe.clint_msip_in = clint.msip_out;
        tribe.clint_mtip_in = clint.mtip_out;
        tribe.time_lo_in = clint.debug_mtime_lo_out;
        tribe.time_hi_in = clint.debug_mtime_hi_out;
        tribe.external_irq_in = plic.external_irq_out;
#endif
        tribe.__inst_name = __inst_name + "/tribe";
        tribe._assign();

        AXI4_DRIVER_FROM(axi_out[0], tribe.axi_out[0]);
        AXI4_DRIVER_FROM(axi_out[1], tribe.axi_out[1]);
        AXI4_DRIVER_FROM(mem2.axi_in, tribe.axi_out[2]);
        AXI4_DRIVER_FROM(iospace.slave_in, tribe.axi_out[3]);
        AXI4_RESPONDER_FROM(accelerator.dma_out, tribe.axi_in[0]);
        AXI4_RESPONDER_FROM(sdcard.dma_out, tribe.axi_in[1]);

        mem2.debugen_in = debugen_in;
        mem2.__inst_name = __inst_name + "/mem2";
        mem2._assign();
        AXI4_RESPONDER_FROM(tribe.axi_out[2], mem2.axi_in);

        iospace.region_base_in[0] = _ASSIGN((uint32_t)0);
        iospace.region_size_in[0] = _ASSIGN((uint32_t)0x100);
        iospace.region_base_in[1] = _ASSIGN((uint32_t)0x100);
        iospace.region_size_in[1] = _ASSIGN((uint32_t)0xC000);
        iospace.region_base_in[2] = _ASSIGN((uint32_t)0xC100);
        iospace.region_size_in[2] = _ASSIGN((uint32_t)0x1000);
        iospace.region_base_in[3] = _ASSIGN((uint32_t)0xD100);
        iospace.region_size_in[3] = _ASSIGN((uint32_t)0x100);
        iospace.region_base_in[4] = _ASSIGN((uint32_t)0x10000);
        iospace.region_size_in[4] = _ASSIGN((uint32_t)0x210000);
        iospace.__inst_name = __inst_name + "/iospace";
        iospace._assign();
        AXI4_DRIVER_FROM(uart.axi_in, iospace.masters_out[0]);
        AXI4_DRIVER_FROM(clint.axi_in, iospace.masters_out[1]);
        AXI4_DRIVER_FROM(accelerator.axi_in, iospace.masters_out[2]);
        AXI4_DRIVER_FROM(sdcard.axi_in, iospace.masters_out[3]);
        AXI4_DRIVER_FROM(plic.axi_in, iospace.masters_out[4]);
        uart.uart_rx_valid_in = _ASSIGN(uart_rx_valid_in());
        uart.uart_rx_data_in = _ASSIGN(uart_rx_data_in());
        sdcard.sd_cmd_ready_in = sd_cmd_ready_in;
        sdcard.sd_rsp_valid_in = sd_rsp_valid_in;
        sdcard.sd_rsp_data_in = sd_rsp_data_in;
        sdcard.sd_rsp_last_in = sd_rsp_last_in;
        clint.set_mtimecmp_in = tribe.sbi_set_timer_out;
        clint.set_mtimecmp_lo_in = tribe.sbi_timer_lo_out;
        clint.set_mtimecmp_hi_in = tribe.sbi_timer_hi_out;
        for (i = 0; i < 32; ++i) {
            plic.source_irq_in[i] = _ASSIGN(false);
        }
        plic.source_irq_in[1] = uart.irq_out;
        plic.source_irq_in[2] = sdcard.irq_out;
        uart.__inst_name = __inst_name + "/uart";
        clint.__inst_name = __inst_name + "/clint";
        plic.__inst_name = __inst_name + "/plic";
        accelerator.__inst_name = __inst_name + "/accelerator";
        sdcard.__inst_name = __inst_name + "/sdcard";
        uart._assign();
        clint._assign();
        plic._assign();
        accelerator._assign();
        sdcard._assign();
        AXI4_RESPONDER_FROM(iospace.masters_out[0], uart.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[1], clint.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[2], accelerator.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[3], sdcard.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[4], plic.axi_in);
        AXI4_RESPONDER_FROM_LATE(tribe.axi_out[0], axi_out[0]);
        AXI4_RESPONDER_FROM_LATE(tribe.axi_out[1], axi_out[1]);
        AXI4_RESPONDER_FROM(tribe.axi_out[3], iospace.slave_in);
    }

    void _work(bool reset)
    {
        bool sd_dma_cache_invalidate_ready;
        sd_dma_cache_invalidate_ready = true;
        tribe._work(reset);
        mem2._work(reset);
        iospace._work(reset);
        uart._work(reset);
        clint._work(reset);
        plic._work(reset);
        accelerator._work(reset);
        sdcard._work(reset);
#ifdef ENABLE_MMU_TLB
        sd_dma_cache_invalidate_ready =
            !tribe.debug_memory_wait_out() && !tribe.dmem_read_out() && !tribe.dmem_write_out();
#endif
        if (sdcard.dma_write_complete_out()) {
            sd_dma_cache_invalidate_reg._next = true;
        }
        else if (sd_dma_cache_invalidate_reg && sd_dma_cache_invalidate_ready) {
            sd_dma_cache_invalidate_reg._next = false;
        }
        else {
            sd_dma_cache_invalidate_reg._next = sd_dma_cache_invalidate_reg;
        }
        if (reset) {
            sd_dma_cache_invalidate_reg.clr();
        }
    }

    void _work_neg(bool reset)
    {
#ifndef SYNTHESIS
        tribe._work_neg(reset);
#endif
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        tribe._strobe(checkpoint_fd);
        mem2._strobe(checkpoint_fd);
        iospace._strobe(checkpoint_fd);
        uart._strobe(checkpoint_fd);
        clint._strobe(checkpoint_fd);
        plic._strobe(checkpoint_fd);
        accelerator._strobe(checkpoint_fd);
        sdcard._strobe(checkpoint_fd);
        sd_dma_cache_invalidate_reg.strobe(checkpoint_fd);
    }
};

#if !defined(SYNTHESIS)

#ifdef VERILATOR
#define SYSTEM_PORT_VALUE(port) (port)
#define SYSTEM_PERF_VALUE(port) verilator_tribe_perf((uint64_t)(port))
#else
#define SYSTEM_PORT_VALUE(port) (port())
#define SYSTEM_PERF_VALUE(port) (port())
#endif

class SystemTest : public Module
{
    static constexpr size_t AXI_RAM0_DEPTH = TRIBE_MEM_REGION0_SIZE / (TRIBE_L2_AXI_WIDTH / 8);
    static constexpr size_t AXI_RAM1_DEPTH = TRIBE_MEM_REGION1_SIZE / (TRIBE_L2_AXI_WIDTH / 8);

    Axi4Ram<clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH, AXI_RAM0_DEPTH> dram0;
    Axi4Ram<clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH, AXI_RAM1_DEPTH> dram1;

#ifdef VERILATOR
    VERILATOR_MODEL system;
#else
    System system;
#endif

    uint32_t reset_pc = 0;
    uint32_t boot_hartid = 0;
    uint32_t boot_dtb_addr = 0;
    uint32_t boot_priv = 3;
    uint32_t start_mem_addr = 0;
    uint32_t ram_size = DEFAULT_RAM_SIZE;
    uint32_t tohost_addr = 0;
    uint32_t tohost_value = 0;
    bool tohost_done = false;
    bool error = false;
    bool debugen_in = false;

    uint64_t perf_clocks = 0;
    uint64_t perf_stall = 0;
    uint64_t perf_hazard = 0;
    uint64_t perf_dcache_wait = 0;
    uint64_t perf_icache_wait = 0;
    uint64_t perf_branch = 0;

    reg<u1> uart_rx_valid_reg;
    reg<u8> uart_rx_data_reg;
    SDCardVerifFrontend sdcard_verif;

    struct Elf32Header
    {
        unsigned char ident[16];
        uint16_t type;
        uint16_t machine;
        uint32_t version;
        uint32_t entry;
        uint32_t phoff;
        uint32_t shoff;
        uint32_t flags;
        uint16_t ehsize;
        uint16_t phentsize;
        uint16_t phnum;
        uint16_t shentsize;
        uint16_t shnum;
        uint16_t shstrndx;
    } __PACKED;

    struct Elf32ProgramHeader
    {
        uint32_t type;
        uint32_t offset;
        uint32_t vaddr;
        uint32_t paddr;
        uint32_t filesz;
        uint32_t memsz;
        uint32_t flags;
        uint32_t align;
    } __PACKED;

    static void set_ram_byte(std::vector<uint32_t>& ram, uint32_t addr, uint8_t value)
    {
        const uint32_t shift = (addr & 3u) * 8u;
        ram[addr / 4] = (ram[addr / 4] & ~(0xffu << shift)) | (uint32_t(value) << shift);
    }

    bool load_elf(FILE* fbin, std::vector<uint32_t>& ram, size_t& read_bytes, uint32_t mem_base, uint32_t mem_size_bytes, uint32_t& entry, bool elf_phys_override, uint32_t elf_phys_offset)
    {
        static constexpr uint32_t PT_LOAD = 1;
        Elf32Header ehdr = {};
        fseek(fbin, 0, SEEK_SET);
        if (fread(&ehdr, 1, sizeof(ehdr), fbin) != sizeof(ehdr)) {
            return false;
        }
        if (ehdr.ident[0] != 0x7f || ehdr.ident[1] != 'E' || ehdr.ident[2] != 'L' || ehdr.ident[3] != 'F' ||
            ehdr.ident[4] != 1 || ehdr.ident[5] != 1 || ehdr.phentsize != sizeof(Elf32ProgramHeader)) {
            return false;
        }
        entry = elf_phys_override ? ehdr.entry + elf_phys_offset : ehdr.entry;
        for (uint16_t i = 0; i < ehdr.phnum; ++i) {
            Elf32ProgramHeader phdr = {};
            fseek(fbin, ehdr.phoff + i * sizeof(phdr), SEEK_SET);
            if (fread(&phdr, 1, sizeof(phdr), fbin) != sizeof(phdr)) {
                return false;
            }
            const uint32_t type = phdr.type;
            const uint32_t offset = phdr.offset;
            const uint32_t vaddr = phdr.vaddr;
            const uint32_t paddr = phdr.paddr;
            const uint32_t filesz = phdr.filesz;
            if (type != PT_LOAD || filesz == 0) {
                continue;
            }
            const uint32_t phys = elf_phys_override ? (vaddr + elf_phys_offset) : (paddr ? paddr : vaddr);
            if (phys < mem_base || phys - mem_base + filesz > mem_size_bytes) {
                std::print("ELF segment outside SoC DRAM window: paddr={:08x}, mem_base={:08x}, size={}\n", phys, mem_base, filesz);
                return false;
            }
            fseek(fbin, offset, SEEK_SET);
            for (uint32_t byte = 0; byte < filesz; ++byte) {
                int c = fgetc(fbin);
                if (c == EOF) {
                    return false;
                }
                set_ram_byte(ram, phys - mem_base + byte, (uint8_t)c);
            }
            read_bytes += filesz;
        }
        return read_bytes != 0;
    }

public:
    explicit SystemTest(bool debug = false)
    {
        debugen_in = debug;
        sdcard_verif.fill_prbs();
    }

    void drive_uart_rx(bool valid, uint8_t data = 0)
    {
        uart_rx_valid_reg = (u1)valid;
        uart_rx_valid_reg._next = (u1)false;
        if (valid) {
            uart_rx_data_reg = (u8)data;
            uart_rx_data_reg._next = (u8)data;
        }
    }

    void _assign()
    {
#ifndef VERILATOR
        system.debugen_in = debugen_in;
        system.reset_pc_in = _ASSIGN(reset_pc);
        system.boot_hartid_in = _ASSIGN(boot_hartid);
        system.boot_dtb_addr_in = _ASSIGN(boot_dtb_addr);
        system.boot_priv_in = _ASSIGN((u<2>)boot_priv);
        system.memory_base_in = _ASSIGN(start_mem_addr);
        system.memory_size_in = _ASSIGN((uint32_t)MAX_RAM_SIZE);
        system.mem_region_size_in[0] = _ASSIGN((uint32_t)TRIBE_MEM_REGION0_SIZE);
        system.mem_region_size_in[1] = _ASSIGN((uint32_t)TRIBE_MEM_REGION1_SIZE);
        system.mem_region_size_in[2] = _ASSIGN((uint32_t)TRIBE_MEM_REGION2_SIZE);
        system.mem_region_size_in[3] = _ASSIGN((uint32_t)TRIBE_IO_REGION_SIZE);
        system.uart_rx_valid_in = _ASSIGN((bool)uart_rx_valid_reg);
        system.uart_rx_data_in = _ASSIGN((uint8_t)uart_rx_data_reg);
        system.sd_cmd_ready_in = sdcard_verif.sd_cmd_ready_out;
        system.sd_rsp_valid_in = sdcard_verif.sd_rsp_valid_out;
        system.sd_rsp_data_in = sdcard_verif.sd_rsp_data_out;
        system.sd_rsp_last_in = sdcard_verif.sd_rsp_last_out;
        sdcard_verif.sd_cmd_valid_in = system.sd_cmd_valid_out;
        sdcard_verif.sd_cmd_data_in = system.sd_cmd_data_out;
        sdcard_verif.sd_cmd_last_in = system.sd_cmd_last_out;
        sdcard_verif.sd_rsp_ready_in = system.sd_rsp_ready_out;
        system.__inst_name = __inst_name + "/system";
        system._assign();
        AXI4_DRIVER_FROM(dram0.axi_in, system.axi_out[0]);
        AXI4_DRIVER_FROM(dram1.axi_in, system.axi_out[1]);
#else
        AXI4_DRIVER_FROM_VERILATOR_CONST(dram0.axi_in, system, 0, u<clog2(MAX_RAM_SIZE)>, verilator_wide_to_logic);
        AXI4_DRIVER_FROM_VERILATOR_CONST(dram1.axi_in, system, 1, u<clog2(MAX_RAM_SIZE)>, verilator_wide_to_logic);
        sdcard_verif.sd_cmd_valid_in = _ASSIGN((bool)system.sd_cmd_valid_out);
        sdcard_verif.sd_cmd_data_in = _ASSIGN((u<8>)(uint8_t)system.sd_cmd_data_out);
        sdcard_verif.sd_cmd_last_in = _ASSIGN((bool)system.sd_cmd_last_out);
        sdcard_verif.sd_rsp_ready_in = _ASSIGN((bool)system.sd_rsp_ready_out);
#endif
        dram0.debugen_in = debugen_in;
        dram1.debugen_in = debugen_in;
        dram0.__inst_name = __inst_name + "/dram0";
        dram1.__inst_name = __inst_name + "/dram1";
        sdcard_verif.__inst_name = __inst_name + "/sdcard_verif";
        dram0._assign();
        dram1._assign();
        sdcard_verif._assign();
#ifndef VERILATOR
        AXI4_RESPONDER_FROM(system.axi_out[0], dram0.axi_in);
        AXI4_RESPONDER_FROM(system.axi_out[1], dram1.axi_in);
#endif
    }

    void _work(bool reset)
    {
#ifdef VERILATOR
        system.debugen_in = debugen_in;
        system.reset_pc_in = reset_pc;
        system.boot_hartid_in = boot_hartid;
        system.boot_dtb_addr_in = boot_dtb_addr;
        system.boot_priv_in = boot_priv;
        system.memory_base_in = start_mem_addr;
        system.memory_size_in = MAX_RAM_SIZE;
        system.mem_region_size_in[0] = TRIBE_MEM_REGION0_SIZE;
        system.mem_region_size_in[1] = TRIBE_MEM_REGION1_SIZE;
        system.mem_region_size_in[2] = TRIBE_MEM_REGION2_SIZE;
        system.mem_region_size_in[3] = TRIBE_IO_REGION_SIZE;
        system.uart_rx_valid_in = (bool)uart_rx_valid_reg;
        system.uart_rx_data_in = (uint8_t)uart_rx_data_reg;
        system.sd_cmd_ready_in = sdcard_verif.sd_cmd_ready_out();
        system.sd_rsp_valid_in = sdcard_verif.sd_rsp_valid_out();
        system.sd_rsp_data_in = (uint8_t)sdcard_verif.sd_rsp_data_out();
        system.sd_rsp_last_in = sdcard_verif.sd_rsp_last_out();
        system.clk = 0;
        system.reset = reset;
        system.eval();
#else
        system._work(reset);
#endif
        dram0._work(reset);
        dram1._work(reset);
#ifdef VERILATOR
        AXI4_RESPONDER_FROM_VERILATOR(system, dram0.axi_in, 0);
        AXI4_RESPONDER_FROM_VERILATOR(system, dram1.axi_in, 1);
        system.clk = 1;
        system.reset = reset;
        system.eval();
        sdcard_verif._work(reset);
#endif
        if (reset) {
            error = false;
        }
    }

    void _work_neg(bool reset)
    {
#ifdef VERILATOR
        system.clk = 0;
        system.reset = reset;
        system.eval();
#else
        system._work_neg(reset);
#endif
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        checkpoint_value(checkpoint_fd, perf_clocks);
        checkpoint_value(checkpoint_fd, perf_stall);
        checkpoint_value(checkpoint_fd, perf_hazard);
        checkpoint_value(checkpoint_fd, perf_dcache_wait);
        checkpoint_value(checkpoint_fd, perf_icache_wait);
        checkpoint_value(checkpoint_fd, perf_branch);
        checkpoint_value(checkpoint_fd, tohost_addr);
        checkpoint_value(checkpoint_fd, tohost_value);
        checkpoint_value(checkpoint_fd, reset_pc);
        checkpoint_value(checkpoint_fd, boot_hartid);
        checkpoint_value(checkpoint_fd, boot_dtb_addr);
        checkpoint_value(checkpoint_fd, boot_priv);
        checkpoint_value(checkpoint_fd, start_mem_addr);
        checkpoint_value(checkpoint_fd, ram_size);
        checkpoint_value(checkpoint_fd, tohost_done);
        checkpoint_value(checkpoint_fd, _system_clock);
        uart_rx_valid_reg.strobe(checkpoint_fd);
        uart_rx_data_reg.strobe(checkpoint_fd);
#ifndef VERILATOR
        system._strobe(checkpoint_fd);
#endif
        dram0._strobe(checkpoint_fd);
        dram1._strobe(checkpoint_fd);
        if (checkpoint_fd && checkpoint_reading(checkpoint_fd)) {
            sdcard_verif._strobe(checkpoint_fd);
        }
        else {
#ifndef VERILATOR
            sdcard_verif._work(false);
#endif
            sdcard_verif._strobe(checkpoint_fd);
        }
    }

    void perf_sample()
    {
        auto perf = SYSTEM_PERF_VALUE(system.perf_out);
        ++perf_clocks;
        perf_hazard += perf.hazard_stall;
        perf_branch += perf.branch_stall;
        perf_dcache_wait += perf.dcache_wait;
        perf_icache_wait += perf.icache_wait;
        perf_stall += perf.hazard_stall || perf.branch_stall || perf.dcache_wait || perf.icache_wait;
    }

    void perf_print()
    {
        auto percent = [&](uint64_t value) {
            return perf_clocks ? (100.0 * (double)value / (double)perf_clocks) : 0.0;
        };
        std::print("Performance: clocks={}, stalled={:.2f}% ({})"
                   ", hazards={:.2f}% ({})"
                   ", dcache_wait={:.2f}% ({})"
                   ", icache_wait={:.2f}% ({})"
                   ", branching={:.2f}% ({})\n",
            perf_clocks, percent(perf_stall), perf_stall, percent(perf_hazard), perf_hazard,
            percent(perf_dcache_wait), perf_dcache_wait, percent(perf_icache_wait), perf_icache_wait,
            percent(perf_branch), perf_branch);
    }

    bool run(std::string filename, size_t start_offset, std::string expected_log = "rv32i.log", int max_cycles = 2000000, uint32_t tohost = 0, uint32_t mem_base = 0, uint32_t ram_words = DEFAULT_RAM_SIZE, bool raw_program = false, uint32_t boot_hartid_arg = 0, uint32_t boot_dtb_addr_arg = 0, uint32_t boot_priv_arg = 3, bool elf_phys_override = false, uint32_t elf_phys_offset = 0)
    {
#ifdef VERILATOR
        std::print("VERILATOR SystemTest...");
#else
        std::print("CppHDL SystemTest...");
#endif
        FILE* out = fopen("out.txt", "wb");
        if (out) {
            fclose(out);
        }
        tohost_addr = tohost;
        tohost_value = 0;
        tohost_done = false;
        start_mem_addr = mem_base;
        reset_pc = mem_base;
        boot_hartid = boot_hartid_arg;
        boot_dtb_addr = boot_dtb_addr_arg;
        boot_priv = boot_priv_arg;
        ram_size = ram_words;
        if (ram_size == 0 || ram_size > TRIBE_RAM_BYTES / 4) {
            std::print("invalid --ram-size {}; supported range is 1..{} words\n", ram_size, TRIBE_RAM_BYTES / 4);
            return false;
        }

        std::vector<uint32_t> ram(MAX_RAM_SIZE / 4);
        FILE* fbin = fopen(filename.c_str(), "rb");
        if (!fbin) {
            std::print("can't open file '{}'\n", filename);
            return false;
        }
        size_t read_bytes = 0;
        uint32_t elf_entry = 0;
        if (!raw_program && load_elf(fbin, ram, read_bytes, start_mem_addr, ram_size * 4, elf_entry, elf_phys_override, elf_phys_offset)) {
            reset_pc = elf_entry;
            std::print("Reading ELF program into memory (size: {})\n", read_bytes);
        }
        else {
            fseek(fbin, start_offset, SEEK_SET);
            read_bytes = fread(ram.data(), 1, 4 * ram_size, fbin);
            std::print("Reading raw program into memory (size: {}, offset: {})\n", read_bytes, start_offset);
        }
        fclose(fbin);

        const size_t active_lines = (ram_size * 4 + (TRIBE_L2_AXI_WIDTH / 8) - 1) / (TRIBE_L2_AXI_WIDTH / 8);
        for (size_t line_idx = 0; line_idx < active_lines; ++line_idx) {
            logic<TRIBE_L2_AXI_WIDTH> line = 0;
            for (size_t word = 0; word < (TRIBE_L2_AXI_WIDTH / 8) / 4; ++word) {
                const size_t addr = line_idx * ((TRIBE_L2_AXI_WIDTH / 8) / 4) + word;
                line.bits(word * 32 + 31, word * 32) = ram[addr];
            }
            if (line_idx < AXI_RAM0_DEPTH) {
                dram0.ram.buffer[line_idx] = line;
            }
            else if (line_idx < AXI_RAM0_DEPTH + AXI_RAM1_DEPTH) {
                dram1.ram.buffer[line_idx - AXI_RAM0_DEPTH] = line;
            }
#ifndef VERILATOR
            else {
                system.mem2.ram.buffer[line_idx - AXI_RAM0_DEPTH - AXI_RAM1_DEPTH] = line;
            }
#endif
        }

        __inst_name = "system_test";
        _assign();
        _strobe();
        ++_system_clock;
        _work(1);
        _work_neg(1);

        auto start = std::chrono::high_resolution_clock::now();
        perf_clocks = perf_stall = perf_hazard = perf_dcache_wait = perf_icache_wait = perf_branch = 0;
        int cycles = max_cycles;
        while (--cycles && !error && !tohost_done) {
            _strobe();
            ++_system_clock;
            perf_sample();
            if (SYSTEM_PORT_VALUE(system.uart_tx_valid_out)) {
                FILE* uart_out = fopen("out.txt", "ab");
                if (uart_out) {
                    fputc((char)SYSTEM_PORT_VALUE(system.uart_tx_data_out), uart_out);
                    fclose(uart_out);
                }
            }
            drive_uart_rx(false);
            _work(0);
            if (tohost_addr && SYSTEM_PORT_VALUE(system.dmem_write_out) && SYSTEM_PORT_VALUE(system.dmem_addr_out) == tohost_addr &&
                SYSTEM_PORT_VALUE(system.dmem_write_mask_out) && SYSTEM_PORT_VALUE(system.dmem_write_data_out)) {
                tohost_value = SYSTEM_PORT_VALUE(system.dmem_write_data_out);
                tohost_done = true;
            }
            _work_neg(0);
        }

        if (tohost_addr) {
            if (!tohost_done) {
                std::print("*** tohost was not written before cycle limit ***\n");
                error = true;
            }
            else if (tohost_value != 1) {
                std::print("*** tohost reported failure value {:08x} ***\n", tohost_value);
                error = true;
            }
        }
        else {
            std::ifstream a(expected_log, std::ios::binary), b("out.txt", std::ios::binary);
            error |= !std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(), std::istreambuf_iterator<char>(b));
        }

        perf_print();
        std::print(" {} ({} microseconds)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

[[maybe_unused]] static std::filesystem::path system_source_root_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

#ifdef VERILATOR
[[maybe_unused]] static std::string shell_quote_path(const std::filesystem::path& path)
{
    std::string text = path.string();
    std::string quoted = "'";
    for (char ch : text) {
        if (ch == '\'') {
            quoted += "'\\''";
        }
        else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

[[maybe_unused]] static std::filesystem::path absolute_from(const std::filesystem::path& base, const std::string& path)
{
    std::filesystem::path p(path);
    return p.is_absolute() ? p : (base / p);
}

[[maybe_unused]] static void use_executable_workdir_if_needed(const char* argv0)
{
    std::filesystem::path exe(argv0 ? argv0 : "");
    if (exe.has_parent_path()) {
        std::filesystem::current_path(exe.parent_path());
    }
}
#endif

[[maybe_unused]] static bool regenerate_system_sv(const std::filesystem::path& source_root)
{
    namespace fs = std::filesystem;
    fs::path cpphdl = fs::current_path() / ".." / "cpphdl";
    if (!fs::exists(cpphdl)) {
        cpphdl = source_root / "build" / "cpphdl";
    }
    if (!fs::exists(cpphdl)) {
        std::print("can't find cpphdl generator near build directory or source root\n");
        return false;
    }

    std::string command;
    command += shell_quote_path(cpphdl);
    command += " " + shell_quote_path(source_root / "tribe" / "SoC" / "System.cpp");
    command += " -DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
    command += " -DTRIBE_RAM_BYTES_CONFIG=" + std::to_string(TRIBE_RAM_BYTES);
    command += " -DTRIBE_IO_REGION_SIZE_CONFIG=" + std::to_string(TRIBE_IO_REGION_SIZE);
    command += " -I " + shell_quote_path(source_root / "include");
    command += " -I " + shell_quote_path(source_root / "tribe");
    command += " -I " + shell_quote_path(source_root / "tribe" / "common");
    command += " -I " + shell_quote_path(source_root / "tribe" / "spec");
    command += " -I " + shell_quote_path(source_root / "tribe" / "cache");
    command += " -I " + shell_quote_path(source_root / "tribe" / "devices");
    if (const char* toolchain_args = std::getenv("CPPHDL_TOOLCHAIN_ARGS")) {
        command += " ";
        command += toolchain_args;
    }
    return std::system(command.c_str()) == 0;
}

#if !defined(NO_MAINFILE)
int main(int argc, char** argv)
{
    const std::filesystem::path original_cwd = std::filesystem::current_path();
    bool debug = false;
    bool noveril = false;
    std::string program = "rv32i.elf";
    std::string expected_log = "rv32i.log";
    bool program_arg = false;
    bool log_arg = false;
    bool raw_program = false;
    size_t start_offset = 0x37c;
    int max_cycles = 2000000;
    uint32_t tohost = 0;
    uint32_t start_mem_addr = 0;
    uint32_t ram_size = DEFAULT_RAM_SIZE;
    uint32_t boot_hartid = 0;
    uint32_t boot_dtb_addr = 0;
    uint32_t boot_priv = 3;
    bool elf_phys_override = false;
    uint32_t elf_phys_offset = 0;
    int only = -1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug") == 0) debug = true;
        else if (strcmp(argv[i], "--noveril") == 0) noveril = true;
        else if (strcmp(argv[i], "--program") == 0 && i + 1 < argc) { program = argv[++i]; program_arg = true; raw_program = false; }
        else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) { expected_log = argv[++i]; log_arg = true; }
        else if (strcmp(argv[i], "--raw") == 0) raw_program = true;
        else if (strcmp(argv[i], "--elf") == 0) raw_program = false;
        else if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc) start_offset = std::stoul(argv[++i], nullptr, 0);
        else if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) max_cycles = std::stoi(argv[++i]);
        else if (strcmp(argv[i], "--tohost") == 0 && i + 1 < argc) tohost = std::stoul(argv[++i], nullptr, 0);
        else if (strcmp(argv[i], "--start-mem-addr") == 0 && i + 1 < argc) start_mem_addr = std::stoul(argv[++i], nullptr, 0);
        else if (strcmp(argv[i], "--ram-size") == 0 && i + 1 < argc) ram_size = std::stoul(argv[++i], nullptr, 0);
        else if (strcmp(argv[i], "--boot-hartid") == 0 && i + 1 < argc) boot_hartid = std::stoul(argv[++i], nullptr, 0);
        else if (strcmp(argv[i], "--boot-dtb-addr") == 0 && i + 1 < argc) boot_dtb_addr = std::stoul(argv[++i], nullptr, 0);
        else if (strcmp(argv[i], "--boot-priv") == 0 && i + 1 < argc) {
            std::string value = argv[++i];
            boot_priv = (value == "m" || value == "M") ? 3 : ((value == "s" || value == "S") ? 1 : std::stoul(value, nullptr, 0));
        }
        else if (strcmp(argv[i], "--elf-phys-offset") == 0 && i + 1 < argc) { elf_phys_offset = std::stoul(argv[++i], nullptr, 0); elf_phys_override = true; }
        else if (strcmp(argv[i], "--elf-phys-base") == 0 && i + 1 < argc) { elf_phys_offset = std::stoul(argv[++i], nullptr, 0) - 0xc0000000u; elf_phys_override = true; }
        else if (argv[i][0] != '-') only = atoi(argv[argc - 1]);
    }

    if (program_arg) program = absolute_from(original_cwd, program).string();
    if (log_arg) expected_log = absolute_from(original_cwd, expected_log).string();
    use_executable_workdir_if_needed(argv[0]);

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building System Verilator simulation... ======================================================\n";
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        const auto source_root = system_source_root_dir();
        if (!regenerate_system_sv(source_root)) {
            ok = false;
        }
        else {
            ok &= VerilatorCompile(__FILE__, "System", {"Predef_pkg",
                "Amo_pkg", "Trap_pkg", "State_pkg", "Rv32i_pkg", "Rv32ic_pkg", "Rv32im_pkg", "Rv32ia_pkg", "Zicsr_pkg",
                "Alu_pkg", "Br_pkg", "Sys_pkg", "Csr_pkg", "Mem_pkg", "Wb_pkg", "L1CachePerf_pkg", "TribePerf_pkg",
                "File", "RAM1PORT", "Memory", "Axi4Ram", "L1Cache", "L2Cache", "BranchPredictor", "InterruptController",
                "Decode", "Execute", "ExecuteMem", "CSR", "MMU_TLB", "Writeback", "WritebackMem",
                "Tribe", "Axi4RegionMux", "NS16550A", "CLINT", "PLIC", "Accelerator", "SDController"}, {
                    (source_root / "include").string(),
                    (source_root / "tribe").string(),
                    (source_root / "tribe" / "common").string(),
                    (source_root / "tribe" / "spec").string(),
                    (source_root / "tribe" / "cache").string(),
                    (source_root / "tribe" / "devices").string()});
        }
        std::string verilator_run = "System/obj_dir/VSystem";
        if (debug) verilator_run += " --debug";
        verilator_run += " --program " + shell_quote_path(absolute_from(std::filesystem::current_path(), program));
        verilator_run += " --log " + shell_quote_path(absolute_from(std::filesystem::current_path(), expected_log));
        verilator_run += " --offset " + std::to_string(start_offset);
        verilator_run += " --cycles " + std::to_string(max_cycles);
        verilator_run += " --start-mem-addr " + std::to_string(start_mem_addr);
        verilator_run += " --ram-size " + std::to_string(ram_size);
        verilator_run += " --boot-hartid " + std::to_string(boot_hartid);
        verilator_run += " --boot-dtb-addr " + std::to_string(boot_dtb_addr);
        verilator_run += " --boot-priv " + std::to_string(boot_priv);
        if (raw_program) verilator_run += " --raw";
        if (tohost) verilator_run += " --tohost " + std::to_string(tohost);
        if (elf_phys_override) verilator_run += " --elf-phys-offset " + std::to_string(elf_phys_offset);
        verilator_run += " 0";
        ok = ok && ((only != -1 && only != 0) || std::system(verilator_run.c_str()) == 0);
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && ((only != -1 && only != 0) || SystemTest(debug).run(program, start_offset, expected_log, max_cycles, tohost, start_mem_addr, ram_size, raw_program, boot_hartid, boot_dtb_addr, boot_priv, elf_phys_override, elf_phys_offset)));
}
#endif

#endif
