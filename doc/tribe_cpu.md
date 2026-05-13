# Tribe RISC-V CPU

## About

Tribe is a RV32 RISC-V CPU model written in the CppHDL C++ dialect. The model is intended to run both as a native C++ simulation and as generated SystemVerilog through Verilator. The core implements a small in-order pipeline with instruction and data L1 caches, a shared L2 cache, CSR/trap support, optional interrupt support, optional Sv32 MMU/TLB support, and AXI4-style memory and device interfaces.

The base implementation targets 32-bit integer software. Build-time configuration in `tribe/Config.h` selects optional blocks such as `ENABLE_ZICSR`, `ENABLE_RV32IA`, `ENABLE_ISR`, and `ENABLE_MMU_TLB`. The L2 memory data width is selected with `L2_AXI_WIDTH`; common test targets use 64, 128, and 256 bits. Main memory starts at `memory_base_in`, has runtime size `memory_size_in`, and is split into four L2 memory/device regions. The last region is used as uncached IO/MMIO space.

## Structure

The top-level CPU is the `Tribe` module in `tribe/main.cpp`. It instantiates these core blocks:

| Block | Source | Role |
| --- | --- | --- |
| `Decode` | `tribe/Decode.h` | Instruction decode, register source selection, and `State` construction. |
| `Execute` | `tribe/Execute.h` | ALU operation and branch resolution. |
| `ExecuteMem` | `tribe/ExecuteMem.h` | Memory request generation, split access handling, and optional atomics. |
| `WritebackMem` | `tribe/WritebackMem.h` | Load response capture, split-load assembly, and store-to-load forwarding. |
| `Writeback` | `tribe/Writeback.h` | Architectural register writeback formatting. |
| `CSR` | `tribe/CSR.h` | CSR, privilege, trap, and return-from-trap state. |
| `MMU_TLB` | `tribe/MMU_TLB.h` | Optional Sv32 instruction/data address translation and page-table walking. |
| `InterruptController` | `tribe/InterruptController.h` | Optional CLINT interrupt routing into CSR trap input. |
| `File<32,32>` | `File.h` | Integer register file. |
| `L1Cache` | `tribe/cache/L1Cache.h` | Separate instruction and data L1 caches. |
| `L2Cache` | `tribe/cache/L2Cache.h` | Shared coherent L2 cache, AXI memory/device master ports, and external AXI slave ports. |
| `BranchPredictor` | `tribe/BranchPredictor.h` | Small direct-mapped branch predictor. |

The test and SoC wrapper code in `tribe/main.cpp` also instantiates RAM regions, an IO region mux, UART/CLINT devices, and optional accelerator test plumbing. Those wrappers are not part of the `Tribe` CPU module itself, but they define the default executable simulation environment.

## Main (Core)

`Tribe` is a three-stage in-order CPU core plus a fetch path. The pipeline state array tracks Decode-to-Execute and Execute-to-Writeback state; instruction fetch is driven directly from `pc`, I-cache output, branch prediction, and MMU translation. The `State` structure carries decoded instruction control fields through the core.

Top-level `Tribe` ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `dmem_write_out` | `bool` | Output | Debug/trace indication of a data-memory write. |
| `dmem_write_data_out` | `uint32_t` | Output | Debug/trace write data for the current data-memory write. |
| `dmem_write_mask_out` | `uint8_t` | Output | Debug/trace byte mask for the current data-memory write. |
| `dmem_read_out` | `bool` | Output | Debug/trace indication of a data-memory read. |
| `dmem_addr_out` | `uint32_t` | Output | Debug/trace current data-memory address. |
| `imem_read_addr_out` | `uint32_t` | Output | Debug/trace current instruction-memory address. |
| `debug_immu_ptw_read_out` | `bool` | Output | MMU debug: IMMU page-table-walk read request, when MMU is enabled. |
| `debug_immu_ptw_addr_out` | `uint32_t` | Output | MMU debug: IMMU page-table-walk address. |
| `debug_immu_busy_out` | `bool` | Output | MMU debug: IMMU is walking or waiting. |
| `debug_immu_fault_out` | `bool` | Output | MMU debug: IMMU fault state. |
| `debug_immu_last_addr_out` | `uint32_t` | Output | MMU debug: last IMMU PTE address. |
| `debug_immu_last_pte_out` | `uint32_t` | Output | MMU debug: last IMMU PTE value. |
| `debug_dmmu_ptw_read_out` | `bool` | Output | MMU debug: DMMU page-table-walk read request. |
| `debug_dmmu_ptw_addr_out` | `uint32_t` | Output | MMU debug: DMMU page-table-walk address. |
| `debug_dmmu_busy_out` | `bool` | Output | MMU debug: DMMU is walking or waiting. |
| `debug_dmmu_fault_out` | `bool` | Output | MMU debug: DMMU fault state. |
| `debug_mmu_ptw_word_out` | `uint32_t` | Output | MMU debug: 32-bit PTE word selected from L2 data. |
| `debug_pc_out` | `uint32_t` | Output | MMU debug: current architectural PC. |
| `sbi_set_timer_out` | `bool` | Output | Local emulation of legacy SBI `set_timer` ECALL. |
| `sbi_timer_lo_out` | `uint32_t` | Output | Low 32 bits of the requested SBI timer compare value. |
| `sbi_timer_hi_out` | `uint32_t` | Output | High 32 bits of the requested SBI timer compare value. |
| `reset_pc_in` | `uint32_t` | Input | Reset PC. |
| `boot_hartid_in` | `uint32_t` | Input | Reset value for `a0`, conventionally hart id. |
| `boot_dtb_addr_in` | `uint32_t` | Input | Reset value for `a1`, conventionally device-tree address. |
| `boot_priv_in` | `u<2>` | Input | Initial privilege mode when CSR support is enabled. |
| `memory_base_in` | `uint32_t` | Input | Physical base address of the attached memory map. |
| `memory_size_in` | `uint32_t` | Input | Total byte size of RAM plus IO region visible to L2. |
| `mem_region_size_in[4]` | `uint32_t[L2_MEM_PORTS]` | Input | Cumulative sizes of the four L2 memory/device regions. |
| `clint_msip_in` | `bool` | Input | CLINT machine software interrupt pending input, when interrupts are enabled. |
| `clint_mtip_in` | `bool` | Input | CLINT machine timer interrupt pending input, when interrupts are enabled. |
| `axi_in[4]` | `Axi4If<clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH>[L2_MEM_PORTS]` | AXI slave | External coherent AXI master access into L2. Used by DMA-style devices. |
| `axi_out[4]` | `Axi4If<clog2(MAX_RAM_SIZE), 4, TRIBE_L2_AXI_WIDTH>[L2_MEM_PORTS]` | AXI master | L2 master ports toward RAM and device regions. |
| `perf_out` | `TribePerf` | Output | Per-cycle performance/stall/cache debug snapshot. |
| `debugen_in` | `bool` | Input | C++ simulation debug print enable flag. |

The main combinational control in `Tribe` handles pipeline hazards, branch redirects, global memory wait, MMU page-table-walk arbitration, trap redirection, SBI timer emulation, I-cache/TLB invalidation, and late load forwarding. The DMMU and IMMU page-table walkers share the L2 data-side port with normal data-cache traffic; DMMU requests have priority over IMMU requests after normal data-cache reads/writes. The data MMU has a direct physical window for the IO region so MMIO is not translated by Sv32.

### Decode

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `pc_in` | `uint32_t` | Input | PC associated with `instr_in`. |
| `instr_valid_in` | `bool` | Input | Indicates that `instr_in` is valid for decode. |
| `instr_in` | `uint32_t` | Input | 32-bit instruction word from I-cache; compressed instructions are decoded from low bits. |
| `regs_data0_in` | `uint32_t` | Input | Register-file value for decoded `rs1`. |
| `regs_data1_in` | `uint32_t` | Input | Register-file value for decoded `rs2`. |
| `rs1_out` | `u<5>` | Output | Decoded source register 1 index for register-file read. |
| `rs2_out` | `u<5>` | Output | Decoded source register 2 index for register-file read. |
| `state_out` | `State` | Output | Decoded `State` carrying operands and control fields into execute. |

`Decode` selects the active decoder specification from `Rv32im`, `Rv32ia`, and `Zicsr` according to build flags. It fills the pipeline `State`, attaches the current PC, marks validity from `instr_valid_in`, fetches register values, and handles the AUIPC PC operand special case. It is combinational and has no internal register state.

### Execute

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `state_in` | `State` | Input | Execute-stage `State` after forwarding and trap redirection. |
| `alu_result_out` | `uint32_t` | Output | ALU result. For comparisons, upper result bits carry equality information used by branches. |
| `debug_alu_a_out` | `uint32_t` | Output | Debug ALU operand A. |
| `debug_alu_b_out` | `uint32_t` | Output | Debug ALU operand B. |
| `branch_taken_out` | `bool` | Output | Branch/jump taken result. |
| `branch_target_out` | `uint32_t` | Output | Resolved branch or jump target. |

`Execute` contains the ALU and branch decision logic. It supports integer arithmetic, shifts, comparisons, multiply/divide operations, address calculation for memory operations, and branch target calculation. Trap and return redirection are represented as branch-like state before this stage enters `Execute`.

### ExecuteMem

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `state_in` | `State` | Input | Execute-stage `State` after forwarding/trap-return adjustments. |
| `alu_result_in` | `uint32_t` | Input | Effective address or ALU result from `Execute`. |
| `dcache_read_valid_in` | `bool` | Input | Atomic read response valid, when atomics are enabled. |
| `dcache_read_addr_in` | `uint32_t` | Input | Address tag returned with D-cache read data. |
| `dcache_read_expected_addr_in` | `uint32_t` | Input | Expected physical address for atomic read completion. |
| `dcache_read_data_in` | `uint32_t` | Input | D-cache read data used by AMO operations. |
| `mem_stall_in` | `bool` | Input | Holds issued memory request while D-cache/L2 cannot accept it. |
| `hold_in` | `bool` | Input | Holds request metadata while the pipeline waits for writeback. |
| `mem_write_out` | `bool` | Output | Registered store request to D-cache. |
| `mem_write_addr_out` | `uint32_t` | Output | Store address to D-cache. |
| `mem_write_data_out` | `uint32_t` | Output | Store data to D-cache. |
| `mem_write_mask_out` | `uint8_t` | Output | Store byte mask to D-cache. |
| `mem_read_out` | `bool` | Output | Registered load request to D-cache. |
| `mem_read_addr_out` | `uint32_t` | Output | Load address to D-cache. |
| `mem_split_out` | `bool` | Output | Current access crosses a 32-byte L1 line and must be split. |
| `mem_split_busy_out` | `bool` | Output | A delayed second split transaction is pending. |
| `split_load_out` | `bool` | Output | Writeback must assemble a split load from two words. |
| `split_load_low_out` | `uint32_t` | Output | Low aligned address for split-load matching. |
| `split_load_high_out` | `uint32_t` | Output | High aligned address for split-load matching. |
| `atomic_busy_out` | `bool` | Output | Atomic LR/SC/AMO sequence is active, when atomics are enabled. |
| `atomic_sc_result_out` | `uint32_t` | Output | Store-conditional architectural result, 0 for success and 1 for failure. |

`ExecuteMem` turns decoded memory operations into D-cache requests. It detects accesses that cross an L1 cache line and issues the first and second aligned transactions in order, while providing metadata for `WritebackMem` to reconstruct split loads. With `ENABLE_RV32IA`, it also implements LR/SC reservation tracking and AMO read-modify-write sequencing.

### Writeback

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `state_in` | `State` | Input | Writeback-stage `State`. |
| `alu_result_in` | `uint32_t` | Input | ALU result to write for ALU instructions. |
| `mem_data_in` | `uint32_t` | Input | Raw aligned load data. |
| `mem_data_hi_in` | `uint32_t` | Input | High word for legacy split-load interface; normally zero after `WritebackMem` assembly. |
| `mem_addr_in` | `uint32_t` | Input | Load address used for byte/halfword interpretation. |
| `mem_split_in` | `bool` | Input | Indicates split-load result selection. |
| `regs_data_out` | `uint32_t` | Output | Final architectural value for the integer register file. |
| `regs_wr_id_out` | `uint8_t` | Output | Destination register index. |
| `regs_write_out` | `bool` | Output | Register-file write enable. |

`Writeback` formats the final value for the integer register file. It selects PC+2, PC+4, ALU result, or memory result according to `State::wb_op`, and sign- or zero-extends byte and halfword loads based on `funct3`. Register x0 is still protected by the register file and write-enable logic around this stage.

### WritebackMem

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `state_in` | `State` | Input | Writeback-stage `State`. |
| `alu_result_in` | `uint32_t` | Input | Address expected for the active load response. |
| `split_load_in` | `bool` | Input | Indicates that two aligned read responses must be assembled. |
| `split_load_low_addr_in` | `uint32_t` | Input | Low aligned address of a split load. |
| `split_load_high_addr_in` | `uint32_t` | Input | High aligned address of a split load. |
| `dcache_read_valid_in` | `bool` | Input | D-cache read response valid. |
| `dcache_read_addr_in` | `uint32_t` | Input | D-cache response address tag. |
| `dcache_read_data_in` | `uint32_t` | Input | D-cache read data. |
| `dcache_write_valid_in` | `bool` | Input | Store request accepted/visible to D-cache. |
| `dcache_write_addr_in` | `uint32_t` | Input | Store address for forwarding history. |
| `dcache_write_data_in` | `uint32_t` | Input | Store data for forwarding history. |
| `dcache_write_mask_in` | `uint8_t` | Input | Store byte mask for forwarding history. |
| `hold_in` | `bool` | Input | Keeps pending load information while pipeline is stalled. |
| `load_ready_out` | `bool` | Output | Load result is available for register writeback and forwarding. |
| `load_raw_out` | `uint32_t` | Output | Raw load data before architectural sign/zero extension. |
| `load_result_out` | `uint32_t` | Output | Architecturally extended load value for late forwarding. |
| `wb_mem_data_out` | `uint32_t` | Output | Load data passed to `Writeback`. |
| `wb_mem_data_hi_out` | `uint32_t` | Output | High split-load word passed to `Writeback`. |

`WritebackMem` is the one-cycle memory/writeback boundary. It captures D-cache responses, waits until both halves of a split load are available, assembles unaligned words, and performs short store-to-load forwarding. Forwarding is disabled for atomics so AMO semantics are not hidden by local store history.

## Peripheral

### L1Cache

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `write_in` | `bool` | Input | CPU write request. The data L1 is write-through to L2. |
| `write_data_in` | `uint32_t` | Input | 32-bit write data. |
| `write_mask_in` | `uint8_t` | Input | Byte mask for the 32-bit write. |
| `read_in` | `bool` | Input | CPU read request. |
| `addr_in` | `uint32_t` | Input | CPU byte address. |
| `read_data_out` | `uint32_t` | Output | 32-bit CPU read result. |
| `read_addr_out` | `uint32_t` | Output | Address tag associated with `read_data_out`. |
| `read_valid_out` | `bool` | Output | Read response valid. |
| `busy_out` | `bool` | Output | Cache cannot accept or complete the current request. |
| `stall_in` | `bool` | Input | Front-end/pipeline stall input. |
| `flush_in` | `bool` | Input | Redirect flush input, used by I-cache fetch. |
| `invalidate_in` | `bool` | Input | Clears valid tags for FENCE.I/SFENCE.VMA. |
| `cache_disable_in` | `bool` | Input | Forces direct, uncached reads for MMIO/direct paths. |
| `mem_write_out` | `bool` | Output | Write-through store request to L2. |
| `mem_write_data_out` | `uint32_t` | Output | L2 write data. |
| `mem_write_mask_out` | `uint8_t` | Output | L2 write byte mask. |
| `mem_read_out` | `bool` | Output | L2 read request for refill or direct read. |
| `mem_addr_out` | `uint32_t` | Output | L2 address for refill or direct read. |
| `mem_read_data_in` | `logic<PORT_BITWIDTH>` | Input | L2 read beat, width `PORT_BITWIDTH`. |
| `mem_wait_in` | `bool` | Input | L2 wait/hold response. |
| `perf_out` | `L1CachePerf` | Output | L1 hit/wait/state performance snapshot. |

`L1Cache` is a small set-associative cache with 32-byte lines. It is instantiated twice: `icache` with ID 0 and `dcache` with ID 1. The line is stored in even/odd 16-bit RAM halves, then assembled into 32-bit words on reads and refills. The data L1 is write-through; it sends 32-bit stores directly to L2 and does not own dirty state.

The L1 refill port width equals the selected L2 AXI width and may be smaller than the cache line. A miss refills a line over one or more `PORT_BITWIDTH` beats. For data reads that would cross the final word of a cache line, the CPU split logic handles the access before L2 sees a cacheable fill. MMIO/direct reads bypass L1 caching. Cached RAM direct reads stay aligned to the containing L2 beat so dirty data already present in L2 is used instead of stale backing RAM.

### L2Cache

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `i_read_in` | `bool` | Input | Instruction-side read request from I-cache. |
| `i_write_in` | `bool` | Input | Instruction-side write request; normally false. |
| `i_addr_in` | `uint32_t` | Input | Instruction-side address. |
| `i_write_data_in` | `uint32_t` | Input | Instruction-side write data. |
| `i_write_mask_in` | `uint8_t` | Input | Instruction-side byte mask. |
| `i_read_data_out` | `logic<PORT_BITWIDTH>` | Output | Instruction-side read beat. |
| `i_wait_out` | `bool` | Output | Instruction-side wait. |
| `d_read_in` | `bool` | Input | Data-side read request from D-cache or MMU page-table walker. |
| `d_write_in` | `bool` | Input | Data-side write request from D-cache. |
| `d_addr_in` | `uint32_t` | Input | Data-side address. |
| `d_write_data_in` | `uint32_t` | Input | Data-side 32-bit write data. |
| `d_write_mask_in` | `uint8_t` | Input | Data-side byte mask. |
| `d_read_data_out` | `logic<PORT_BITWIDTH>` | Output | Data-side read beat. |
| `d_wait_out` | `bool` | Output | Data-side wait. |
| `memory_base_in` | `uint32_t` | Input | Base physical address of the memory map. |
| `memory_size_in` | `uint32_t` | Input | Total visible bytes across all regions. |
| `mem_region_size_in[MEM_PORTS]` | `uint32_t[MEM_PORTS]` | Input | Per-region byte sizes. Regions are contiguous, not interleaved. |
| `mem_region_uncached_in[MEM_PORTS]` | `bool[MEM_PORTS]` | Input | Per-region bypass flag; device/MMIO regions are uncached. |
| `axi_in[MEM_PORTS]` | `Axi4If<MEM_ADDR_BITS, 4, PORT_BITWIDTH>[MEM_PORTS]` | AXI slave | Coherent slave ports for external AXI masters such as DMA. |
| `axi_out[MEM_PORTS]` | `Axi4If<MEM_ADDR_BITS, 4, PORT_BITWIDTH>[MEM_PORTS]` | AXI master | Master ports to RAM/device regions. |

`L2Cache` is a 4-way set-associative shared cache with 32-byte lines and a configurable memory beat width. It arbitrates coherent external AXI slave requests, data-cache requests, and instruction-cache requests onto one L2 tag/data RAM. External AXI masters can read cached data written by the CPU, and the CPU can read data written through an external AXI slave port.

L2 memory ports are split into contiguous regions. Address selection subtracts `memory_base_in`, chooses a region by cumulative region size, and then forwards a local byte address to the selected AXI master port. Cached regions allocate and evict dirty cache lines through AXI. Uncached regions bypass the tag/data RAM and issue single-beat MMIO reads and writes, so device regions do not infer cache storage.

### BranchPredictor

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `lookup_valid_in` | `bool` | Input | Valid decoded branch lookup. |
| `lookup_pc_in` | `uint32_t` | Input | Branch PC for lookup. |
| `lookup_target_in` | `uint32_t` | Input | Newly decoded branch target. |
| `lookup_fallthrough_in` | `uint32_t` | Input | Sequential next PC. |
| `lookup_br_op_in` | `u<4>` | Input | Branch operation type. |
| `predict_taken_out` | `bool` | Output | Predicted branch-taken flag. |
| `predict_next_out` | `uint32_t` | Output | Predicted next PC. |
| `update_valid_in` | `bool` | Input | Execute-stage branch update valid. |
| `update_pc_in` | `uint32_t` | Input | Resolved branch PC. |
| `update_taken_in` | `bool` | Input | Resolved branch direction. |
| `update_target_in` | `uint32_t` | Input | Resolved branch target. |

`BranchPredictor` is a direct-mapped predictor with saturating counters, valid bits, tags, and target storage. Conditional branches use the counter state. JAL, JALR, and JR are predicted taken. On execute resolution, the predictor updates direction and target state.

### Interrupt Controller

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `mstatus_in` | `uint32_t` | Input | CSR `mstatus` bits used for global interrupt enables. |
| `mie_in` | `uint32_t` | Input | CSR interrupt enable mask. |
| `mideleg_in` | `uint32_t` | Input | Machine interrupt delegation mask. |
| `mip_sw_in` | `uint32_t` | Input | Software-writable pending bits from CSR state. |
| `priv_in` | `u<2>` | Input | Current privilege mode. |
| `clint_msip_in` | `bool` | Input | Machine software interrupt from CLINT. |
| `clint_mtip_in` | `bool` | Input | Machine timer interrupt from CLINT. |
| `mip_out` | `uint32_t` | Output | Merged interrupt-pending bits. |
| `interrupt_valid_out` | `bool` | Output | An enabled interrupt should be taken. |
| `interrupt_cause_out` | `uint32_t` | Output | Selected interrupt cause number. |
| `interrupt_to_supervisor_out` | `bool` | Output | Interrupt should trap to supervisor mode by delegation. |

`InterruptController` merges hardware CLINT interrupt inputs with writable CSR pending bits, masks them with `mie`, applies privilege-aware global enable rules from `mstatus`, and reports one pending cause to the CSR/trap block. Machine timer/software interrupt support is the primary hardware path; supervisor and external pending bits can also be routed when CSR tests set them.

### MMU/TLB

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `vaddr_in` | `uint32_t` | Input | Virtual address to translate. |
| `read_in` | `bool` | Input | Load translation request. |
| `write_in` | `bool` | Input | Store translation request. |
| `execute_in` | `bool` | Input | Instruction-fetch translation request. |
| `satp_in` | `uint32_t` | Input | Current `satp`; Sv32 is active when MODE is 1. |
| `priv_in` | `u<2>` | Input | Current privilege mode. |
| `direct_base_in` | `uint32_t` | Input | Base of direct physical bypass window. |
| `direct_size_in` | `uint32_t` | Input | Size of direct physical bypass window. |
| `fill_in` | `bool` | Input | External/manual TLB fill request. |
| `fill_index_in` | `u<clog2(ENTRIES)>` | Input | External/manual fill index. |
| `fill_vpn_in` | `uint32_t` | Input | External/manual fill virtual page number. |
| `fill_ppn_in` | `uint32_t` | Input | External/manual fill physical page number. |
| `fill_flags_in` | `uint8_t` | Input | External/manual fill PTE flags. |
| `sfence_in` | `bool` | Input | Invalidates cached translations. |
| `mem_read_out` | `bool` | Output | Page-table-walker memory read request. |
| `mem_addr_out` | `uint32_t` | Output | Physical PTE address requested by the walker. |
| `mem_read_data_in` | `uint32_t` | Input | 32-bit PTE data returned by memory. |
| `mem_wait_in` | `bool` | Input | Page-table-walker memory wait. |
| `paddr_out` | `uint32_t` | Output | Translated or bypassed physical address. |
| `translated_out` | `bool` | Output | Translation is active for the current request. |
| `hit_out` | `bool` | Output | TLB hit or translation disabled. |
| `fault_out` | `bool` | Output | Page fault or permission fault. |
| `miss_out` | `bool` | Output | Translation miss requiring a page-table walk. |
| `busy_out` | `bool` | Output | Walker is active or waiting. |
| `debug_last_pte_out` | `uint32_t` | Output | Last PTE captured by the walker. |
| `debug_last_addr_out` | `uint32_t` | Output | Last PTE address captured by the walker. |

`MMU_TLB` implements a small Sv32 TLB with a hardware page-table walker. There are separate instances for instruction fetch and data access. Translation is enabled only when `satp.MODE == 1` and current privilege is not M-mode. A direct mapping window can bypass translation; Tribe uses it for DMMU access to MMIO so device addresses remain physical under an OS.

The walker reads the level-1 PTE from the `satp` root page and, when needed, reads the level-0 PTE. It supports level-1 superpages and level-0 pages, checks valid/write/read combinations, checks A/D/R/W/X/U permissions, and reports instruction/load/store page faults through the main CSR/trap path. `SFENCE.VMA` clears cached translations.

## Devices

Device modules live in `tribe/devices`. They are memory-mapped AXI responders, except `Accelerator`, which also owns an AXI master DMA port. In the default Tribe simulation wrapper, devices are placed behind an IO-space region mux connected to the uncached L2 IO region.

### IOUART

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `axi_in` | `Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH>` | AXI slave | MMIO register access. |
| `uart_valid_out` | `bool` | Output | One-cycle pulse when a byte is written to TXDATA. |
| `uart_data_out` | `uint8_t` | Output | Byte written by software. |

`IOUART` is a minimal UART-like output device. It exposes `TXDATA` at offset `0x00` and `STATUS` at offset `0x04`; status bit 0 is always ready. Writes to `TXDATA` emit `uart_valid_out` and `uart_data_out`. It is useful for simple bare-metal tests.

### NS16550A

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `axi_in` | `Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH>` | AXI slave | MMIO register access to the 16550-style register file. |
| `uart_valid_out` | `bool` | Output | One-cycle pulse when software writes the transmit holding register. |
| `uart_data_out` | `uint8_t` | Output | Transmitted byte. |

`NS16550A` models enough of a 16550-compatible UART for firmware and Linux console probing. It implements the usual register offsets for RBR/THR/DLL, IER/DLM, IIR/FCR, LCR, MCR, LSR, MSR, and SCR. Transmit is modeled as always empty and ready by setting `LSR.THRE` and `LSR.TEMT`; receive is not modeled. DLAB selects divisor latch registers at offsets 0 and 1.

### CLINT

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `axi_in` | `Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH>` | AXI slave | MMIO access to CLINT registers. |
| `set_mtimecmp_in` | `bool` | Input | Direct timer-compare update from local SBI `set_timer` emulation. |
| `set_mtimecmp_lo_in` | `uint32_t` | Input | Low 32 bits for direct `mtimecmp` update. |
| `set_mtimecmp_hi_in` | `uint32_t` | Input | High 32 bits for direct `mtimecmp` update. |
| `msip_out` | `bool` | Output | Machine software interrupt pending. |
| `mtip_out` | `bool` | Output | Machine timer interrupt pending. |

`CLINT` implements the basic RISC-V local interrupt timer registers used by Tribe: `msip`, `mtimecmp`, and `mtime`. The timer increments every CPU model cycle. `mtip_out` is asserted when `mtime >= mtimecmp`; `msip_out` follows bit 0 of the `msip` register. The direct `set_mtimecmp_*` inputs let the core emulate legacy SBI timer calls without requiring firmware support.

### Accelerator

Ports:

| Port | Type | Direction | Description |
| --- | --- | --- | --- |
| `axi_in` | `Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH>` | AXI slave | CPU MMIO control/status and accelerator-local memory access. |
| `dma_out` | `Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH>` | AXI master | DMA memory access through an L2 coherent slave port. |

`Accelerator` is a test device with a small local word memory, a PRBS generator, and a DMA engine. Its control registers include source address, destination address, transfer length, control, status, and PRBS seed. The DMA path is a real AXI master port and is intended to connect to an L2 slave/coherency port, not to a private CPU shortcut. It can copy main memory into accelerator memory, copy accelerator memory back to main memory, and fill its local memory from a PRBS sequence for software-visible data movement tests.
