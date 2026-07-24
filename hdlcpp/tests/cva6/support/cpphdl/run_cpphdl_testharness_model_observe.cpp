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
    auto& csr = core.csr_regfile_i;
    auto& id = core.id_stage_i;
    auto& decoder = id.decoder_i[0];
    auto& scoreboard = core.issue_stage_i.i_scoreboard;
    auto& load = core.ex_stage_i.lsu_i.i_load_unit;
    auto& cacheWrapper = core.i_cache_subsystem.i_dcache;
    auto& hpd = cacheWrapper.i_hpdcache;
    auto& ctrl = hpd.hpdcache_ctrl_i;
    auto& ctrlPe = ctrl.hpdcache_ctrl_pe_i;
    auto& memctrl = ctrl.hpdcache_memctrl_i;
    auto& dirWay1 = memctrl.dir_sram[1];
    auto& dataWord1 = memctrl.data_sram[0][1];
    auto& miss = hpd.hpdcache_miss_handler_i;
    auto& coreArb = hpd.core_req_arbiter_i;
    auto& fixedArb = coreArb.req_arbiter_i;
    auto& dmMem = top.i_dm_top.i_dm_mem;
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
}
