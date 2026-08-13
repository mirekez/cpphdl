#include "run_cpphdl_testharness_model.h"
#include "run_cpphdl_testharness_model_internal.h"

#include <cstdlib>
#include <cstdio>

void cpphdl_model_configure(uint64_t maxCycles, uint32_t tohost)
{
    cpphdlModel->dut->i_rvfi_tracer.SIM_FINISH = static_cast<uint32_t>(maxCycles);
    cpphdlModel->dut->i_rvfi_tracer.TOHOST_ADDR = tohost;
    cpphdlModel->dut->debug_enable = 1;
}

std::size_t cpphdl_model_commit_lanes()
{
    return std::remove_reference_t<decltype(cpphdlModel->dut->i_cva6_rvfi.rvfi_instr_o)>::COUNT_VALUE;
}

CpphdlCommit cpphdl_model_commit(std::size_t lane)
{
    const auto& instructions = cpphdlModel->dut->i_cva6_rvfi.rvfi_instr_o;
    const auto source = std::as_const(instructions)[lane];
    if (std::getenv("CPPHDL_TRACE_RVFI_RD") != nullptr &&
        static_cast<uint32_t>(static_cast<uint64_t>(source.pc_rdata)) == 0x8000318eU) {
        auto& top = *cpphdlModel->dut;
        auto& core = top.i_ariane.i_cva6;
        const auto issueRd = core.issue_stage_i.commit_instr_o_out__field_rd();
        const auto commitRd = core.commit_stage_i.commit_instr_i_in__field_rd();
        const auto probeInputRd = core.i_cva6_rvfi_probes.commit_instr_i_in__field_rd();
        const auto probeInstr = core.i_cva6_rvfi_probes.rvfi_probes_o_out__field_instr();
        const auto coreInstr = core.rvfi_probes_o_out__field_instr();
        const auto rvfiInputInstr = top.i_cva6_rvfi.rvfi_probes_i_in__field_instr();
        std::fprintf(stderr,
                     "CPPRVFIRD lane=%zu issue=%llu,%llu commit=%llu,%llu "
                     "probe_in=%llu,%llu probe_out=%llu,%llu core_out=%llu,%llu "
                     "rvfi_in=%llu,%llu stored=%llu\n",
                     lane,
                     static_cast<unsigned long long>(static_cast<uint64_t>(issueRd[0])),
                     static_cast<unsigned long long>(static_cast<uint64_t>(issueRd[1])),
                     static_cast<unsigned long long>(static_cast<uint64_t>(commitRd[0])),
                     static_cast<unsigned long long>(static_cast<uint64_t>(commitRd[1])),
                     static_cast<unsigned long long>(static_cast<uint64_t>(probeInputRd[0])),
                     static_cast<unsigned long long>(static_cast<uint64_t>(probeInputRd[1])),
                     static_cast<unsigned long long>(static_cast<uint64_t>(probeInstr.commit_instr_rd[0])),
                     static_cast<unsigned long long>(static_cast<uint64_t>(probeInstr.commit_instr_rd[1])),
                     static_cast<unsigned long long>(static_cast<uint64_t>(coreInstr.commit_instr_rd[0])),
                     static_cast<unsigned long long>(static_cast<uint64_t>(coreInstr.commit_instr_rd[1])),
                     static_cast<unsigned long long>(static_cast<uint64_t>(rvfiInputInstr.commit_instr_rd[0])),
                     static_cast<unsigned long long>(static_cast<uint64_t>(rvfiInputInstr.commit_instr_rd[1])),
                     static_cast<unsigned long long>(static_cast<uint64_t>(source.rd_addr)));
    }
    return {
        .valid = static_cast<bool>(source.valid),
        .order = static_cast<uint64_t>(source.order),
        .pc = static_cast<uint32_t>(static_cast<uint64_t>(source.pc_rdata)),
        .nextPc = static_cast<uint32_t>(static_cast<uint64_t>(source.pc_wdata)),
        .insn = static_cast<uint32_t>(static_cast<uint64_t>(source.insn)),
        .cause = static_cast<uint32_t>(static_cast<uint64_t>(source.cause)),
        .data = static_cast<uint32_t>(static_cast<uint64_t>(source.rd_wdata)),
        .trap = static_cast<uint8_t>(static_cast<uint64_t>(source.trap)),
        .rd = static_cast<uint8_t>(static_cast<uint64_t>(source.rd_addr)),
    };
}

uint32_t cpphdl_model_exit()
{
    return static_cast<uint32_t>(static_cast<uint64_t>(cpphdlModel->dut->exit_o_out()));
}

void cpphdl_model_trace_rvfi_rd(uint64_t cycle)
{
    auto& core = cpphdlModel->dut->i_ariane.i_cva6;
    auto& scoreboard = core.issue_stage_i.i_scoreboard;
    const auto idEntries = core.id_stage_i.issue_entry_o_out();
    const auto issueInput = core.issue_stage_i.decoded_instr_i_in();
    const auto scoreboardInput = scoreboard.decoded_instr_i_in();
    const auto scoreboardIssue = scoreboard.issue_instr_o_out();
    bool relevant = false;
    for (std::size_t lane = 0; lane < std::remove_cvref_t<decltype(issueInput)>::COUNT_VALUE; ++lane) {
        relevant = relevant || static_cast<uint32_t>(static_cast<uint64_t>(idEntries[lane].pc)) == 0x8000318eU ||
                   static_cast<uint32_t>(static_cast<uint64_t>(issueInput[lane].pc)) == 0x8000318eU ||
                   static_cast<uint32_t>(static_cast<uint64_t>(scoreboardInput[lane].pc)) == 0x8000318eU ||
                   static_cast<uint32_t>(static_cast<uint64_t>(scoreboardIssue[lane].pc)) == 0x8000318eU;
    }
    for (std::size_t index = 0; index < std::remove_cvref_t<decltype(scoreboard.mem_q)>::COUNT_VALUE; ++index) {
        if (static_cast<uint32_t>(static_cast<uint64_t>(scoreboard.mem_q[index].sbe.pc)) == 0x8000318eU) {
            relevant = true;
        }
    }
    if (!relevant) {
        return;
    }
    std::fprintf(stderr,
                 "CPPRVFIRDPIPE cycle=%llu id=%08llx/%llu,%08llx/%llu "
                 "issue_in=%08llx/%llu,%08llx/%llu sb_in=%08llx/%llu,%08llx/%llu "
                 "sb_issue=%08llx/%llu,%08llx/%llu",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned long long>(static_cast<uint64_t>(idEntries[0].pc)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(idEntries[0].rd)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(idEntries[1].pc)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(idEntries[1].rd)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(issueInput[0].pc)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(issueInput[0].rd)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(issueInput[1].pc)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(issueInput[1].rd)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(scoreboardInput[0].pc)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(scoreboardInput[0].rd)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(scoreboardInput[1].pc)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(scoreboardInput[1].rd)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(scoreboardIssue[0].pc)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(scoreboardIssue[0].rd)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(scoreboardIssue[1].pc)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(scoreboardIssue[1].rd)));
    for (std::size_t index = 0; index < std::remove_cvref_t<decltype(scoreboard.mem_q)>::COUNT_VALUE; ++index) {
        const auto& entry = scoreboard.mem_q[index];
        if (static_cast<uint32_t>(static_cast<uint64_t>(entry.sbe.pc)) == 0x8000318eU) {
            std::fprintf(stderr, " mem[%zu]=%llu/%llu", index,
                         static_cast<unsigned long long>(static_cast<uint64_t>(entry.sbe.rd)),
                         static_cast<unsigned long long>(static_cast<uint64_t>(entry.issued)));
        }
    }
    std::fputc('\n', stderr);
}

void cpphdl_model_trace_hartinfo()
{
    const auto topInput = cpphdlModel->dut->i_dm_top.hartinfo_i_in();
    const auto csrInput = cpphdlModel->dut->i_dm_top.i_dm_csrs.hartinfo_i_in();
    const auto topHart = topInput[0];
    const auto csrHart = csrInput[0];
    std::fprintf(stderr,
                 "CPPHARTINFO constant=0x%08llx nscratch=%llu top=0x%08llx nscratch=%llu "
                 "csr=0x%08llx nscratch=%llu\n",
                 static_cast<unsigned long long>(static_cast<uint64_t>(ariane_pkg::DebugHartInfo.pack())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(ariane_pkg::DebugHartInfo.nscratch)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(topHart.pack())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(topHart.nscratch)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(csrHart.pack())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(csrHart.nscratch)));
}

void cpphdl_model_trace_irq(uint64_t cycle)
{
    auto& top = *cpphdlModel->dut;
    auto& clint = top.i_clint;
    auto& core = top.i_ariane.i_cva6;
    auto& cacheSubsystem = core.i_cache_subsystem;
    auto& cacheAxi = cacheSubsystem.i_axi_arbiter;
    auto& memToAxiWrite = cacheAxi.i_hpdcache_mem_to_axi_write;
    auto& csr = core.csr_regfile_i;
    auto& id = core.id_stage_i;
    auto& decoder = id.decoder_i[0];
    auto& scoreboard = core.issue_stage_i.i_scoreboard;
    auto& load = core.ex_stage_i.lsu_i.i_load_unit;
    auto& cacheWrapper = core.i_cache_subsystem.i_dcache;
    auto& icache = core.i_cache_subsystem.i_cva6_icache;
    auto& hpd = cacheWrapper.i_hpdcache;
    auto& ctrl = hpd.hpdcache_ctrl_i;
    auto& ctrlPe = ctrl.hpdcache_ctrl_pe_i;
    auto& memctrl = ctrl.hpdcache_memctrl_i;
    auto& dirWay1 = memctrl.dir_sram[1];
    auto& dataWord1 = memctrl.data_sram[0][1];
    auto& miss = hpd.hpdcache_miss_handler_i;
    auto& coreArb = hpd.core_req_arbiter_i;
    auto& fixedArb = coreArb.req_arbiter_i;
    auto& uncached = hpd.hpdcache_uc_i;
    auto& readArb = hpd.hpdcache_mem_req_read_arbiter_i;
    auto& writeArb = hpd.hpdcache_mem_req_write_arbiter_i;
    auto& store = core.ex_stage_i.lsu_i.i_store_unit;
    auto& storeAdapter = cacheWrapper.i_cva6_hpdcache_store_if_adapter;
    auto& dmMem = top.i_dm_top.i_dm_mem;
    auto& debugBus = top.master[static_cast<unsigned>(ariane_soc::Debug)];
    auto& cpuBus = top.slave[0];
    const auto decoded = decoder.instruction_o_out();
    const auto idEntry = id.issue_entry_o_out()[0];
    const auto issueEntry = core.issue_stage_i.decoded_instr_i_in()[0];
    const auto commitEntry = core.commit_stage_i.commit_instr_i_in()[0];
    const auto commitException = core.commit_stage_i.exception_o_out();
    const auto commitPointer = static_cast<unsigned>(static_cast<uint64_t>(scoreboard.commit_pointer_q[0]));
    const auto& scoreboardEntry = scoreboard.mem_q[commitPointer];
    std::fprintf(stderr,
                 "CPPIRQ cycle=%llu rtc=%u sync=0x%llx serial=%u edge=%u "
                 "mtime=0x%llx mtimecmp=0x%llx timer=%u core_timer=%u "
                 "mip=0x%llx mie=0x%llx global_mie=%u priv=%llu npc=0x%llx "
                 "dmi_delay=%u halt_csrs=%u halt_mem=%u dm_req=%u core_req=%u "
                 "dec_req=%u dec=%u/%llu/%llu id=%u/%u/%llu/%llu id_outer=%u "
                 "issue=%u/%u/%llu/%llu issue_outer=%u "
                 "sb_ptr=%u/%llu sb=%u/%u/%u/%llu/%llu "
                 "commit=%u/%u/%llu projected=%u/%u/%llu drop=%u halt=%u "
                 "commit_out=%u/%llu csr_ex=%u/%llu debug_mode=%u set_debug_pc=%u "
                 "dmmem=%llu/%u/%u/0x%llx/0x%llx cmd=%u halted=0x%llx busy=%u "
                 "dmrd=%u/0x%llx/0x%llx/0x%llx "
                 "ld=%llu/%u/%llu/0x%llx ldbuf=0x%llx/%u req=%u/%u/0x%llx "
                 "rsp=%u/%u/%llu/0x%llx result=0x%llx adapter=0x%llx hpd_data=0x%llx "
                 "hpd=%u/%u arb=%u/%u/0x%llx/0x%llx fx=0x%llx/%u st1=%u "
                 "hit=%u/%u pe=%u/%u refill=%llu/%u/%u/%u/%llu/0x%llx/0x%llx "
                 "dir1=%u/%u/%llu/0x%llx/0x%llx/0x%llx/0x%llx "
                 "data1=%u/%u/%llu/0x%llx/0x%llx\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned>(static_cast<bool>(cpphdlModel->rtc)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(clint.i_sync_edge.i_sync.reg_q)),
                 static_cast<unsigned>(static_cast<bool>(clint.i_sync_edge.serial_q)),
                 static_cast<unsigned>(static_cast<bool>(clint.i_sync_edge.r_edge_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(clint.mtime_q)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(clint.mtimecmp_q[0])),
                 static_cast<unsigned>(static_cast<bool>(clint.timer_irq_o_out()[0])),
                 static_cast<unsigned>(static_cast<bool>(core.time_irq_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(csr.mip_q)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(csr.mie_q)),
                 static_cast<unsigned>(static_cast<bool>(csr.mstatus_q.mie)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(csr.priv_lvl_q)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(core.i_frontend.npc_q)),
                 static_cast<unsigned>(static_cast<uint32_t>(top.dmi_del_cnt_q)),
                 static_cast<unsigned>(static_cast<uint64_t>(top.i_dm_top.i_dm_csrs.haltreq_o_out())),
                 static_cast<unsigned>(static_cast<uint64_t>(top.i_dm_top.i_dm_mem.haltreq_i_in())),
                 static_cast<unsigned>(static_cast<uint64_t>(top.i_dm_top.debug_req_o_out())),
                 static_cast<unsigned>(static_cast<bool>(core.debug_req_i_in())),
                 static_cast<unsigned>(static_cast<bool>(decoder.debug_req_i_in())),
                 static_cast<unsigned>(static_cast<bool>(decoded.ex.valid)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(decoded.ex.cause)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(decoded.fu)),
                 static_cast<unsigned>(static_cast<bool>(idEntry.valid)),
                 static_cast<unsigned>(static_cast<bool>(idEntry.ex.valid)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(idEntry.ex.cause)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(idEntry.fu)),
                 static_cast<unsigned>(static_cast<bool>(id.issue_entry_valid_o_out()[0])),
                 static_cast<unsigned>(static_cast<bool>(issueEntry.valid)),
                 static_cast<unsigned>(static_cast<bool>(issueEntry.ex.valid)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(issueEntry.ex.cause)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(issueEntry.fu)),
                 static_cast<unsigned>(static_cast<bool>(core.issue_stage_i.decoded_instr_valid_i_in()[0])),
                 commitPointer,
                 static_cast<unsigned long long>(static_cast<uint64_t>(scoreboard.issue_pointer_q)),
                 static_cast<unsigned>(static_cast<bool>(scoreboardEntry.issued)),
                 static_cast<unsigned>(static_cast<bool>(scoreboardEntry.sbe.valid)),
                 static_cast<unsigned>(static_cast<bool>(scoreboardEntry.sbe.ex.valid)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(scoreboardEntry.sbe.ex.cause)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(scoreboardEntry.sbe.fu)),
                 static_cast<unsigned>(static_cast<bool>(commitEntry.valid)),
                 static_cast<unsigned>(static_cast<bool>(commitEntry.ex.valid)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(commitEntry.ex.cause)),
                 static_cast<unsigned>(static_cast<bool>(core.commit_stage_i.commit_instr_i_in__field_valid()[0])),
                 static_cast<unsigned>(static_cast<bool>(core.commit_stage_i.commit_instr_i_in__field_ex_valid()[0])),
                 static_cast<unsigned long long>(static_cast<uint64_t>(core.commit_stage_i.commit_instr_i_in__field_ex_cause()[0])),
                 static_cast<unsigned>(static_cast<bool>(core.commit_stage_i.commit_drop_i_in()[0])),
                 static_cast<unsigned>(static_cast<bool>(core.commit_stage_i.halt_i_in())),
                 static_cast<unsigned>(static_cast<bool>(commitException.valid)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(commitException.cause)),
                 static_cast<unsigned>(static_cast<bool>(csr.ex_i_in__field_valid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(csr.ex_i_in__field_cause())),
                 static_cast<unsigned>(static_cast<bool>(csr.debug_mode_o_out())),
                 static_cast<unsigned>(static_cast<bool>(csr.set_debug_pc_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dmMem.state_q)),
                 static_cast<unsigned>(static_cast<bool>(dmMem.req_i_in())),
                 static_cast<unsigned>(static_cast<bool>(dmMem.we_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dmMem.addr_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dmMem.wdata_i_in())),
                 static_cast<unsigned>(static_cast<bool>(dmMem.cmd_valid_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dmMem.halted_q)),
                 static_cast<unsigned>(static_cast<bool>(dmMem.cmdbusy_o_out())),
                 static_cast<unsigned>(static_cast<bool>(dmMem.fwd_rom_q)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dmMem.rdata_q)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dmMem.i_debug_rom.rdata_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dmMem.rdata_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(load.state_q)),
                 static_cast<unsigned>(static_cast<bool>(load.valid_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(load.lsu_ctrl_i_in__field_trans_id())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(load.lsu_ctrl_i_in__field_vaddr())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(load.ldbuf_valid_q)),
                 static_cast<unsigned>(static_cast<bool>(load.ldbuf_w)),
                 static_cast<unsigned>(static_cast<bool>(load.req_port_o_out__field_data_req())),
                 static_cast<unsigned>(static_cast<uint64_t>(load.req_port_o_out__field_data_id())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(load.vaddr_o_out())),
                 static_cast<unsigned>(static_cast<bool>(load.req_port_i_in__field_data_gnt())),
                 static_cast<unsigned>(static_cast<bool>(load.req_port_i_in__field_data_rvalid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(load.req_port_i_in__field_data_rid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpphdl::pack_value<64>(load.req_port_i_in__field_data_rdata()))),
                 static_cast<unsigned long long>(static_cast<uint64_t>(load.result_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpphdl::pack_value<64>(cacheWrapper.i_cva6_hpdcache_load_if_adapter[1].cva6_req_o_out__field_data_rdata()))),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpphdl::pack_value<64>(hpd.core_rsp_o_out__field_rdata()[1]))),
                 static_cast<unsigned>(static_cast<bool>(cacheWrapper.i_cva6_hpdcache_load_if_adapter[1].hpdcache_req_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(cacheWrapper.i_cva6_hpdcache_load_if_adapter[1].hpdcache_req_ready_i_in())),
                 static_cast<unsigned>(static_cast<bool>(coreArb.arb_req_ready_i_in())),
                 static_cast<unsigned>(static_cast<bool>(coreArb.arb_req_valid_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(coreArb.arb_req_gnt_q)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(fixedArb.gnt_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(fixedArb.gnt_q)),
                 static_cast<unsigned>(static_cast<bool>(fixedArb.wait_q)),
                 static_cast<unsigned>(static_cast<bool>(ctrl.st1_req_valid_q)),
                 static_cast<unsigned>(static_cast<bool>(ctrl.cachedir_hit_o_out())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.cachedir_hit_i_in())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.core_req_ready_o_out())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.st1_req_valid_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(miss.refill_fsm_q)),
                 static_cast<unsigned>(static_cast<bool>(miss.refill_req_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(miss.refill_req_ready_i_in())),
                 static_cast<unsigned>(static_cast<bool>(miss.refill_write_dir_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(miss.refill_set_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(miss.refill_way_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(miss.refill_dir_entry_o_out().pack())),
                 static_cast<unsigned>(static_cast<bool>(dirWay1.cs_in())),
                 static_cast<unsigned>(static_cast<bool>(dirWay1.we_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dirWay1.addr_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpphdl::pack_value<64>(dirWay1.wdata_in()))),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpphdl::pack_value<64>(dirWay1.rdata_out()))),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpphdl::pack_value<64>(dirWay1.i_sram.mem[0]))),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpphdl::pack_value<64>(dirWay1.i_sram.mem._next[0]))),
                 static_cast<unsigned>(static_cast<bool>(dataWord1.cs_in())),
                 static_cast<unsigned>(static_cast<bool>(dataWord1.we_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dataWord1.addr_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpphdl::pack_value<64>(dataWord1.wdata_in()))),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpphdl::pack_value<64>(dataWord1.rdata_out()))));
    // Trace the debug target at each structural boundary, not by forcing evaluation.
    // Every value below is read through its ordinary port/comb getter at this clock.
    // A first mismatch identifies the translated module that lost the transaction.
    std::fprintf(stderr,
                 "CPPDM cycle=%llu bus_ar=%u/0x%llx bus_aw=%u/0x%llx bus_w=%u/0x%llx "
                 "a2m=%u/%u/0x%llx/0x%llx dm=%u/%u/0x%llx rom=%u/0x%llx/%llu/%llu/0x%llx "
                 "ic=0x%llx/0x%llx\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned>(static_cast<bool>(debugBus.ar_valid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(debugBus.ar_addr())),
                 static_cast<unsigned>(static_cast<bool>(debugBus.aw_valid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(debugBus.aw_addr())),
                 static_cast<unsigned>(static_cast<bool>(debugBus.w_valid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(debugBus.w_data())),
                 static_cast<unsigned>(static_cast<bool>(top.i_dm_axi2mem.req_o_out())),
                 static_cast<unsigned>(static_cast<bool>(top.i_dm_axi2mem.we_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_dm_axi2mem.ax_req_q.addr)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_dm_axi2mem.addr_o_out())),
                 static_cast<unsigned>(static_cast<bool>(dmMem.req_i_in())),
                 static_cast<unsigned>(static_cast<bool>(dmMem.we_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dmMem.addr_i_in())),
                 static_cast<unsigned>(static_cast<bool>(dmMem.i_debug_rom.req_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dmMem.i_debug_rom.addr_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dmMem.i_debug_rom.addr_q)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dmMem.i_debug_rom.addr_q._next)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(dmMem.i_debug_rom.rdata_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(core.i_frontend.icache_data_q)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(core.i_frontend.icache_vaddr_q)));
    // Report the individual ready factors without forcing any combinational evaluation.
    // These are ordinary lazy port reads in the same clock epoch as the surrounding trace.
    // A set factor identifies the actual controller backpressure source.
    std::fprintf(stderr,
                 "CPPHPE cycle=%llu core=%u/%u init=%u competing=%u/%u/%u "
                 "block=%u/%u/%u/%u/%u st1=%u/%u/%u\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.core_req_valid_i_in())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.core_req_ready_o_out())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.cachedir_init_ready_i_in())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.scrub_req_valid_i_in())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.rtab_req_valid_i_in())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.refill_req_valid_i_in())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.rtab_full_i_in())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.cmo_busy_i_in())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.uc_busy_i_in())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.err_busy_i_in())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.rtab_fence_i_in())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.st1_req_valid_i_in())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.st1_req_is_uncacheable_i_in())),
                 static_cast<unsigned>(static_cast<bool>(ctrlPe.st1_req_valid_o_out())));
    // Trace the uncached read at each ordinary valid/ready boundary.
    // All accesses are lazy port reads in the current clock epoch.
    // The first unequal adjacent pair identifies a generated binding failure.
    std::fprintf(stderr,
                 "CPPUC cycle=%llu state=%llu req=%u/%u/%u/%u/0x%llx "
                 "uc=%u/%u/0x%llx arb_in=0x%llx/0x%llx arb=%u/%u/0x%llx "
                 "hpd=%u/%u/0x%llx wrap=%u/%u/0x%llx rsp=%u/%u\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned long long>(static_cast<uint64_t>(uncached.uc_fsm_q)),
                 static_cast<unsigned>(static_cast<bool>(uncached.req_valid_i_in())),
                 static_cast<unsigned>(static_cast<bool>(uncached.req_ready_o_out())),
                 static_cast<unsigned>(static_cast<bool>(uncached.req_op_i_in__field_is_ld())),
                 static_cast<unsigned>(static_cast<bool>(uncached.req_op_i_in__field_is_st())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(uncached.req_addr_i_in())),
                 static_cast<unsigned>(static_cast<bool>(uncached.mem_req_read_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(uncached.mem_req_read_ready_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(uncached.mem_req_read_o_out__field_mem_req_addr())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(readArb.mem_req_read_valid_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(readArb.mem_req_read_ready_o_out())),
                 static_cast<unsigned>(static_cast<bool>(readArb.mem_req_read_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(readArb.mem_req_read_ready_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(readArb.mem_req_read_o_out__field_mem_req_addr())),
                 static_cast<unsigned>(static_cast<bool>(hpd.mem_req_read_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(hpd.mem_req_read_ready_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(hpd.mem_req_read_o_out().mem_req_addr)),
                 static_cast<unsigned>(static_cast<bool>(cacheWrapper.dcache_mem_req_read_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(cacheWrapper.dcache_mem_req_read_ready_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cacheWrapper.dcache_mem_req_read_o_out().mem_req_addr)),
                 static_cast<unsigned>(static_cast<bool>(uncached.mem_resp_read_valid_i_in())),
                 static_cast<unsigned>(static_cast<bool>(uncached.core_rsp_valid_o_out())));
    std::fprintf(stderr,
                 "CPPUCW cycle=%llu uc_a=%u/%u uc_d=%u/%u "
                 "arb_in=0x%llx/0x%llx arb_rdy=0x%llx/0x%llx "
                 "arb_a=%u/%u/0x%llx arb_d=%u/%u "
                 "hpd=%u/%u/%u/%u wrap=%u/%u/%u/%u rsp=%u\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned>(static_cast<bool>(uncached.mem_req_write_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(uncached.mem_req_write_ready_i_in())),
                 static_cast<unsigned>(static_cast<bool>(uncached.mem_req_write_data_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(uncached.mem_req_write_data_ready_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(writeArb.mem_req_write_valid_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(writeArb.mem_req_write_data_valid_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(writeArb.mem_req_write_ready_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(writeArb.mem_req_write_data_ready_o_out())),
                 static_cast<unsigned>(static_cast<bool>(writeArb.mem_req_write_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(writeArb.mem_req_write_ready_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(writeArb.mem_req_write_o_out__field_mem_req_addr())),
                 static_cast<unsigned>(static_cast<bool>(writeArb.mem_req_write_data_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(writeArb.mem_req_write_data_ready_i_in())),
                 static_cast<unsigned>(static_cast<bool>(hpd.mem_req_write_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(hpd.mem_req_write_ready_i_in())),
                 static_cast<unsigned>(static_cast<bool>(hpd.mem_req_write_data_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(hpd.mem_req_write_data_ready_i_in())),
                 static_cast<unsigned>(static_cast<bool>(cacheWrapper.dcache_mem_req_write_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(cacheWrapper.dcache_mem_req_write_ready_i_in())),
                 static_cast<unsigned>(static_cast<bool>(cacheWrapper.dcache_mem_req_write_data_valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(cacheWrapper.dcache_mem_req_write_data_ready_i_in())),
                 static_cast<unsigned>(static_cast<bool>(uncached.mem_resp_write_valid_i_in())));
    std::fprintf(stderr,
                 "CPPAXIW cycle=%llu in=%u/%u/%u/%u conv=%u/%u/%u/%u "
                 "axi=%u/%u/%u/%u bus=%u/%u/%u/%u aw=0x%llx b=%u/%u\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.dcache_write_valid_i_in())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.dcache_write_ready_o_out())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.dcache_write_data_valid_i_in())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.dcache_write_data_ready_o_out())),
                 static_cast<unsigned>(static_cast<bool>(memToAxiWrite.req_valid_i_in())),
                 static_cast<unsigned>(static_cast<bool>(memToAxiWrite.req_ready_o_out())),
                 static_cast<unsigned>(static_cast<bool>(memToAxiWrite.req_data_valid_i_in())),
                 static_cast<unsigned>(static_cast<bool>(memToAxiWrite.req_data_ready_o_out())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.axi_req_o_out__field_aw_valid())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.axi_resp_i_in__field_aw_ready())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.axi_req_o_out__field_w_valid())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.axi_resp_i_in__field_w_ready())),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.aw_valid())),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.aw_ready())),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.w_valid())),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.w_ready())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpuBus.aw_addr())),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.b_valid())),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.b_ready())));
    const auto adapterRequest = storeAdapter.hpdcache_req_o_out();
    const auto arbiterRequest = coreArb.arb_req_o_out();
    auto& storeBuffer = store.store_buffer_i;
    auto& lsu = core.ex_stage_i.lsu_i;
    const auto speculativeRead =
        static_cast<unsigned>(static_cast<uint64_t>(storeBuffer.speculative_read_pointer_q));
    const auto speculativeWrite =
        static_cast<unsigned>(static_cast<uint64_t>(storeBuffer.speculative_write_pointer_q));
    const auto commitRead =
        static_cast<unsigned>(static_cast<uint64_t>(storeBuffer.commit_read_pointer_q));
    std::fprintf(stderr,
                 "CPPADDR cycle=%llu store=%u/0x%llx/0x%llx/0x%llx "
                 "agu=0x%llx+0x%llx=>0x%llx "
                 "paddr=0x%llx/0x%llx queue=%u/%u/0x%llx/%u/0x%llx "
                 "adapter=%u/0x%llx/0x%llx=>0x%llx/0x%llx tag=0x%llx "
                 "tagpath=0x%llx/0x%llx/0x%llx/0x%llx "
                 "arb=%u/0x%llx/0x%llx ctrl=0x%llx/0x%llx uc=0x%llx\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned>(static_cast<bool>(store.req_port_o_out__field_data_req())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(store.vaddr_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(store.req_port_o_out__field_address_tag())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(store.req_port_o_out__field_address_index())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(lsu.fu_data_i_in__field_imm())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(lsu.fu_data_i_in__field_operand_a())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(lsu.vaddr_xlen_comb_func())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(store.paddr_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(storeBuffer.paddr_i_in())),
                 speculativeRead,
                 speculativeWrite,
                 static_cast<unsigned long long>(static_cast<uint64_t>(
                     storeBuffer.speculative_queue_q[speculativeRead].address)),
                 commitRead,
                 static_cast<unsigned long long>(static_cast<uint64_t>(
                     storeBuffer.commit_queue_q[commitRead].address)),
                 static_cast<unsigned>(static_cast<bool>(storeAdapter.hpdcache_req_valid_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(storeAdapter.cva6_req_i_in__field_address_tag())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(storeAdapter.cva6_req_i_in__field_address_index())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(adapterRequest.addr_tag)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(adapterRequest.addr_offset)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(storeAdapter.hpdcache_req_tag_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cacheWrapper.dcache_req_tag_comb_func()[3])),
                 static_cast<unsigned long long>(static_cast<uint64_t>(hpd.core_req_tag_i_in()[3])),
                 static_cast<unsigned long long>(static_cast<uint64_t>(coreArb.core_req_tag_i_in()[3])),
                 static_cast<unsigned long long>(static_cast<uint64_t>(coreArb.core_req_tag_mux_i.data_i_in()[3])),
                 static_cast<unsigned>(static_cast<bool>(coreArb.arb_req_valid_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(coreArb.arb_tag_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(arbiterRequest.addr_offset)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(ctrl.core_req_tag_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(ctrl.core_req_i_in__field_addr_offset())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(ctrl.uc_req_addr_o_out())));
    std::fprintf(stderr,
                 "CPPIRESP cycle=%llu return=%u/0x%llx cache=%u/0x%llx/0x%llx "
                 "frontend=%u/0x%llx/0x%llx stored=0x%llx/0x%llx\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned>(static_cast<bool>(icache.mem_rtrn_vld_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(icache.mem_rtrn_i_in__field_data())),
                 static_cast<unsigned>(static_cast<bool>(icache.dreq_o_out__field_valid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(icache.dreq_o_out__field_data())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(icache.dreq_o_out__field_vaddr())),
                 static_cast<unsigned>(static_cast<bool>(core.i_frontend.icache_dreq_i_in__field_valid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(core.i_frontend.icache_dreq_i_in__field_data())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(core.i_frontend.icache_dreq_i_in__field_vaddr())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(core.i_frontend.icache_data_q)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(core.i_frontend.icache_vaddr_q)));
    std::fprintf(stderr,
                 "CPPAXIR cycle=%llu bus_ar=%u/%u/0x%llx bus_r=%u/%u/%u/0x%llx "
                 "cache_ar=%u/%u cache_r=%u/%u/%u/0x%llx miss=%u/0x%llx icache=%u/0x%llx\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.ar_valid())),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.ar_ready())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpuBus.ar_addr())),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.r_valid())),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.r_ready())),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.r_last())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpuBus.r_data())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.axi_req_o_out__field_ar_valid())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.axi_resp_i_in__field_ar_ready())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.axi_resp_i_in__field_r_valid())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.axi_req_o_out__field_r_ready())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.axi_resp_i_in__field_r_last())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cacheAxi.axi_resp_i_in__field_r_data())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.icache_miss_resp_valid_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cacheAxi.icache_miss_resp_o_out__field_data())),
                 static_cast<unsigned>(static_cast<bool>(icache.mem_rtrn_vld_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(icache.mem_rtrn_i_in__field_data())));
    // Follow the instruction request through existing getters at the same clock epoch.
    // This does not evaluate or settle anything explicitly; each call is a normal read.
    // Adjacent values should agree unless the corresponding generated binding is wrong.
    auto& pmpData = core.ex_stage_i.lsu_i.i_pmp_data_if;
    std::fprintf(stderr,
                 "CPPIF cycle=%llu ic=%u/0x%llx/%llu/%u/%u "
                 "fsm=%u/%u/%llu/%u/%u/%u/%u/0x%llx/%u/%u "
                 "pmp=%u/%u/%u/%llu/0x%llx "
                 "arb=%u/0x%llx/%u/0x%llx "
                 "core=%u/0x%llx wrap=%u/0x%llx bus=%u/0x%llx\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned>(static_cast<bool>(icache.mem_data_req_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(icache.mem_data_o_out__field_paddr())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(icache.state_q)),
                 static_cast<unsigned>(static_cast<bool>(icache.cache_en_q)),
                 static_cast<unsigned>(static_cast<bool>(icache.cmp_en_q)),
                 static_cast<unsigned>(static_cast<bool>(icache.areq_i_in__field_fetch_valid())),
                 static_cast<unsigned>(static_cast<bool>(icache.areq_i_in__field_fetch_exception_valid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(icache.areq_i_in__field_fetch_exception().cause)),
                 static_cast<unsigned>(static_cast<bool>(icache.dreq_i_in__field_spec())),
                 static_cast<unsigned>(static_cast<bool>(icache.dreq_i_in__field_kill_s2())),
                 static_cast<unsigned>(static_cast<bool>(icache.flush_q)),
                 static_cast<unsigned>(static_cast<bool>(icache.inv_q)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(icache.cl_hit_comb_func())),
                 static_cast<unsigned>(static_cast<bool>(icache.addr_ni_comb_func())),
                 static_cast<unsigned>(static_cast<bool>(icache.paddr_is_nc_comb_func())),
                 static_cast<unsigned>(static_cast<bool>(pmpData.match_any_execute_region_comb_func())),
                 static_cast<unsigned>(static_cast<bool>(pmpData.pmp_if_allow_comb_func())),
                 static_cast<unsigned>(static_cast<bool>(pmpData.icache_areq_i_in__field_fetch_exception().valid)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(pmpData.priv_lvl_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(pmpData.icache_areq_i_in__field_fetch_paddr())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.icache_miss_valid_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cacheAxi.icache_miss_i_in__field_paddr())),
                 static_cast<unsigned>(static_cast<bool>(cacheAxi.axi_req_o_out__field_ar_valid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cacheAxi.axi_req_o_out__field_ar().addr)),
                 static_cast<unsigned>(static_cast<bool>(core.noc_req_o_out__field_ar_valid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(core.noc_req_o_out__field_ar_addr())),
                 static_cast<unsigned>(static_cast<bool>(top.i_ariane.noc_req_o_out__field_ar_valid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_ariane.noc_req_o_out__field_ar_addr())),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.ar_valid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpuBus.ar_addr())));
    // Follow the boot-ROM transaction with ordinary getters at one clock epoch.
    // This observer never drives or explicitly evaluates the generated model.
    // It identifies the first AXI stage that disagrees with the native harness.
    auto& xbar = top.i_axi_xbar.i_xbar;
    auto& demux = xbar.i_axi_demux[0];
    auto& romMux = xbar.i_axi_mux[static_cast<unsigned>(ariane_soc::ROM)];
    auto& debugMux = xbar.i_axi_mux[static_cast<unsigned>(ariane_soc::Debug)];
    auto& romBus = top.master[static_cast<unsigned>(ariane_soc::ROM)];
    auto& romAxi = top.i_axi2rom.slave();
    const auto demuxRequests = demux.mst_reqs_o_out();
    const auto muxResponses = romMux.slv_resps_o_out();
    const auto muxWholeInputs = romMux.slv_reqs_i_in();
    const auto muxProjectedInputs = romMux.slv_reqs_i_in__field_ar_valid();
    const auto muxArbRequest = romMux.i_ar_arbiter.req_i_in();
    const auto muxWholeOutput = romMux.mst_req_o_out();
    const auto xbarWholeOutputs = xbar.mst_ports_req_o_out();
    const auto demuxWholeResponses = demux.mst_resps_i_in();
    const auto xbarWholeResponses = xbar.mst_ports_resp_i_in();
    std::fprintf(stderr,
                 "CPPREADY cycle=%llu adapter=%llu/%llu/%llu target=%u/%u/%u/%u/%llu/%u/%u/%u "
                 "xbar_in=%u/%u mux_in=%u mux_out=%u/%u "
                 "demux_in=%u/%u demux_out=%u xbar_out=%u bus=%u\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_dm_axi2mem.state_q)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_dm_axi2mem.cnt_q)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_dm_axi2mem.ax_req_q.len)),
                 static_cast<unsigned>(static_cast<bool>(debugBus.ar_valid())),
                 static_cast<unsigned>(static_cast<bool>(debugBus.ar_ready())),
                 static_cast<unsigned>(static_cast<bool>(top.i_dm_axi2mem.slave().ar_ready())),
                 static_cast<unsigned>(std::addressof(debugBus) == std::addressof(top.i_dm_axi2mem.slave())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(debugBus.ar_len())),
                 static_cast<unsigned>(static_cast<bool>(debugBus.r_valid())),
                 static_cast<unsigned>(static_cast<bool>(debugBus.r_ready())),
                 static_cast<unsigned>(static_cast<bool>(debugBus.r_last())),
                 static_cast<unsigned>(static_cast<bool>(xbarWholeResponses[static_cast<unsigned>(ariane_soc::Debug)].ar_ready)),
                 static_cast<unsigned>(static_cast<bool>(xbar.mst_ports_resp_i_in__field_ar_ready()[static_cast<unsigned>(ariane_soc::Debug)])),
                 static_cast<unsigned>(static_cast<bool>(debugMux.mst_resp_i_in__field_ar_ready())),
                 static_cast<unsigned>(static_cast<bool>(debugMux.slv_resps_o_out__field_ar_ready()[0])),
                 static_cast<unsigned>(static_cast<bool>(debugMux.slv_resps_o_out__field_ar_ready()[1])),
                 static_cast<unsigned>(static_cast<bool>(demuxWholeResponses[static_cast<unsigned>(ariane_soc::Debug)].ar_ready)),
                 static_cast<unsigned>(static_cast<bool>(demux.mst_resps_i_in__field_ar_ready()[static_cast<unsigned>(ariane_soc::Debug)])),
                 static_cast<unsigned>(static_cast<bool>(demux.slv_resp_o_out__field_ar_ready())),
                 static_cast<unsigned>(static_cast<bool>(xbar.slv_ports_resp_o_out__field_ar_ready()[0])),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.ar_ready())));
    std::fprintf(stderr,
                 "CPPROM cycle=%llu cpu=%u/%u/0x%llx "
                 "demux=%u/0x%llx rom=%u/%u/0x%llx/%u/%u/0x%llx "
                 "axi=%u/%u/0x%llx/%u/%u/0x%llx boot=%u/0x%llx/0x%llx "
                 "return=%u/0x%llx/%u/0x%llx\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.ar_valid())),
                 static_cast<unsigned>(static_cast<bool>(cpuBus.ar_ready())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(cpuBus.ar_addr())),
                 static_cast<unsigned>(static_cast<bool>(demuxRequests[static_cast<unsigned>(ariane_soc::ROM)].ar_valid)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(demuxRequests[static_cast<unsigned>(ariane_soc::ROM)].ar.addr)),
                 static_cast<unsigned>(static_cast<bool>(romBus.ar_valid())),
                 static_cast<unsigned>(static_cast<bool>(romBus.ar_ready())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(romBus.ar_addr())),
                 static_cast<unsigned>(static_cast<bool>(romBus.r_valid())),
                 static_cast<unsigned>(static_cast<bool>(romBus.r_ready())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(romBus.r_data())),
                 static_cast<unsigned>(static_cast<bool>(romAxi.ar_valid())),
                 static_cast<unsigned>(static_cast<bool>(romAxi.ar_ready())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(romAxi.ar_addr())),
                 static_cast<unsigned>(static_cast<bool>(romAxi.r_valid())),
                 static_cast<unsigned>(static_cast<bool>(romAxi.r_ready())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(romAxi.r_data())),
                 static_cast<unsigned>(static_cast<bool>(top.i_bootrom.req_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_bootrom.addr_i_in())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_bootrom.rdata_o_out())),
                 static_cast<unsigned>(static_cast<bool>(muxResponses[0].r_valid)),
                 static_cast<unsigned long long>(static_cast<uint64_t>(muxResponses[0].r.data)),
	                 static_cast<unsigned>(static_cast<bool>(demux.slv_resp_o_out__field_r_valid())),
	                 static_cast<unsigned long long>(static_cast<uint64_t>(demux.slv_resp_o_out__field_r().data)));
	    std::fprintf(stderr,
	                 "CPPROMFSM cycle=%llu self=%p slave=%p state=%llu next=%llu cnt=%llu nextcnt=%llu "
	                 "ar=%u/%u/0x%llx len=%llu saved=0x%llx req=%u/0x%llx "
	                 "r=%u/%u/%u\n",
	                 static_cast<unsigned long long>(cycle),
	                 static_cast<void*>(std::addressof(top.i_axi2rom)),
	                 static_cast<void*>(std::addressof(romAxi)),
	                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_axi2rom.state_q)),
	                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_axi2rom.state_d_comb_func())),
	                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_axi2rom.cnt_q)),
	                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_axi2rom.cnt_d_comb_func())),
	                 static_cast<unsigned>(static_cast<bool>(romAxi.ar_valid())),
	                 static_cast<unsigned>(static_cast<bool>(top.i_axi2rom.slave_ar_ready_comb_func())),
	                 static_cast<unsigned long long>(static_cast<uint64_t>(romAxi.ar_addr())),
	                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_axi2rom.ax_req_q.len)),
	                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_axi2rom.req_addr_q)),
	                 static_cast<unsigned>(static_cast<bool>(top.i_axi2rom.req_o_out())),
	                 static_cast<unsigned long long>(static_cast<uint64_t>(top.i_axi2rom.addr_o_out())),
	                 static_cast<unsigned>(static_cast<bool>(romAxi.r_valid())),
	                 static_cast<unsigned>(static_cast<bool>(romAxi.r_ready())),
	                 static_cast<unsigned>(static_cast<bool>(romAxi.r_last())));
	    const auto romIndex = static_cast<unsigned>(ariane_soc::ROM);
	    auto& xbarCore = top.i_axi_xbar.i_xbar;
	    std::fprintf(stderr,
	                 "CPPROMSTORE cycle=%llu port=%p/%u wrap=%p/%u/%ld core=%p/%u/%ld mux=%p/%u\n",
	                 static_cast<unsigned long long>(cycle),
	                 static_cast<void*>(romAxi.ar_valid.cache),
	                 static_cast<unsigned>(static_cast<bool>(romAxi.ar_valid())),
	                 static_cast<void*>(std::addressof(top.i_axi_xbar.mst_reqs_ar_valid_comb[romIndex])),
	                 static_cast<unsigned>(static_cast<bool>(top.i_axi_xbar.mst_reqs_ar_valid_comb[romIndex])),
	                 top.i_axi_xbar.__prev__system_clock_mst_reqs_ar_valid_comb,
	                 static_cast<void*>(std::addressof(xbarCore.mst_ports_req_o_ar_valid_comb[romIndex])),
	                 static_cast<unsigned>(static_cast<bool>(xbarCore.mst_ports_req_o_ar_valid_comb[romIndex])),
	                 xbarCore.__prev__system_clock_mst_ports_req_o_ar_valid_comb,
	                 static_cast<void*>(std::addressof(romMux.mst_req_o_ar_valid_comb)),
	                 static_cast<unsigned>(static_cast<bool>(romMux.mst_req_o_ar_valid_comb)));
    std::fprintf(stderr,
                 "CPPMUX cycle=%llu input=%u/%u/0x%llx prepend=%u/%u "
                 "arb=0x%llx/%u/0x%llx/%llu spill=%u/%u/%u/%u\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned>(static_cast<bool>(muxWholeInputs[0].ar_valid)),
                 static_cast<unsigned>(static_cast<bool>(muxProjectedInputs[0])),
                 static_cast<unsigned long long>(static_cast<uint64_t>(romMux.slv_reqs_i_in__field_ar()[0].addr)),
                 static_cast<unsigned>(static_cast<bool>(romMux.i_id_prepend[0].slv_ar_valids_i_in())),
                 static_cast<unsigned>(static_cast<bool>(romMux.i_id_prepend[0].mst_ar_valids_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(muxArbRequest)),
                 static_cast<unsigned>(static_cast<bool>(romMux.i_ar_arbiter.req_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(romMux.i_ar_arbiter.gnt_o_out())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(romMux.i_ar_arbiter.idx_o_out())),
                 static_cast<unsigned>(static_cast<bool>(romMux.i_ar_spill_reg.valid_i_in())),
                 static_cast<unsigned>(static_cast<bool>(romMux.i_ar_spill_reg.valid_o_out())),
                 static_cast<unsigned>(static_cast<bool>(romMux.i_ar_spill_reg.ready_i_in())),
                 static_cast<unsigned>(static_cast<bool>(romMux.i_ar_spill_reg.ready_o_out())));
    std::fprintf(stderr,
                 "CPPOUT cycle=%llu mux=%u/%u/0x%llx xbar=%u/%u/0x%llx top=%u/0x%llx\n",
                 static_cast<unsigned long long>(cycle),
                 static_cast<unsigned>(static_cast<bool>(muxWholeOutput.ar_valid)),
                 static_cast<unsigned>(static_cast<bool>(romMux.mst_req_o_out__field_ar_valid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(romMux.mst_req_o_out__field_ar().addr)),
                 static_cast<unsigned>(static_cast<bool>(xbarWholeOutputs[static_cast<unsigned>(ariane_soc::ROM)].ar_valid)),
                 static_cast<unsigned>(static_cast<bool>(xbar.mst_ports_req_o_out__field_ar_valid()[static_cast<unsigned>(ariane_soc::ROM)])),
                 static_cast<unsigned long long>(static_cast<uint64_t>(xbar.mst_ports_req_o_out__field_ar_addr()[static_cast<unsigned>(ariane_soc::ROM)])),
                 static_cast<unsigned>(static_cast<bool>(romBus.ar_valid())),
                 static_cast<unsigned long long>(static_cast<uint64_t>(romBus.ar_addr())));
}
