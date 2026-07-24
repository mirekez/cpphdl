#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#include <fstream>
#include <map>
#define sync cpphdl_posix_sync_ignored
#include <signal.h>
#undef sync
#include <string>
#include <vector>

#include "cpphdl.h"

#ifndef CPPHDL_USE_GENERATED_PCH
#define private public
#include "all_generated.h"
#include "generated/corev_apu/tb/ariane_axi_pkg.h"
#include "generated/corev_apu/src/ariane.h"
#undef private
#endif

long _system_clock = 0;

namespace {

void crash_handler(int sig)
{
    void* frames[64];
    int n = backtrace(frames, 64);
    std::fprintf(stderr, "cpphdl signal %d backtrace:\n", sig);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    _Exit(128 + sig);
}

struct Elf32Ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf32Phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

std::vector<unsigned char> read_file(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "failed to open %s\n", path);
        std::exit(2);
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

uint64_t parse_u64(const char* s)
{
    return std::strtoull(s, nullptr, 0);
}

constexpr uint64_t DRAM_BASE = 0x80000000ull;
constexpr uint64_t UART_BASE = 0x10000000ull;

struct Memory {
    std::map<uint64_t, uint8_t> bytes;

    uint8_t read8(uint64_t addr) const
    {
        auto it = bytes.find(addr);
        if (it != bytes.end()) {
            return it->second;
        }
        return 0;
    }

    uint32_t read32(uint64_t addr) const
    {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i) {
            v |= uint32_t(read8(addr + i)) << (8 * i);
        }
        return v;
    }

    uint64_t read64(uint64_t addr) const
    {
        const uint64_t base = addr & ~7ull;
        uint64_t v = 0;
        for (unsigned i = 0; i < 8; ++i) {
            auto it = bytes.find(base + i);
            if (it != bytes.end()) {
                v |= uint64_t(it->second) << (8 * i);
            }
        }
        return v;
    }

    void write64(uint64_t addr, uint64_t data, uint64_t strb)
    {
        const uint64_t base = addr & ~7ull;
        for (unsigned i = 0; i < 8; ++i) {
            if ((strb >> i) & 1u) {
                bytes[base + i] = uint8_t(data >> (8 * i));
            }
        }
    }
};

uint32_t load_elf32(Memory& mem, const char* path)
{
    auto file = read_file(path);
    if (file.size() < sizeof(Elf32Ehdr)) {
        std::fprintf(stderr, "ELF too small: %s\n", path);
        std::exit(2);
    }
    Elf32Ehdr eh{};
    std::memcpy(&eh, file.data(), sizeof(eh));
    if (eh.e_ident[0] != 0x7f || eh.e_ident[1] != 'E' || eh.e_ident[2] != 'L' || eh.e_ident[3] != 'F' || eh.e_ident[4] != 1) {
        std::fprintf(stderr, "not an ELF32 file: %s\n", path);
        std::exit(2);
    }
    for (uint16_t i = 0; i < eh.e_phnum; ++i) {
        const auto off = eh.e_phoff + uint32_t(i) * eh.e_phentsize;
        if (off + sizeof(Elf32Phdr) > file.size()) {
            std::fprintf(stderr, "bad program header in %s\n", path);
            std::exit(2);
        }
        Elf32Phdr ph{};
        std::memcpy(&ph, file.data() + off, sizeof(ph));
        if (ph.p_type != 1) {
            continue;
        }
        const uint64_t base = uint64_t(ph.p_paddr);
        const bool has_dram_alias = base >= DRAM_BASE;
        for (uint32_t j = 0; j < ph.p_filesz; ++j) {
            const auto value = file.at(uint64_t(ph.p_offset) + j);
            mem.bytes[base + j] = value;
            if (has_dram_alias) {
                mem.bytes[base - DRAM_BASE + j] = value;
            }
        }
        for (uint32_t j = ph.p_filesz; j < ph.p_memsz; ++j) {
            mem.bytes[base + j] = 0;
            if (has_dram_alias) {
                mem.bytes[base - DRAM_BASE + j] = 0;
            }
        }
    }
    return eh.e_entry;
}

} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);

    if (argc < 2) {
        std::fprintf(stderr, "usage: %s program.riscv [tohost_addr] [max_cycles]\n", argv[0]);
        return 2;
    }

    Memory mem;
    const uint32_t entry = load_elf32(mem, argv[1]);
    const uint64_t tohost = argc > 2 ? parse_u64(argv[2]) : 0x80001000ull;
    const uint64_t fromhost = 0x80001040ull;
    const uint64_t max_cycles = argc > 3 ? parse_u64(argv[3]) : 3000000ull;
    const bool debug = std::getenv("CPPHDL_DEBUG") != nullptr;
    const bool early_debug = std::getenv("CPPHDL_EARLY_DEBUG") != nullptr;
    if (debug) {
        std::printf("DBGLOAD addr=0x80000070 data=0x%016llx\n",
                    (unsigned long long)mem.read64(0x80000070ull));
        std::printf("DBGLOAD addr=0x00003910 data=0x%016llx\n",
                    (unsigned long long)mem.read64(0x00003910ull));
        std::printf("DBGLOAD addr=0x80003910 data=0x%016llx\n",
                    (unsigned long long)mem.read64(0x80003910ull));
    }
    const bool trace_steps = std::getenv("CPPHDL_TRACE_STEPS") != nullptr;
    const bool trace_commits = std::getenv("CPPHDL_TRACE_COMMITS") != nullptr;
    const bool trace_mem = std::getenv("CPPHDL_TRACE_MEM") != nullptr;
    const bool trace_load = std::getenv("CPPHDL_TRACE_LOAD") != nullptr;
    const bool trace_hpd = std::getenv("CPPHDL_TRACE_HPD") != nullptr;
    const bool trace_instr = std::getenv("CPPHDL_TRACE_INSTR") != nullptr;
    const bool progress = std::getenv("CPPHDL_PROGRESS") != nullptr;
    const auto start_time = std::clock();

    static constexpr auto CpphdlCva6Cfg = build_config_pkg::build_config(cva6_config_pkg::cva6_cfg);
    using CpphdlRvfiInstr = cpphdl_rvfi::probes_instr_t<CpphdlCva6Cfg>;
    using CpphdlRvfiCsr = cpphdl_rvfi::probes_csr_t<CpphdlCva6Cfg>;
    using CpphdlRvfiProbes = ariane_rvfi_probes_t_default_t<CpphdlCva6Cfg, CpphdlRvfiInstr, CpphdlRvfiCsr>;
    ariane<CpphdlCva6Cfg, CpphdlRvfiInstr, CpphdlRvfiCsr, CpphdlRvfiProbes> dut;
    logic<1> rst_n = 0;
    logic<CpphdlCva6Cfg.VLEN> boot_addr = entry;
    logic<CpphdlCva6Cfg.XLEN> hart_id = 0;
    logic<2> irq = 0;
    logic<1> ipi = 0;
    logic<1> timer_irq = 0;
    logic<1> debug_req = 0;
    ariane_axi::resp_t axi_resp{};

    dut.rst_ni_in = [&]() { return &rst_n; };
    dut.boot_addr_i_in = [&]() { return &boot_addr; };
    dut.hart_id_i_in = [&]() { return &hart_id; };
    dut.irq_i_in = [&]() { return &irq; };
    dut.ipi_i_in = [&]() { return &ipi; };
    dut.time_irq_i_in = [&]() { return &timer_irq; };
    dut.debug_req_i_in = [&]() { return &debug_req; };
    dut.noc_resp_i_in = [&]() { return &axi_resp; };

    bool read_active = false;
    uint64_t read_addr = 0;
    uint64_t read_beats_left = 0;
    uint64_t read_id = 0;
    bool aw_pending = false;
    uint64_t write_addr = 0;
    uint64_t write_beats_left = 0;
    uint64_t write_id = 0;
    bool w_pending = false;
    uint64_t pending_w_data = 0;
    uint64_t pending_w_strb = 0;
    bool pending_w_last = false;
    bool b_pending = false;
    uint64_t ar_count = 0;
    uint64_t r_count = 0;
    uint64_t aw_count = 0;
    uint64_t w_count = 0;
    uint64_t b_count = 0;
    std::string uart_line;
    bool saw_uart_passed = false;
    auto trace_stack_addr = [](uint64_t addr) {
        return addr >= 0x80024000ull && addr < 0x80026000ull;
    };
    auto emit_uart_char = [&](char ch) {
        if (ch == '\n' || ch == '\r') {
            if (!uart_line.empty()) {
                std::printf("UART: %s\n", uart_line.c_str());
                if (uart_line == "PASSED") {
                    saw_uart_passed = true;
                }
                uart_line.clear();
            }
        } else {
            uart_line.push_back(ch);
        }
    };

    if (trace_steps) {
        std::fprintf(stderr, "TRACE before dut._assign()\n");
        std::fflush(stderr);
    }
    dut._assign();
    if (trace_steps) {
        std::fprintf(stderr, "TRACE after dut._assign()\n");
        std::fflush(stderr);
    }

    uint64_t cycle = 0;
    try {
    for (; cycle < max_cycles; ++cycle) {
        dut._strobe();
        ++_system_clock;

        if (debug && cycle >= 517 && cycle <= 522) {
            auto& memctrl_pre = dut.i_cva6.i_cache_subsystem.i_dcache.i_hpdcache.hpdcache_ctrl_i.hpdcache_memctrl_i;
            std::printf("DBGMEMPRE cycle=%llu init_q=%u init_next=%u init_set_q=0x%llx init_set_next=0x%llx ready=%u\n",
                        (unsigned long long)cycle,
                        unsigned(bool(memctrl_pre.init_q)),
                        unsigned(bool(memctrl_pre.init_q._next)),
                        (unsigned long long)uint64_t(memctrl_pre.init_set_q),
                        (unsigned long long)uint64_t(memctrl_pre.init_set_q._next),
                        unsigned(bool(memctrl_pre.ready_o_out())));
        }

        rst_n = cycle >= 8;

        axi_resp = {};
        axi_resp.aw_ready = !aw_pending && !b_pending;
        axi_resp.w_ready = !w_pending;
        axi_resp.ar_ready = !read_active;
        const bool r_valid_this_cycle = read_active;
        if (read_active) {
            axi_resp.r_valid = 1;
            axi_resp.r.id = read_id;
            const uint64_t read_data = mem.read64(read_addr);
            axi_resp.r.data = read_data;
            axi_resp.r.resp = 0;
            axi_resp.r.last = read_beats_left == 1;
            if (trace_mem && trace_stack_addr(read_addr)) {
                std::printf("CPPMEMR cycle=%llu addr=0x%llx data=0x%016llx id=%llu last=%u ready=%u\n",
                            (unsigned long long)cycle,
                            (unsigned long long)read_addr,
                            (unsigned long long)read_data,
                            (unsigned long long)uint64_t(axi_resp.r.id),
                            unsigned(bool(axi_resp.r.last)),
                            0u);
            }
            if (debug && ((read_addr >= 0x80000070ull && read_addr < 0x80000090ull) ||
                          (read_addr >= 0x00003900ull && read_addr < 0x00003930ull) ||
                          (read_addr >= 0x80003900ull && read_addr < 0x80003930ull))) {
                std::printf("DBGREAD cycle=%llu addr=0x%llx mem=0x%016llx axi=0x%016llx id=%llu last=%u ready=%u\n",
                            (unsigned long long)cycle,
                            (unsigned long long)read_addr,
                            (unsigned long long)read_data,
                            (unsigned long long)uint64_t(axi_resp.r.data),
                            (unsigned long long)uint64_t(axi_resp.r.id),
                            unsigned(bool(axi_resp.r.last)),
                            0u);
            }
        }
        if (b_pending) {
            axi_resp.b_valid = 1;
            axi_resp.b.id = write_id;
            axi_resp.b.resp = 0;
        }

        if (trace_steps && cycle < 16) {
            std::fprintf(stderr, "TRACE cycle=%llu before _work reset=%u\n",
                         (unsigned long long)cycle,
                         unsigned(!bool(rst_n)));
            std::fflush(stderr);
        }
        dut._work(!bool(rst_n));
        if (trace_steps && cycle < 16) {
            std::fprintf(stderr, "TRACE cycle=%llu before noc_req_o_out\n", (unsigned long long)cycle);
            std::fflush(stderr);
        }
        const auto& req = dut.noc_req_o_out();
        if (trace_instr && bool(rst_n)) {
            auto& realign = dut.i_cva6.i_frontend.i_instr_realign;
            auto& frontend = dut.i_cva6.i_frontend;
            auto& id_stage = dut.i_cva6.id_stage_i;
            const auto& realign_addr = realign.addr_o_out();
            const auto& realign_instr = realign.instr_o_out();
            const auto& frontend_entry = frontend.fetch_entry_o_out()[0];
            const auto& id_entry = id_stage.fetch_entry_i_in()[0];
            const uint64_t input_addr = uint64_t(realign.address_i_in());
            const bool queue_region =
                (uint64_t(frontend_entry.address) >= 0x80003900ull && uint64_t(frontend_entry.address) < 0x80003920ull) ||
                (uint64_t(id_entry.address) >= 0x80003900ull && uint64_t(id_entry.address) < 0x80003920ull);
            const auto& trace_if_req = frontend.icache_dreq_o_out();
            const bool trace_fetch_event = queue_region || bool(frontend.bp_valid_comb_func()) ||
                (uint64_t(trace_if_req.vaddr) >= 0x80004400ull && uint64_t(trace_if_req.vaddr) < 0x80004420ull);
            if ((input_addr >= 0x80003900ull && input_addr < 0x80003920ull) ||
                (uint64_t(realign_addr[0]) >= 0x80003900ull && uint64_t(realign_addr[0]) < 0x80003920ull) ||
                (uint64_t(realign_addr[1]) >= 0x80003900ull && uint64_t(realign_addr[1]) < 0x80003920ull)) {
                std::printf("CPPINSTR cycle=%llu in_valid=%u in_addr=0x%llx in_data=0x%llx unaligned=%u saved=0x%llx out_valid=0x%llx a0=0x%llx i0=0x%llx a1=0x%llx i1=0x%llx\n",
                            (unsigned long long)cycle,
                            unsigned(bool(realign.valid_i_in())),
                            (unsigned long long)input_addr,
                            (unsigned long long)uint64_t(realign.data_i_in()),
                            unsigned(bool(realign.unaligned_q)),
                            (unsigned long long)uint64_t(realign.unaligned_instr_q),
                            (unsigned long long)uint64_t(realign.valid_o_out()),
                            (unsigned long long)uint64_t(realign_addr[0]),
                            (unsigned long long)uint64_t(realign_instr[0]),
                            (unsigned long long)uint64_t(realign_addr[1]),
                            (unsigned long long)uint64_t(realign_instr[1]));
            }
            if (queue_region) {
                const auto& decoded = id_stage.decoder_i[0].instruction_o_out();
                std::printf("CPPID cycle=%llu fe_valid=0x%llx fe_ready=0x%llx fe_addr=0x%llx fe_instr=0x%llx id_valid=0x%llx id_addr=0x%llx id_instr=0x%llx comp_in=0x%llx comp_out=0x%llx dec_valid=%u dec_pc=0x%llx dec_fu=%llu dec_op=%llu dec_rd=%llu\n",
                            (unsigned long long)cycle,
                            (unsigned long long)uint64_t(frontend.fetch_entry_valid_o_out()),
                            (unsigned long long)uint64_t(frontend.fetch_entry_ready_i_in()),
                            (unsigned long long)uint64_t(frontend_entry.address),
                            (unsigned long long)uint64_t(frontend_entry.instruction),
                            (unsigned long long)uint64_t(id_stage.fetch_entry_valid_i_in()),
                            (unsigned long long)uint64_t(id_entry.address),
                            (unsigned long long)uint64_t(id_entry.instruction),
                            (unsigned long long)uint64_t(id_stage.compressed_decoder_i[0].instr_i_in()),
                            (unsigned long long)uint64_t(id_stage.compressed_decoder_i[0].instr_o_out()),
                            unsigned(bool(decoded.valid)),
                            (unsigned long long)uint64_t(decoded.pc),
                            (unsigned long long)uint64_t(decoded.fu),
                            (unsigned long long)uint64_t(decoded.op),
                            (unsigned long long)uint64_t(decoded.rd));
            }
            auto& instr_queue = frontend.i_instr_queue;
            if (trace_fetch_event ||
                (input_addr >= 0x80003900ull && input_addr < 0x80003920ull && uint64_t(instr_queue.valid_i_in()) != 0)) {
                const auto push_bits = instr_queue.push_instr_fifo_comb_func();
                const auto pop_bits = instr_queue.pop_instr_comb_func();
                const auto& if_req = trace_if_req;
                const auto& if_rsp = frontend.icache_dreq_i_in();
                std::printf("CPPFETCH cycle=%llu flush=%u bp_valid=%u predict=0x%llx req=%u req_addr=0x%llx kill1=%u kill2=%u rsp_valid=%u rsp_addr=0x%llx rsp_data=0x%llx q_valid=%u q_addr=0x%llx q_data=0x%llx real_valid=%u real_addr=0x%llx real_data=0x%llx\n",
                            (unsigned long long)cycle,
                            unsigned(bool(frontend.flush_i_in())),
                            unsigned(bool(frontend.bp_valid_comb_func())),
                            (unsigned long long)uint64_t(frontend.predict_address_comb_func()),
                            unsigned(bool(if_req.req)),
                            (unsigned long long)uint64_t(if_req.vaddr),
                            unsigned(bool(if_req.kill_s1)),
                            unsigned(bool(if_req.kill_s2)),
                            unsigned(bool(if_rsp.valid)),
                            (unsigned long long)uint64_t(if_rsp.vaddr),
                            (unsigned long long)uint64_t(if_rsp.data),
                            unsigned(bool(frontend.icache_valid_q)),
                            (unsigned long long)uint64_t(frontend.icache_vaddr_q),
                            (unsigned long long)uint64_t(frontend.icache_data_q),
                            unsigned(bool(realign.valid_i_in())),
                            (unsigned long long)input_addr,
                            (unsigned long long)uint64_t(realign.data_i_in()));
                for (size_t fifo_index = 0; fifo_index < CpphdlCva6Cfg.INSTR_PER_FETCH; ++fifo_index) {
                    auto& fifo = instr_queue.i_fifo_instr_data[fifo_index];
                    std::printf("CPPFIFO cycle=%llu fifo=%zu idx_is=%llu idx_ds=0x%llx valid_i=0x%llx push=0x%llx pop=0x%llx rp=%llu wp=%llu count=%llu in=0x%llx out=0x%llx next_at_wp=0x%llx\n",
                                (unsigned long long)cycle,
                                fifo_index,
                                (unsigned long long)uint64_t(instr_queue.idx_is_q),
                                (unsigned long long)uint64_t(instr_queue.idx_ds_q),
                                (unsigned long long)uint64_t(instr_queue.valid_i_in()),
                                (unsigned long long)uint64_t(push_bits),
                                (unsigned long long)uint64_t(pop_bits),
                                (unsigned long long)uint64_t(fifo.read_pointer_q),
                                (unsigned long long)uint64_t(fifo.write_pointer_q),
                                (unsigned long long)uint64_t(fifo.status_cnt_q),
                                (unsigned long long)uint64_t(fifo.data_i_in().instr),
                                (unsigned long long)uint64_t(fifo.data_o_out().instr),
                                (unsigned long long)uint64_t(fifo.mem_q._next[uint64_t(fifo.write_pointer_q)].instr));
                }
            }
        }
        if (trace_commits && bool(rst_n)) {
            auto commit_instr = dut.i_cva6.commit_instr_id_commit_comb_func();
            auto commit_ack = dut.i_cva6.commit_ack_commit_id_comb_func();
            auto we_gpr = dut.i_cva6.we_gpr_commit_id_comb_func();
            auto waddr = dut.i_cva6.waddr_commit_id_comb_func();
            auto wdata = dut.i_cva6.wdata_commit_id_comb_func();
            for (size_t lane = 0; lane < CpphdlCva6Cfg.NrCommitPorts; ++lane) {
                if (!bool(commit_instr[lane].valid) || !bool(logic<1>(commit_ack[lane]))) {
                    continue;
                }
                std::printf("CPPCOMMIT cycle=%llu pc=0x%llx fu=%llu op=%llu tid=%llu rd=%llu result=0x%llx we=0x%llx waddr=%llu wdata=0x%llx lane=%zu\n",
                            (unsigned long long)cycle,
                            (unsigned long long)uint64_t(commit_instr[lane].pc),
                            (unsigned long long)uint64_t(commit_instr[lane].fu),
                            (unsigned long long)uint64_t(commit_instr[lane].op),
                            (unsigned long long)uint64_t(commit_instr[lane].trans_id),
                            (unsigned long long)uint64_t(commit_instr[lane].rd),
                            (unsigned long long)uint64_t(commit_instr[lane].result),
                            (unsigned long long)uint64_t(logic<1>(we_gpr[lane])),
                            (unsigned long long)uint64_t(waddr[lane]),
                            (unsigned long long)uint64_t(wdata[lane]),
                            lane);
            }
        }
        if (trace_load && bool(rst_n)) {
            auto& lu = dut.i_cva6.ex_stage_i.lsu_i.i_load_unit;
            if (bool(lu.req_port_i_in().data_rvalid)) {
                const auto rindex = lu.ldbuf_rindex_comb_func();
                const auto packed = lu.ldbuf_q[(unsigned)uint64_t(rindex)].pack();
                std::printf("CPPLOAD cycle=%llu rid=%llu pack=0x%llx off=%llu op=%llu raw=0x%llx shifted=0x%llx result=0x%llx\n",
                            (unsigned long long)cycle,
                            (unsigned long long)uint64_t(rindex),
                            (unsigned long long)uint64_t(packed),
                            (unsigned long long)uint64_t(lu.ldbuf_rdata_address_offset_comb_func()),
                            (unsigned long long)uint64_t(lu.ldbuf_rdata_operation_comb_func()),
                            (unsigned long long)uint64_t(lu.req_port_i_in().data_rdata),
                            (unsigned long long)uint64_t(lu.shifted_data_comb_func()),
                            (unsigned long long)uint64_t(lu.result_o_out()));
            }
        }
        if (trace_hpd && bool(rst_n)) {
            auto& lu = dut.i_cva6.ex_stage_i.lsu_i.i_load_unit;
            auto& hpdcache = dut.i_cva6.i_cache_subsystem.i_dcache.i_hpdcache;
            auto& ctrl = hpdcache.hpdcache_ctrl_i;
            auto& miss = hpdcache.hpdcache_miss_handler_i;
            auto& memctrl = ctrl.hpdcache_memctrl_i;
            if (bool(lu.req_port_i_in().data_rvalid)) {
                const auto req_word_lanes = memctrl.data_read_req_word_mux_i.data_i_in();
                const auto req_word_mux = memctrl.data_read_req_word_mux_i.data_o_out();
                const auto way_lanes = memctrl.data_read_req_word_way_mux_i.data_i_in();
                const auto way_mux = memctrl.data_read_req_word_way_mux_i.data_o_out();
                const auto refill_lanes = miss.data_read_rsp_mux_i.data_i_in();
                const auto refill_mux = miss.data_read_rsp_mux_i.data_o_out();
                std::printf("CPPHPD cycle=%llu rd_i=%u rd_word=0x%llx rd_way=0x%llx word_idx_q=0x%llx word_sel=0x%llx word_lane0=0x%llx word_lane1=0x%llx way_lane0=0x%llx way_lane1=0x%llx way_out=0x%llx mem_o=0x%llx refill_v=%u refill_word=0x%llx refill_sel=0x%llx refill_lane0=0x%llx refill_lane1=0x%llx refill_mux=0x%llx refill_rsp=0x%llx core=0x%llx lu_raw=0x%llx\n",
                            (unsigned long long)cycle,
                            unsigned(bool(memctrl.data_req_read_i_in())),
                            (unsigned long long)uint64_t(memctrl.data_req_read_word_i_in()),
                            (unsigned long long)uint64_t(memctrl.data_req_read_way_i_in()),
                            (unsigned long long)uint64_t(memctrl.data_read_req_word_index_q),
                            (unsigned long long)uint64_t(req_word_mux),
                            (unsigned long long)uint64_t(req_word_lanes[0]),
                            (unsigned long long)uint64_t(req_word_lanes[1]),
                            (unsigned long long)uint64_t(way_lanes[0]),
                            (unsigned long long)uint64_t(way_lanes[1]),
                            (unsigned long long)uint64_t(way_mux),
                            (unsigned long long)uint64_t(memctrl.data_req_read_data_o_out()),
                            unsigned(bool(ctrl.refill_core_rsp_valid_i_in())),
                            (unsigned long long)uint64_t(miss.refill_core_rsp_word_comb_func()),
                            (unsigned long long)uint64_t(miss.data_read_rsp_mux_i.sel_i_in()),
                            (unsigned long long)uint64_t(refill_lanes[0]),
                            (unsigned long long)uint64_t(refill_lanes[1]),
                            (unsigned long long)uint64_t(refill_mux),
                            (unsigned long long)uint64_t(ctrl.refill_core_rsp_i_in().rdata),
                            (unsigned long long)uint64_t(ctrl.core_rsp_o_out().rdata),
                            (unsigned long long)uint64_t(lu.req_port_i_in().data_rdata));
            }
        }
        if (debug && cycle >= 268 && cycle < 282) {
            auto& cache_early = dut.i_cva6.i_cache_subsystem.i_cva6_icache;
            const auto& comb_rsp_early = cache_early.dreq_o_comb_func();
            std::printf("DBGEARLY cycle=%llu state=%llu state_d=%llu mem_vld=%u comb_valid=%u port_valid=%u ready=%u\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(cache_early.state_q),
                        (unsigned long long)uint64_t(cache_early.state_d_comb_func()),
                        unsigned(bool(cache_early.mem_rtrn_vld_i_in())),
                        unsigned(bool(comb_rsp_early.valid)),
                        unsigned(bool(cache_early.dreq_o_out().valid)),
                        unsigned(bool(comb_rsp_early.ready)));
        }
        if (trace_steps && cycle < 16) {
            std::fprintf(stderr, "TRACE cycle=%llu after noc_req_o_out aw=%u w=%u ar=%u rready=%u bready=%u\n",
                         (unsigned long long)cycle,
                         unsigned(bool(req.aw_valid)),
                         unsigned(bool(req.w_valid)),
                         unsigned(bool(req.ar_valid)),
                         unsigned(bool(req.r_ready)),
                         unsigned(bool(req.b_ready)));
            std::fflush(stderr);
        }
        if (!read_active && bool(req.ar_valid)) {
            read_active = true;
            read_addr = req.ar.addr;
            read_beats_left = uint64_t(req.ar.len) + 1;
            read_id = req.ar.id;
            ++ar_count;
            if (trace_mem && trace_stack_addr(read_addr)) {
                std::printf("CPPMEMAR cycle=%llu addr=0x%llx len=%llu id=%llu\n",
                            (unsigned long long)cycle,
                            (unsigned long long)read_addr,
                            (unsigned long long)(read_beats_left - 1),
                            (unsigned long long)read_id);
            }
            if (debug && (ar_count <= 32 || (cycle >= 450 && cycle < 530) ||
                          (read_addr >= 0x80003900ull && read_addr < 0x80003940ull))) {
                std::printf("DBG cycle=%llu AR addr=0x%llx len=%llu id=%llu\n",
                            (unsigned long long)cycle,
                            (unsigned long long)read_addr,
                            (unsigned long long)(read_beats_left - 1),
                            (unsigned long long)read_id);
            }
        }
        if (r_valid_this_cycle) {
            if (bool(req.r_ready)) {
                ++r_count;
                read_addr += 8;
                if (--read_beats_left == 0) {
                    read_active = false;
                }
            }
        }

        if (!aw_pending && !b_pending && bool(req.aw_valid)) {
            aw_pending = true;
            write_addr = req.aw.addr;
            write_beats_left = uint64_t(req.aw.len) + 1;
            write_id = req.aw.id;
            ++aw_count;
            if (trace_mem && trace_stack_addr(write_addr)) {
                std::printf("CPPMEMAW cycle=%llu addr=0x%llx len=%llu id=%llu\n",
                            (unsigned long long)cycle,
                            (unsigned long long)write_addr,
                            (unsigned long long)(write_beats_left - 1),
                            (unsigned long long)write_id);
            }
            if (debug && aw_count <= 32) {
                std::printf("DBG cycle=%llu AW addr=0x%llx len=%llu id=%llu\n",
                            (unsigned long long)cycle,
                            (unsigned long long)write_addr,
                            (unsigned long long)(write_beats_left - 1),
                            (unsigned long long)write_id);
            }
        }
        if (!w_pending && bool(req.w_valid)) {
            w_pending = true;
            pending_w_data = uint64_t(req.w.data);
            pending_w_strb = uint64_t(req.w.strb);
            pending_w_last = bool(req.w.last);
            ++w_count;
            if (trace_mem) {
                std::printf("CPPMEMW cycle=%llu data=0x%016llx strb=0x%02llx last=%u\n",
                            (unsigned long long)cycle,
                            (unsigned long long)pending_w_data,
                            (unsigned long long)pending_w_strb,
                            unsigned(pending_w_last));
            }
            if (debug && w_count <= 64) {
                std::printf("DBG cycle=%llu W data=0x%llx strb=0x%llx last=%u\n",
                            (unsigned long long)cycle,
                            (unsigned long long)pending_w_data,
                            (unsigned long long)pending_w_strb,
                            unsigned(pending_w_last));
            }
        }
        if (aw_pending && w_pending) {
            mem.write64(write_addr, pending_w_data, pending_w_strb);
            if (trace_mem && trace_stack_addr(write_addr)) {
                std::printf("CPPMEMWR cycle=%llu addr=0x%llx data=0x%016llx strb=0x%02llx after=0x%016llx\n",
                            (unsigned long long)cycle,
                            (unsigned long long)write_addr,
                            (unsigned long long)pending_w_data,
                            (unsigned long long)pending_w_strb,
                            (unsigned long long)mem.read64(write_addr));
            }
            for (unsigned i = 0; i < 8; ++i) {
                if (((pending_w_strb >> i) & 1u) && write_addr + i == UART_BASE) {
                    const char ch = char((pending_w_data >> (8 * i)) & 0xffu);
                    emit_uart_char(ch);
                }
            }
            const bool tohost_write = (write_addr == tohost) || (write_addr + DRAM_BASE == tohost);
            if (tohost_write && pending_w_data != 0 && ((pending_w_data & 1u) == 0)) {
                const uint64_t payload = uint32_t(pending_w_data);
                uint64_t syscall_num = mem.read64(payload);
                uint64_t fd = mem.read64(payload + 8);
                uint64_t buf = mem.read64(payload + 16);
                uint64_t len = mem.read64(payload + 24);
                if (syscall_num > 0xffffffffull) {
                    syscall_num = mem.read32(payload);
                    fd = mem.read32(payload + 4);
                    buf = mem.read32(payload + 8);
                    len = mem.read32(payload + 12);
                }
                if (syscall_num == 64u) {
                    if (fd == 1u || fd == 2u) {
                        for (uint64_t i = 0; i < len; ++i) {
                            emit_uart_char(char(mem.read8(buf + i)));
                        }
                    }
                    mem.write64(tohost, 0, 0xff);
                    mem.write64(tohost - DRAM_BASE, 0, 0xff);
                    mem.write64(fromhost, 1, 0xff);
                    mem.write64(fromhost - DRAM_BASE, 1, 0xff);
                }
            } else if (tohost_write && (pending_w_data & 1u)) {
                const uint32_t exit_word = uint32_t(pending_w_data);
                if (exit_word == 1u) {
                    if (!saw_uart_passed) {
                        std::printf("cpphdl FAIL missing UART PASSED before tohost pass\n");
                        return 1;
                    }
                    std::printf("cpphdl PASS cycle=%llu tohost=0x%llx data=0x%llx\n",
                                (unsigned long long)cycle,
                                (unsigned long long)tohost,
                                (unsigned long long)pending_w_data);
                    return 0;
                }
                if (debug) {
                    std::printf("DBGTOHOST cycle=%llu raw_axi_ignored tohost=0x%llx data=0x%llx exit_word=0x%x\n",
                                (unsigned long long)cycle,
                                (unsigned long long)tohost,
                                (unsigned long long)pending_w_data,
                                unsigned(exit_word));
                }
            }
            w_pending = false;
            write_addr += 8;
            if (--write_beats_left == 0 || pending_w_last) {
                aw_pending = false;
                b_pending = true;
            }
        }
        if (b_pending) {
            if (bool(req.b_ready)) {
                ++b_count;
                b_pending = false;
            }
        }
        if (debug && ((cycle >= 428 && cycle <= 438) || (cycle >= 480 && cycle <= 492))) {
            auto& cache_sub_probe = dut.i_cva6.i_cache_subsystem;
            auto& adapter_probe = cache_sub_probe.i_axi_arbiter.i_hpdcache_mem_to_axi_read;
            auto& cache_probe = cache_sub_probe.i_cva6_icache;
            std::printf("DBGUPPER cycle=%llu axi_valid=%u axi_data=0x%016llx shim_data=0x%016llx resp_data=0x%016llx ic_in=0x%016llx ic_valid=%u\n",
                        (unsigned long long)cycle,
                        unsigned(bool(axi_resp.r_valid)),
                        (unsigned long long)uint64_t(axi_resp.r.data),
                        (unsigned long long)uint64_t(adapter_probe.axi_r_i_in().data),
                        (unsigned long long)uint64_t(adapter_probe.resp_o_out().mem_resp_r_data),
                        (unsigned long long)uint64_t(cache_probe.mem_rtrn_i_in().data),
                        unsigned(bool(cache_probe.mem_rtrn_vld_i_in())));
            auto& arb_probe = cache_sub_probe.i_axi_arbiter;
            auto& up_probe = arb_probe.i_icache_hpdcache_data_upsize;
            std::printf("DBGUPSIZE cycle=%llu demux_data=0x%016llx up_w=%u up_wdata=0x%016llx up_buf0=0x%016llx up_rdata=0x%016llx miss_data=0x%016llx miss_raw=0x%016llx miss_valid=%u\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(arb_probe.icache_miss_resp_wdata_comb_func().mem_resp_r_data),
                        unsigned(bool(up_probe.w_i_in())),
                        (unsigned long long)uint64_t(up_probe.wdata_i_in()),
                        (unsigned long long)uint64_t(up_probe.buf_q[0][0]),
                        (unsigned long long)uint64_t(up_probe.rdata_o_out()),
                        (unsigned long long)uint64_t(arb_probe.icache_miss_resp_o_out().data),
                        (unsigned long long)uint64_t(arb_probe.icache_miss_resp_data_rdata_comb_func()),
                        unsigned(bool(arb_probe.icache_miss_resp_valid_o_out())));
        }

        if (debug && ((cycle < 24) || (cycle >= 240 && cycle < 560) || (cycle != 0 && (cycle % 100000ull) == 0))) {
            auto& fe = dut.i_cva6.i_frontend;
            auto& cache_sub = dut.i_cva6.i_cache_subsystem;
            auto& ic = cache_sub.i_cva6_icache;
            auto& arb = cache_sub.i_axi_arbiter;
            const auto& if_req = fe.icache_dreq_o_out();
            const auto& if_rsp = fe.icache_dreq_i_in();
            const auto& ic_mem = ic.mem_data_o_out();
            const auto& miss = cache_sub.icache_miss_comb_func();
            const auto& axi_req_dbg = arb.axi_req_o_out();
            if (cycle >= 258 && cycle < 272) {
                const auto arb_in = arb.icache_miss_i_in();
                const auto req_wdata = arb.icache_miss_req_wdata_comb_func();
                const auto fifo_wdata = arb.i_icache_miss_req_fifo.wdata_i_in();
                const auto fifo_rdata = arb.i_icache_miss_req_fifo.rdata_o_out();
                const auto req_rdata = arb.icache_miss_req_rdata_comb_func();
                const auto mem_req0 = arb.mem_req_read_comb_func()[0];
                const auto mem_req1 = arb.mem_req_read_comb_func()[1];
                const auto arbiter_in0 = arb.i_mem_req_read_arbiter.mem_req_read_i_in()[0];
                const auto arbiter_in1 = arb.i_mem_req_read_arbiter.mem_req_read_i_in()[1];
                const auto arbiter_out = arb.i_mem_req_read_arbiter.mem_req_read_o_out();
                const auto mem_req_arb = arb.mem_req_read_arb_comb_func();
                const auto axi_read_in = arb.i_hpdcache_mem_to_axi_read.req_i_in();
                const auto axi_read_ar = arb.i_hpdcache_mem_to_axi_read.axi_ar_o_out();
                const auto top_req_dbg = dut.noc_req_o_out();
                std::printf("DBGREQ cycle=%llu cfg_tid_w=%u icache_rdtxid=%u ic_mem_tid=%llu ic_mem_paddr=0x%llx miss_tid=%llu miss_paddr=0x%llx arb_in_tid=%llu arb_in_paddr=0x%llx wdata_id=%llu wdata_addr=0x%llx fifo_w_id=%llu fifo_w_addr=0x%llx fifo_r_id=%llu fifo_r_addr=0x%llx req_r_id=%llu req_r_addr=0x%llx mem0_id=%llu mem0_addr=0x%llx mem1_id=%llu mem1_addr=0x%llx arbin0_id=%llu arbin0_addr=0x%llx arbin1_id=%llu arbin1_addr=0x%llx arbout_id=%llu arbout_addr=0x%llx memarb_id=%llu memarb_addr=0x%llx axiin_id=%llu axiin_addr=0x%llx axiar_id=%llu axiar_addr=0x%llx top_id=%llu top_addr=0x%llx\n",
                            (unsigned long long)cycle,
                            unsigned(build_config_pkg::build_config(cva6_config_pkg::cva6_cfg).MEM_TID_WIDTH),
                            unsigned(cache_sub.ICACHE_RDTXID),
                            (unsigned long long)uint64_t(ic_mem.tid),
                            (unsigned long long)uint64_t(ic_mem.paddr),
                            (unsigned long long)uint64_t(miss.tid),
                            (unsigned long long)uint64_t(miss.paddr),
                            (unsigned long long)uint64_t(arb_in.tid),
                            (unsigned long long)uint64_t(arb_in.paddr),
                            (unsigned long long)uint64_t(req_wdata.mem_req_id),
                            (unsigned long long)uint64_t(req_wdata.mem_req_addr),
                            (unsigned long long)uint64_t(fifo_wdata.mem_req_id),
                            (unsigned long long)uint64_t(fifo_wdata.mem_req_addr),
                            (unsigned long long)uint64_t(fifo_rdata.mem_req_id),
                            (unsigned long long)uint64_t(fifo_rdata.mem_req_addr),
                            (unsigned long long)uint64_t(req_rdata.mem_req_id),
                            (unsigned long long)uint64_t(req_rdata.mem_req_addr),
                            (unsigned long long)uint64_t(mem_req0.mem_req_id),
                            (unsigned long long)uint64_t(mem_req0.mem_req_addr),
                            (unsigned long long)uint64_t(mem_req1.mem_req_id),
                            (unsigned long long)uint64_t(mem_req1.mem_req_addr),
                            (unsigned long long)uint64_t(arbiter_in0.mem_req_id),
                            (unsigned long long)uint64_t(arbiter_in0.mem_req_addr),
                            (unsigned long long)uint64_t(arbiter_in1.mem_req_id),
                            (unsigned long long)uint64_t(arbiter_in1.mem_req_addr),
                            (unsigned long long)uint64_t(arbiter_out.mem_req_id),
                            (unsigned long long)uint64_t(arbiter_out.mem_req_addr),
                            (unsigned long long)uint64_t(mem_req_arb.mem_req_id),
                            (unsigned long long)uint64_t(mem_req_arb.mem_req_addr),
                            (unsigned long long)uint64_t(axi_read_in.mem_req_id),
                            (unsigned long long)uint64_t(axi_read_in.mem_req_addr),
                            (unsigned long long)uint64_t(axi_read_ar.id),
                            (unsigned long long)uint64_t(axi_read_ar.addr),
                            (unsigned long long)uint64_t(top_req_dbg.ar.id),
                            (unsigned long long)uint64_t(top_req_dbg.ar.addr));
            }
            std::printf("DBGHPD cycle=%llu ar=%llu r=%llu rst=%u npc=0x%llx if_ready=%u if_req=%u if_vaddr=0x%llx if_rsp_ready=%u if_rsp_valid=%u ic_state=%llu ic_state_d=%llu flush_done=%u cache_en=%u cache_en_d=%u ic_dreq=%u ic_dvaddr=0x%llx ic_areq_req=%u ic_areq_vaddr=0x%llx ic_areq_valid=%u ic_areq_paddr=0x%llx ic_mem_req=%u ic_mem_ack=%u ic_mem_paddr=0x%llx sub_miss_valid=%u sub_miss_ready=%u sub_miss_paddr=0x%llx arb_in_valid=%u arb_ready=%u arb_fifo_wok=%u arb_fifo_rok=%u arb_pending=%u mem_valid=0x%llx mem_ready=0x%llx mem_arb_valid=%u mem_arb_addr=0x%llx mem_arb_ready=%u axi_ar=%u axi_addr=0x%llx axi_len=%llu axi_rready=%u resp_ar_ready=%u resp_r_valid=%u\n",
                        (unsigned long long)cycle,
                        (unsigned long long)ar_count,
                        (unsigned long long)r_count,
                        unsigned(bool(rst_n)),
                        (unsigned long long)uint64_t(fe.npc_q),
                        unsigned(bool(fe.if_ready_comb_func())),
                        unsigned(bool(if_req.req)),
                        (unsigned long long)uint64_t(if_req.vaddr),
                        unsigned(bool(if_rsp.ready)),
                        unsigned(bool(if_rsp.valid)),
                        (unsigned long long)uint64_t(ic.state_q),
                        (unsigned long long)uint64_t(ic.state_d_comb_func()),
                        unsigned(bool(ic.flush_done_comb_func())),
                        unsigned(bool(ic.cache_en_q)),
                        unsigned(bool(ic.cache_en_d_comb_func())),
                        unsigned(bool(ic.dreq_i_in().req)),
                        (unsigned long long)uint64_t(ic.dreq_i_in().vaddr),
                        unsigned(bool(ic.areq_o_out().fetch_req)),
                        (unsigned long long)uint64_t(ic.areq_o_out().fetch_vaddr),
                        unsigned(bool(ic.areq_i_in().fetch_valid)),
                        (unsigned long long)uint64_t(ic.areq_i_in().fetch_paddr),
                        unsigned(bool(ic.mem_data_req_o_out())),
                        unsigned(bool(ic.mem_data_ack_i_in())),
                        (unsigned long long)uint64_t(ic_mem.paddr),
                        unsigned(bool(cache_sub.icache_miss_valid_comb_func())),
                        unsigned(bool(cache_sub.icache_miss_ready_comb_func())),
                        (unsigned long long)uint64_t(miss.paddr),
                        unsigned(bool(arb.icache_miss_valid_i_in())),
                        unsigned(bool(arb.icache_miss_ready_o_out())),
                        unsigned(bool(arb.icache_miss_req_wok_comb_func())),
                        unsigned(bool(arb.icache_miss_req_rok_comb_func())),
                        unsigned(bool(arb.icache_miss_pending_q)),
                        (unsigned long long)uint64_t(arb.mem_req_read_valid_comb_func()),
                        (unsigned long long)uint64_t(arb.mem_req_read_ready_comb_func()),
                        unsigned(bool(arb.mem_req_read_valid_arb_comb_func())),
                        (unsigned long long)uint64_t(arb.mem_req_read_arb_comb_func().mem_req_addr),
                        unsigned(bool(arb.mem_req_read_ready_arb_comb_func())),
                        unsigned(bool(axi_req_dbg.ar_valid)),
                        (unsigned long long)uint64_t(axi_req_dbg.ar.addr),
                        (unsigned long long)uint64_t(axi_req_dbg.ar.len),
                        unsigned(bool(axi_req_dbg.r_ready)),
                        unsigned(bool(axi_resp.ar_ready)),
                        unsigned(bool(axi_resp.r_valid)));
            std::printf("DBGHPD2 cycle=%llu tb_rid=%llu tb_last=%u arb_rid=%llu arb_last=%u child_rid=%llu child_last=%u child_resp_id=%llu child_resp_last=%u resp_valid=%u resp_id=%llu resp_last=%u arb_resp_last=%u ic_data_last=%u rt0=%llu rt1=%llu sel=%llu demux_ready_o=%u ready_i0=%u ready_i1=%u valid_o0=%u valid_o1=%u ic_w=%u ic_wlast=%u ic_wok=%u ic_data_wok=%u ic_meta_wok=%u ic_data_rok=%u ic_meta_rok=%u data_r=%u meta_r=%u data_fifo_w=%u data_fifo_last=%u data_fifo_wok=%u data_fifo_rok=%u data_fifo_empty=%u data_fifo_full=%u data_fifo_used=%llu meta_fifo_w=%u meta_fifo_wok=%u meta_fifo_rok=%u meta_fifo_r=%u meta_fifo_empty=%u meta_fifo_full=%u meta_fifo_data=%llu\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(axi_resp.r.id),
                        unsigned(bool(axi_resp.r.last)),
                        (unsigned long long)uint64_t(arb.axi_resp_i_in().r.id),
                        unsigned(bool(arb.axi_resp_i_in().r.last)),
                        (unsigned long long)uint64_t(arb.i_hpdcache_mem_to_axi_read.axi_r_i_in().id),
                        unsigned(bool(arb.i_hpdcache_mem_to_axi_read.axi_r_i_in().last)),
                        (unsigned long long)uint64_t(arb.i_hpdcache_mem_to_axi_read.resp_o_out().mem_resp_r_id),
                        unsigned(bool(arb.i_hpdcache_mem_to_axi_read.resp_o_out().mem_resp_r_last)),
                        unsigned(bool(arb.mem_resp_read_valid_comb_func())),
                        (unsigned long long)uint64_t(arb.mem_resp_read_comb_func().mem_resp_r_id),
                        unsigned(bool(arb.mem_resp_read_comb_func().mem_resp_r_last)),
                        unsigned(bool(arb.mem_resp_read_arb_comb_func()[0].mem_resp_r_last)),
                        unsigned(bool(arb.icache_miss_resp_wdata_comb_func().mem_resp_r_last)),
                        (unsigned long long)uint64_t(arb.mem_resp_read_rt_comb_func()[0]),
                        (unsigned long long)uint64_t(arb.mem_resp_read_rt_comb_func()[1]),
                        (unsigned long long)uint64_t(arb.i_mem_resp_read_demux.mem_resp_demux_sel_comb_func()),
                        unsigned(bool(arb.i_mem_resp_read_demux.mem_resp_ready_o_out())),
                        unsigned(bool(arb.i_mem_resp_read_demux.mem_resp_ready_i_in()[0])),
                        unsigned(bool(arb.i_mem_resp_read_demux.mem_resp_ready_i_in()[1])),
                        unsigned(bool(arb.i_mem_resp_read_demux.mem_resp_valid_o_out()[0])),
                        unsigned(bool(arb.i_mem_resp_read_demux.mem_resp_valid_o_out()[1])),
                        unsigned(bool(arb.icache_miss_resp_w_comb_func())),
                        unsigned(bool(arb.icache_miss_resp_wdata_comb_func().mem_resp_r_last)),
                        unsigned(bool(arb.icache_miss_resp_wok_comb_func())),
                        unsigned(bool(arb.icache_miss_resp_data_wok_comb_func())),
                        unsigned(bool(arb.icache_miss_resp_meta_wok_comb_func())),
                        unsigned(bool(arb.icache_miss_resp_data_rok_comb_func())),
                        unsigned(bool(arb.icache_miss_resp_meta_rok_comb_func())),
                        unsigned(bool(arb.icache_miss_resp_data_r_comb_func())),
                        unsigned(bool(arb.icache_miss_resp_meta_r_comb_func())),
                        unsigned(bool(arb.i_icache_hpdcache_data_upsize.w_i_in())),
                        unsigned(bool(arb.i_icache_hpdcache_data_upsize.wlast_i_in())),
                        unsigned(bool(arb.i_icache_hpdcache_data_upsize.wok_o_out())),
                        unsigned(bool(arb.i_icache_hpdcache_data_upsize.rok_o_out())),
                        unsigned(bool(arb.i_icache_hpdcache_data_upsize.empty_comb_func())),
                        unsigned(bool(arb.i_icache_hpdcache_data_upsize.full_comb_func())),
                        (unsigned long long)uint64_t(arb.i_icache_hpdcache_data_upsize.used_q),
                        unsigned(bool(arb.i_icache_refill_meta_fifo.w_i_in())),
                        unsigned(bool(arb.i_icache_refill_meta_fifo.wok_o_out())),
                        unsigned(bool(arb.i_icache_refill_meta_fifo.rok_o_out())),
                        unsigned(bool(arb.i_icache_refill_meta_fifo.r_i_in())),
                        unsigned(bool(arb.i_icache_refill_meta_fifo.empty_comb_func())),
                        unsigned(bool(arb.i_icache_refill_meta_fifo.full_comb_func())),
                        (unsigned long long)uint64_t(arb.i_icache_refill_meta_fifo.rdata_o_out()));
        }
        #if 0
        if (debug && ((cycle < 24) || (cycle >= 240 && cycle < 430) || (cycle >= 420 && cycle < 560) || (cycle < 512 && (cycle % 32ull) == 0) || (cycle != 0 && (cycle % 100000ull) == 0))) {
            const double elapsed = double(std::clock() - start_time) / double(CLOCKS_PER_SEC);
            const auto& if_req = dut.i_cva6.i_frontend.icache_dreq_o_out();
            const auto& if_resp = dut.i_cva6.i_frontend.icache_dreq_i_in();
            auto& cache = dut.i_cva6.i_cache_subsystem.i_cva6_icache;
            auto& adapter = dut.i_cva6.i_cache_subsystem.i_adapter;
            auto& lsu = dut.i_cva6.ex_stage_i.lsu_i;
            auto& mmu = lsu.i_cva6_mmu;
            auto& fe = dut.i_cva6.i_frontend;
            auto& iq = fe.i_instr_queue;
            auto& id = dut.i_cva6.id_stage_i;
            auto& issue = dut.i_cva6.issue_stage_i;
            auto& iro = issue.i_issue_read_operands;
            auto& ex = dut.i_cva6.ex_stage_i;
            auto& commit = dut.i_cva6.commit_stage_i;
            const auto& ic_mem = cache.mem_data_o_out();
            const auto& ad_in = adapter.icache_data_i_in();
            const auto& ad_fifo = adapter.icache_data_comb_func();
            auto& ic_fifo_dbg = adapter.i_icache_data_fifo;
            adapter.dcache_wr_pop_comb_func();
            adapter.dcache_rd_shift_user_d_comb_func();
            const auto& itlb_content = mmu.itlb_content_comb_func();
            const auto& itlb_g_content = mmu.itlb_g_content_comb_func();
            const auto& itlb_is_page = mmu.itlb_is_page_comb_func();
            const auto& itlb_hit = mmu.itlb_lu_hit_comb_func();
            std::printf("DBG cycle=%llu elapsed=%.1fs ar=%llu r=%llu aw=%llu w=%llu b=%llu rst=%u boot=0x%llx npc=0x%llx npc_rst=%u set_pc=%u pc_commit=0x%llx if_ready=%u if_req=%u if_vaddr=0x%llx iq_ready=%u halt=%u fe_valid=0x%llx fe_ready=0x%llx iq_valid=0x%llx iq_ready_i=0x%llx iq_fire=0x%llx iq_pop=0x%llx iq_full=0x%llx iq_empty=0x%llx iq_idx=0x%llx iq_rst_addr=%u iq_pc=0x%llx iq_addr_full=%u iq_addr_pop=%u id_ready=0x%llx id_fetch_valid=0x%llx id_issue_valid=0x%llx issue_ack=0x%llx issue_sb_full=%u ic_ready=%u ic_valid=%u ic_data=0x%llx ic_state=%llu ic_state_d=%llu flush_cnt=%llu ic_dreq=%u ic_dvaddr=0x%llx ic_areq_req=%u ic_areq_vaddr=0x%llx mmu_in_req=%u mmu_in_vaddr=0x%llx mmu_out_valid=%u mmu_out_paddr=0x%llx pmp_out_valid=%u pmp_match_exec=%u pmp_if_allow=%u lsu_out_valid=%u areq_valid=%u areq_paddr=0x%llx areq_ex=%u areq_cause=%llu flush_done=%u cache_en=%u cache_en_d=%u cache_req=%u cache_ack=%u mem_vld=%u mem_rtype=%llu ret_vld=%u ret_vld_q=%u ret_vld_d=%u ret_rd_en=%u axi_r_valid=%u axi_r_ready=%u axi_r_last=%u axi_r_id=%llu cl_tag=0x%llx vaddr_q=0x%llx mem_paddr=0x%llx ad_in=0x%llx fifo_paddr=0x%llx axi_rd=0x%llx axi_rd_req=%u shim_rd_req=%u ar_ready=%u rd_gnt=%u arb_gnt=%u arb_req=0x%llx arb_idx=%u arb_ack=0x%llx ic_empty=%u ic_full=%u ic_rd_full=%u dc_empty=%u dc_wr_full=%u dc_rd_full=%u fifo0_full=%u fifo0_empty=%u\n",
                        (unsigned long long)cycle,
                        elapsed,
                        (unsigned long long)ar_count,
                        (unsigned long long)r_count,
                        (unsigned long long)aw_count,
                        (unsigned long long)w_count,
                        (unsigned long long)b_count,
                        unsigned(bool(rst_n)),
                        (unsigned long long)uint64_t(boot_addr),
                        (unsigned long long)uint64_t(dut.i_cva6.i_frontend.npc_q),
                        unsigned(bool(dut.i_cva6.i_frontend.npc_rst_load_q)),
                        unsigned(bool(dut.i_cva6.i_frontend.set_pc_commit_i_in())),
                        (unsigned long long)uint64_t(dut.i_cva6.i_frontend.pc_commit_i_in()),
                        unsigned(bool(dut.i_cva6.i_frontend.if_ready_comb_func())),
                        unsigned(bool(if_req.req)),
                        (unsigned long long)uint64_t(if_req.vaddr),
                        unsigned(bool(iq.ready_o_out())),
                        unsigned(bool(fe.halt_frontend_i_in())),
                        (unsigned long long)uint64_t(fe.fetch_entry_valid_o_out()),
                        (unsigned long long)uint64_t(fe.fetch_entry_ready_i_in()),
                        (unsigned long long)uint64_t(iq.fetch_entry_valid_o_out()),
                        (unsigned long long)uint64_t(iq.fetch_entry_ready_i_in()),
                        (unsigned long long)uint64_t(iq.fetch_entry_fire_comb_func()),
                        (unsigned long long)uint64_t(iq.pop_instr_comb_func()),
                        (unsigned long long)uint64_t(iq.instr_queue_full_comb_func()),
                        (unsigned long long)uint64_t(iq.instr_queue_empty_comb_func()),
                        (unsigned long long)uint64_t(iq.idx_ds_q),
                        unsigned(bool(iq.reset_address_q)),
                        (unsigned long long)uint64_t(iq.pc_q),
                        unsigned(bool(iq.full_address_comb_func())),
                        unsigned(bool(iq.pop_address_comb_func())),
                        (unsigned long long)uint64_t(id.fetch_entry_ready_o_out()),
                        (unsigned long long)uint64_t(id.fetch_entry_valid_i_in()),
                        (unsigned long long)uint64_t(id.issue_entry_valid_o_out()),
                        (unsigned long long)uint64_t(issue.decoded_instr_ack_o_out()),
                        unsigned(bool(issue.sb_full_o_out())),
                        unsigned(bool(if_resp.ready)),
                        unsigned(bool(if_resp.valid)),
                        (unsigned long long)uint64_t(if_resp.data),
                        (unsigned long long)uint64_t(cache.state_q),
                        (unsigned long long)uint64_t(cache.state_d_comb_func()),
                        (unsigned long long)uint64_t(cache.flush_cnt_q),
                        unsigned(bool(cache.dreq_i_in().req)),
                        (unsigned long long)uint64_t(cache.dreq_i_in().vaddr),
                        unsigned(bool(cache.areq_o_out().fetch_req)),
                        (unsigned long long)uint64_t(cache.areq_o_out().fetch_vaddr),
                        unsigned(bool(mmu.icache_areq_i_in().fetch_req)),
                        (unsigned long long)uint64_t(mmu.icache_areq_i_in().fetch_vaddr),
                        unsigned(bool(mmu.icache_areq_o_out().fetch_valid)),
                        (unsigned long long)uint64_t(mmu.icache_areq_o_out().fetch_paddr),
                        unsigned(bool(lsu.i_pmp_data_if.icache_areq_o_out().fetch_valid)),
                        unsigned(bool(lsu.i_pmp_data_if.match_any_execute_region_comb_func())),
                        unsigned(bool(lsu.i_pmp_data_if.pmp_if_allow_comb_func())),
                        unsigned(bool(lsu.icache_areq_o_out().fetch_valid)),
                        unsigned(bool(cache.areq_i_in().fetch_valid)),
                        (unsigned long long)uint64_t(cache.areq_i_in().fetch_paddr),
                        unsigned(bool(cache.areq_i_in().fetch_exception.valid)),
                        (unsigned long long)uint64_t(cache.areq_i_in().fetch_exception.cause),
                        unsigned(bool(cache.flush_done_comb_func())),
                        unsigned(bool(cache.cache_en_q)),
                        unsigned(bool(cache.cache_en_d_comb_func())),
                        unsigned(bool(cache.mem_data_req_o_out())),
                        unsigned(bool(cache.mem_data_ack_i_in())),
                        unsigned(bool(cache.mem_rtrn_vld_i_in())),
                        (unsigned long long)uint64_t(cache.mem_rtrn_i_in().rtype),
                        unsigned(bool(adapter.icache_rtrn_vld_o_out())),
                        unsigned(bool(adapter.icache_rtrn_vld_q)),
                        unsigned(bool(adapter.icache_rtrn_vld_d_comb_func())),
                        unsigned(bool(adapter.icache_rtrn_rd_en_comb_func())),
                        unsigned(bool(adapter.axi_rd_valid_comb_func())),
                        unsigned(bool(req.r_ready)),
                        unsigned(bool(adapter.axi_rd_last_comb_func())),
                        (unsigned long long)uint64_t(adapter.axi_rd_id_out_comb_func()),
                        (unsigned long long)uint64_t(cache.cl_tag_d_comb_func()),
                        (unsigned long long)uint64_t(cache.vaddr_q),
                        (unsigned long long)uint64_t(ic_mem.paddr),
                        (unsigned long long)uint64_t(ad_in.paddr),
                        (unsigned long long)uint64_t(ad_fifo.paddr),
                        (unsigned long long)uint64_t(adapter.axi_rd_addr_comb_func()),
                        unsigned(bool(adapter.axi_rd_req_comb_func())),
                        unsigned(bool(adapter.i_axi_shim.rd_req_i_in())),
                        unsigned(bool(adapter.i_axi_shim.axi_resp_i_in().ar_ready)),
                        unsigned(bool(adapter.axi_rd_gnt_comb_func())),
                        unsigned(bool(adapter.arb_gnt_comb_func())),
                        (unsigned long long)uint64_t(adapter.arb_req_comb_func()),
                        unsigned(bool(adapter.arb_idx_comb_func())),
                        (unsigned long long)uint64_t(adapter.arb_ack_comb_func()),
                        unsigned(bool(adapter.icache_data_empty_comb_func())),
                        unsigned(bool(adapter.icache_data_full_comb_func())),
                        unsigned(bool(adapter.icache_rd_full_comb_func())),
                        unsigned(bool(adapter.dcache_data_empty_comb_func())),
                        unsigned(bool(adapter.dcache_wr_full_comb_func())),
                        unsigned(bool(adapter.dcache_rd_full_comb_func())),
                        unsigned(bool(dut.i_cva6.i_frontend.i_instr_queue.i_fifo_instr_data[0].full_o_out())),
                        unsigned(bool(dut.i_cva6.i_frontend.i_instr_queue.i_fifo_instr_data[0].empty_o_out())));
            std::printf("DBGFIFO cycle=%llu push=%u pop=%u full=%u empty=%u status=%llu status_n=%llu rptr=%llu rptr_n=%llu wptr=%llu wptr_n=%llu data_i_paddr=0x%llx data_o_paddr=0x%llx mem0_paddr=0x%llx mem1_paddr=0x%llx mem_n0_paddr=0x%llx mem_n1_paddr=0x%llx gate=%u\n",
                        (unsigned long long)cycle,
                        unsigned(bool(ic_fifo_dbg.push_i_in())),
                        unsigned(bool(ic_fifo_dbg.pop_i_in())),
                        unsigned(bool(ic_fifo_dbg.full_o_out())),
                        unsigned(bool(ic_fifo_dbg.empty_o_out())),
                        (unsigned long long)uint64_t(ic_fifo_dbg.status_cnt_q),
                        (unsigned long long)uint64_t(ic_fifo_dbg.status_cnt_n_comb_func()),
                        (unsigned long long)uint64_t(ic_fifo_dbg.read_pointer_q),
                        (unsigned long long)uint64_t(ic_fifo_dbg.read_pointer_n_comb_func()),
                        (unsigned long long)uint64_t(ic_fifo_dbg.write_pointer_q),
                        (unsigned long long)uint64_t(ic_fifo_dbg.write_pointer_n_comb_func()),
                        (unsigned long long)uint64_t(ic_fifo_dbg.data_i_in().paddr),
                        (unsigned long long)uint64_t(ic_fifo_dbg.data_o_out().paddr),
                        (unsigned long long)uint64_t(ic_fifo_dbg.mem_q[(unsigned)0].paddr),
                        (unsigned long long)uint64_t(ic_fifo_dbg.mem_q[(unsigned)1].paddr),
                        (unsigned long long)uint64_t(ic_fifo_dbg.mem_n_comb_func()[(unsigned)0].paddr),
                        (unsigned long long)uint64_t(ic_fifo_dbg.mem_n_comb_func()[(unsigned)1].paddr),
                        unsigned(bool(ic_fifo_dbg.gate_clock_comb_func())));
            std::printf("DBGMMU cycle=%llu en_s=%u en_g=%u hit=%u is_page=0x%llx pte_ppn=0x%llx pte_v=%u pte_r=%u pte_w=%u pte_x=%u pte_u=%u pte_g=%u pte_a=%u pte_d=%u gpte_ppn=0x%llx\n",
                        (unsigned long long)cycle,
                        unsigned(bool(mmu.enable_translation_i_in())),
                        unsigned(bool(mmu.enable_g_translation_i_in())),
                        unsigned(bool(itlb_hit)),
                        (unsigned long long)uint64_t(itlb_is_page),
                        (unsigned long long)uint64_t(itlb_content.ppn),
                        unsigned(bool(itlb_content.v)),
                        unsigned(bool(itlb_content.r)),
                        unsigned(bool(itlb_content.w)),
                        unsigned(bool(itlb_content.x)),
                        unsigned(bool(itlb_content.u)),
                        unsigned(bool(itlb_content.g)),
                        unsigned(bool(itlb_content.a)),
                        unsigned(bool(itlb_content.d)),
                        (unsigned long long)uint64_t(itlb_g_content.ppn));
            auto fu_data_id_ex = dut.i_cva6.fu_data_id_ex_comb_func();
            auto wt_valid = dut.i_cva6.wt_valid_ex_id_comb_func();
            auto trans_id = dut.i_cva6.trans_id_ex_id_comb_func();
            auto wbdata = dut.i_cva6.wbdata_ex_id_comb_func();
            auto commit_instr = dut.i_cva6.commit_instr_id_commit_comb_func();
            auto commit_ack = dut.i_cva6.commit_ack_commit_id_comb_func();
            auto lsu_req = lsu.lsu_req_i_comb_func();
            const unsigned flu_tid_dbg = unsigned(uint64_t(trans_id[ariane_pkg::FLU_WB]) & 0xffu);
            if (cycle >= 292 && cycle <= 296) {
                auto ex_fu_in = ex.fu_data_i_in();
                auto ex_one = ex.one_cycle_data_comb_func();
                auto sb_wt = issue.i_scoreboard.wt_valid_i_in();
                auto sb_tid = issue.i_scoreboard.trans_id_i_in();
                std::printf("DBGEX cycle=%llu parent_tid=%llu ex_in_tid=%llu one_tid=%llu flu_comb_tid=%llu flu_out_tid=%llu parent_bridge_tid=%llu trans_vec_tid=%llu sb_tid=%llu ex_valid=%u flu_valid_comb=%u flu_valid_out=%u wt_vec=0x%llx sb_wt=0x%llx one_sel=0x%llx alu_i=0x%llx csr_i=0x%llx mult_i=0x%llx csr_ready=%u csr_valid_in=%u csr_result=0x%llx\n",
                            (unsigned long long)cycle,
                            (unsigned long long)uint64_t(fu_data_id_ex[0].trans_id),
                            (unsigned long long)uint64_t(ex_fu_in[0].trans_id),
                            (unsigned long long)uint64_t(ex_one.trans_id),
                            (unsigned long long)uint64_t(ex.flu_trans_id_o_comb_func()),
                            (unsigned long long)uint64_t(ex.flu_trans_id_o_out()),
                            (unsigned long long)uint64_t(dut.i_cva6.flu_trans_id_ex_id_comb_func()),
                            (unsigned long long)uint64_t(trans_id[ariane_pkg::FLU_WB]),
                            (unsigned long long)uint64_t(sb_tid[ariane_pkg::FLU_WB]),
                            unsigned(bool(ex.v_i_in())),
                            unsigned(bool(ex.flu_valid_o_comb_func())),
                            unsigned(bool(ex.flu_valid_o_out())),
                            (unsigned long long)uint64_t(wt_valid),
                            (unsigned long long)uint64_t(sb_wt),
                            (unsigned long long)uint64_t(ex.one_cycle_select_comb_func()),
                            (unsigned long long)uint64_t(ex.alu_valid_i_in()),
                            (unsigned long long)uint64_t(ex.csr_valid_i_in()),
                            (unsigned long long)uint64_t(ex.mult_valid_i_in()),
                            unsigned(bool(ex.csr_ready_comb_func())),
                            unsigned(bool(ex.csr_buffer_i.csr_valid_i_in())),
                            (unsigned long long)uint64_t(ex.csr_result_comb_func()));
            }
            if (cycle >= 268 && cycle < 290) {
                auto& sb_dbg = issue.i_scoreboard;
                std::printf("DBGSBX cycle=%llu ip=%llu cp=%llu full=%u dec_v=0x%llx sb_ack=0x%llx iro_ack=0x%llx wt=0x%llx tids=%llu,%llu,%llu,%llu wb0=0x%llx commit_ack=0x%llx commit_v=%u commit_tid=%llu alu_q=0x%llx alu_n=0x%llx iro_issue_v=0x%llx iro_fu=%llu iro_tid=%llu ex_flu_v=%u ex_flu_tid=%llu ex_alu_sel=0x%llx one_cycle=0x%llx\n",
                            (unsigned long long)cycle,
                            (unsigned long long)uint64_t(sb_dbg.issue_pointer_q),
                            (unsigned long long)uint64_t(sb_dbg.commit_pointer_q[0]),
                            unsigned(bool(issue.sb_full_o_out())),
                            (unsigned long long)uint64_t(issue.i_scoreboard.decoded_instr_valid_i_in()),
                            (unsigned long long)uint64_t(issue.i_scoreboard.decoded_instr_ack_o_out()),
                            (unsigned long long)uint64_t(iro.issue_ack_o_out()),
                            (unsigned long long)uint64_t(wt_valid),
                            (unsigned long long)uint64_t(trans_id[0]),
                            (unsigned long long)uint64_t(trans_id[1]),
                            (unsigned long long)uint64_t(trans_id[2]),
                            (unsigned long long)uint64_t(trans_id[3]),
                            (unsigned long long)uint64_t(wbdata[0]),
                            (unsigned long long)uint64_t(commit_ack),
                            unsigned(bool(commit_instr[0].valid)),
                            (unsigned long long)uint64_t(commit_instr[0].trans_id),
                            (unsigned long long)uint64_t(iro.alu_valid_q),
                            (unsigned long long)uint64_t(iro.alu_valid_n_comb_func()),
                            (unsigned long long)uint64_t(iro.issue_instr_valid_i_in()),
                            (unsigned long long)uint64_t(iro.fu_data_n_comb_func()[0].fu),
                            (unsigned long long)uint64_t(iro.fu_data_n_comb_func()[0].trans_id),
                            unsigned(bool(ex.flu_valid_o_out())),
                            (unsigned long long)uint64_t(ex.flu_trans_id_o_out()),
                            (unsigned long long)uint64_t(ex.alu_valid_i_in()),
                            (unsigned long long)uint64_t(ex.one_cycle_select_comb_func()));
                for (unsigned sbi = 0; sbi < 8; ++sbi) {
                    const auto& e = sb_dbg.mem_q[sbi];
                    std::printf("DBGSBE cycle=%llu i=%u issued=%u valid=%u rd=%llu fu=%llu op=%llu pc=0x%llx result=0x%llx cancel=%u\n",
                                (unsigned long long)cycle,
                                sbi,
                                unsigned(bool(e.issued)),
                                unsigned(bool(e.sbe.valid)),
                                (unsigned long long)uint64_t(e.sbe.rd),
                                (unsigned long long)uint64_t(e.sbe.fu),
                                (unsigned long long)uint64_t(e.sbe.op),
                                (unsigned long long)uint64_t(e.sbe.pc),
                                (unsigned long long)uint64_t(e.sbe.result),
                                unsigned(bool(e.cancelled)));
                }
            }
            auto& realign_probe = dut.i_cva6.i_frontend.i_instr_realign;
            const auto& cache_rsp_dbg = cache.dreq_o_out();
            const auto& subsys_rsp_dbg = dut.i_cva6.i_cache_subsystem.icache_dreq_o_out();
            const auto& front_rsp_dbg = dut.i_cva6.i_frontend.icache_dreq_i_in();
            const auto parent_rsp_dbg = dut.i_cva6.icache_dreq_cache_if_comb;
            std::printf("DBGDATA cycle=%llu axi_r_valid=%u axi_r_data=0x%llx shim_data=0x%llx shift0=0x%llx shift1=0x%llx rtrn_vld=%u rtrn_data_lo=0x%llx cache_rtrn_lo=0x%llx cache_valid=%u subsys_valid=%u parent_valid=%u front_valid=%u cache_vaddr=0x%llx subsys_vaddr=0x%llx parent_vaddr=0x%llx front_vaddr=0x%llx cache_data=0x%llx subsys_data=0x%llx parent_data=0x%llx front_data=0x%llx fe_ic_q_valid=%u fe_ic_q_data=0x%llx real_valid_i=%u real_data_i=0x%llx real_valid_o=0x%llx real_instr0=0x%llx real_instr1=0x%llx fetch_instr=0x%llx\n",
                        (unsigned long long)cycle,
                        unsigned(bool(axi_resp.r_valid)),
                        (unsigned long long)uint64_t(axi_resp.r.data),
                        (unsigned long long)uint64_t(adapter.i_axi_shim.rd_data_o_out()),
                        (unsigned long long)uint64_t(adapter.icache_rd_shift_q[0]),
                        (unsigned long long)uint64_t(adapter.icache_rd_shift_q[1]),
                        unsigned(bool(adapter.icache_rtrn_vld_o_out())),
                        (unsigned long long)uint64_t(adapter.icache_rtrn_o_out().data),
                        (unsigned long long)uint64_t(cache.mem_rtrn_i_in().data),
                        unsigned(bool(cache_rsp_dbg.valid)),
                        unsigned(bool(subsys_rsp_dbg.valid)),
                        unsigned(bool(parent_rsp_dbg.valid)),
                        unsigned(bool(front_rsp_dbg.valid)),
                        (unsigned long long)uint64_t(cache_rsp_dbg.vaddr),
                        (unsigned long long)uint64_t(subsys_rsp_dbg.vaddr),
                        (unsigned long long)uint64_t(parent_rsp_dbg.vaddr),
                        (unsigned long long)uint64_t(front_rsp_dbg.vaddr),
                        (unsigned long long)uint64_t(cache_rsp_dbg.data),
                        (unsigned long long)uint64_t(subsys_rsp_dbg.data),
                        (unsigned long long)uint64_t(parent_rsp_dbg.data),
                        (unsigned long long)uint64_t(front_rsp_dbg.data),
                        unsigned(bool(dut.i_cva6.i_frontend.icache_valid_q)),
                        (unsigned long long)uint64_t(dut.i_cva6.i_frontend.icache_data_q),
                        unsigned(bool(realign_probe.valid_i_in())),
                        (unsigned long long)uint64_t(realign_probe.data_i_in()),
                        (unsigned long long)uint64_t(realign_probe.valid_o_out()),
                        (unsigned long long)uint64_t(realign_probe.instr_o_out()[0]),
                        (unsigned long long)uint64_t(realign_probe.instr_o_out()[1]),
                        (unsigned long long)uint64_t(id.fetch_entry_i_in()[0].instruction));
            if (cycle >= 268 && cycle < 282) {
                const auto& cache_comb_rsp_dbg = cache.dreq_o_comb_func();
                std::printf("DBGICCOND cycle=%llu state=%llu state_d=%llu mem_vld=%u rtype=%llu kill_s1=%u kill_s2=%u flush_i=%u flush_q=%u flush_d=%u comb_valid=%u out_valid=%u req=%u spec=%u areq_valid=%u ex_valid=%u inv_q=%u cmp_en_q=%u cl_hit=%u\n",
                            (unsigned long long)cycle,
                            (unsigned long long)uint64_t(cache.state_q),
                            (unsigned long long)uint64_t(cache.state_d_comb_func()),
                            unsigned(bool(cache.mem_rtrn_vld_i_in())),
                            (unsigned long long)uint64_t(cache.mem_rtrn_i_in().rtype),
                            unsigned(bool(cache.dreq_i_in().kill_s1)),
                            unsigned(bool(cache.dreq_i_in().kill_s2)),
                            unsigned(bool(cache.flush_i_in())),
                            unsigned(bool(cache.flush_q)),
                            unsigned(bool(cache.flush_d_comb_func())),
                            unsigned(bool(cache_comb_rsp_dbg.valid)),
                            unsigned(bool(cache_rsp_dbg.valid)),
                            unsigned(bool(cache.dreq_i_in().req)),
                            unsigned(bool(cache.dreq_i_in().spec)),
                            unsigned(bool(cache.areq_i_in().fetch_valid)),
                            unsigned(bool(cache.areq_i_in().fetch_exception.valid)),
                            unsigned(bool(cache.inv_q)),
                            unsigned(bool(cache.cmp_en_q)),
                            unsigned(bool(cache.cl_hit_comb_func())));
            }
            if (cycle >= 8 && uint64_t(id.fetch_entry_valid_i_in()) != 0) {
            auto dec0 = id.decoder_i[0].instruction_o_out();
            auto id_out0 = id.issue_entry_o_out()[0];
            auto sb_in0 = issue.i_scoreboard.decoded_instr_i_in()[0];
            auto sb_out0 = issue.i_scoreboard.issue_instr_o_out()[0];
            auto& realign_dbg = dut.i_cva6.i_frontend.i_instr_realign;
            std::printf("DBGREALIGN cycle=%llu fe_ic_q_valid=%u fe_ic_q_data=0x%llx real_valid_i=%u real_data_i=0x%llx real_valid_o=0x%llx real_instr0=0x%llx real_instr1=0x%llx real_addr0=0x%llx unalign_q=%u unalign_d=%u\n",
                        (unsigned long long)cycle,
                        unsigned(bool(dut.i_cva6.i_frontend.icache_valid_q)),
                        (unsigned long long)uint64_t(dut.i_cva6.i_frontend.icache_data_q),
                        unsigned(bool(realign_dbg.valid_i_in())),
                        (unsigned long long)uint64_t(realign_dbg.data_i_in()),
                        (unsigned long long)uint64_t(realign_dbg.valid_o_out()),
                        (unsigned long long)uint64_t(realign_dbg.instr_o_out()[0]),
                        (unsigned long long)uint64_t(realign_dbg.instr_o_out()[1]),
                        (unsigned long long)uint64_t(realign_dbg.addr_o_out()[0]),
                        unsigned(bool(realign_dbg.unaligned_q)),
                        unsigned(bool(realign_dbg.unaligned_d_comb_func())));
            auto& iq_dbg = dut.i_cva6.i_frontend.i_instr_queue;
            std::printf("DBGIQ cycle=%llu pc_q=0x%llx pc_d=0x%llx feaddr0=0x%llx reset_q=%u reset_d=%u idx_q=0x%llx idx_d=0x%llx idx_is=0x%llx idx_is_d=0x%llx shamt=0x%llx popcnt=0x%llx idx0=0x%llx idx1=0x%llx empty=0x%llx full=0x%llx push=0x%llx push_fifo=0x%llx pop=0x%llx valid=0x%llx instr_i0=0x%llx instr_i1=0x%llx instr_i2=0x%llx instr_i3=0x%llx in0=0x%llx in1=0x%llx out0=0x%llx out1=0x%llx out2=0x%llx out3=0x%llx fe0=0x%llx fe1=0x%llx fev=0x%llx fer=0x%llx\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(iq_dbg.pc_q),
                        (unsigned long long)uint64_t(iq_dbg.pc_d_comb_func()),
                        (unsigned long long)uint64_t(iq_dbg.fetch_entry_o_out()[0].address),
                        unsigned(bool(iq_dbg.reset_address_q)),
                        unsigned(bool(iq_dbg.reset_address_d_comb_func())),
                        (unsigned long long)uint64_t(iq_dbg.idx_ds_q),
                        (unsigned long long)uint64_t(iq_dbg.idx_ds_d_comb_func()),
                        (unsigned long long)uint64_t(iq_dbg.idx_is_q),
                        (unsigned long long)uint64_t(iq_dbg.idx_is_d_comb_func()),
                        (unsigned long long)uint64_t(iq_dbg.shamt_comb_func()),
                        (unsigned long long)uint64_t(iq_dbg.popcount_comb_func()),
                        (unsigned long long)uint64_t(iq_dbg.idx_ds_comb_func()[0]),
                        (unsigned long long)uint64_t(iq_dbg.idx_ds_comb_func()[1]),
                        (unsigned long long)uint64_t(iq_dbg.instr_queue_empty_comb_func()),
                        (unsigned long long)uint64_t(iq_dbg.instr_queue_full_comb_func()),
                        (unsigned long long)uint64_t(iq_dbg.push_instr_comb_func()),
                        (unsigned long long)uint64_t(iq_dbg.push_instr_fifo_comb_func()),
                        (unsigned long long)uint64_t(iq_dbg.pop_instr_comb_func()),
                        (unsigned long long)uint64_t(iq_dbg.valid_comb_func()),
                        (unsigned long long)uint64_t(iq_dbg.instr_comb_func()[0]),
                        (unsigned long long)uint64_t(iq_dbg.instr_comb_func()[1]),
                        (unsigned long long)uint64_t(iq_dbg.instr_comb_func()[2]),
                        (unsigned long long)uint64_t(iq_dbg.instr_comb_func()[3]),
                        (unsigned long long)uint64_t(iq_dbg.instr_data_in_comb_func()[0].instr),
                        (unsigned long long)uint64_t(iq_dbg.instr_data_in_comb_func()[1].instr),
                        (unsigned long long)uint64_t(iq_dbg.instr_data_out_comb_func()[0].instr),
                        (unsigned long long)uint64_t(iq_dbg.instr_data_out_comb_func()[1].instr),
                        (unsigned long long)uint64_t(iq_dbg.instr_data_out_comb_func()[2].instr),
                        (unsigned long long)uint64_t(iq_dbg.instr_data_out_comb_func()[3].instr),
                        (unsigned long long)uint64_t(iq_dbg.fetch_entry_o_out()[0].instruction),
                        (unsigned long long)uint64_t(iq_dbg.fetch_entry_o_out()[1].instruction),
                        (unsigned long long)uint64_t(iq_dbg.fetch_entry_valid_o_out()),
                        (unsigned long long)uint64_t(iq_dbg.fetch_entry_ready_i_in()));
            auto& addr_fifo_dbg = iq_dbg.i_fifo_address;
            fe.predict_address_comb_func();
            std::printf("DBGBP cycle=%llu fe_pred=0x%llx fe_cf0=%llu fe_cf1=%llu iq_pred_in=0x%llx iq_cf_in0=%llu iq_cf_in1=%llu in_cf0=%llu in_cf1=%llu out_cf0=%llu out_cf1=%llu fe_cf=%llu fe_pred_out=0x%llx is_cf=0x%llx push_addr=%u pop_addr=%u addr_out=0x%llx fifo_push=%u fifo_pop=%u fifo_empty=%u fifo_full=%u fifo_status=%llu fifo_rptr=%llu fifo_wptr=%llu fifo_data_i=0x%llx fifo_data_o=0x%llx fifo_mem0=0x%llx fifo_mem1=0x%llx fifo_next0=0x%llx fifo_next1=0x%llx\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(fe.predict_address_comb),
                        (unsigned long long)uint64_t(fe.cf_type_comb_func()[0]),
                        (unsigned long long)uint64_t(fe.cf_type_comb_func()[1]),
                        (unsigned long long)uint64_t(iq_dbg.predict_address_i_in()),
                        (unsigned long long)uint64_t(iq_dbg.cf_type_i_in()[0]),
                        (unsigned long long)uint64_t(iq_dbg.cf_type_i_in()[1]),
                        (unsigned long long)uint64_t(iq_dbg.instr_data_in_comb_func()[0].cf),
                        (unsigned long long)uint64_t(iq_dbg.instr_data_in_comb_func()[1].cf),
                        (unsigned long long)uint64_t(iq_dbg.instr_data_out_comb_func()[0].cf),
                        (unsigned long long)uint64_t(iq_dbg.instr_data_out_comb_func()[1].cf),
                        (unsigned long long)uint64_t(iq_dbg.fetch_entry_o_out()[0].branch_predict.cf),
                        (unsigned long long)uint64_t(iq_dbg.fetch_entry_o_out()[0].branch_predict.predict_address),
                        (unsigned long long)uint64_t(iq_dbg.fetch_entry_is_cf_comb_func()),
                        unsigned(bool(iq_dbg.push_address_comb_func())),
                        unsigned(bool(iq_dbg.pop_address_comb_func())),
                        (unsigned long long)uint64_t(iq_dbg.address_out_comb_func()),
                        unsigned(bool(addr_fifo_dbg.push_i_in())),
                        unsigned(bool(addr_fifo_dbg.pop_i_in())),
                        unsigned(bool(addr_fifo_dbg.empty_o_out())),
                        unsigned(bool(addr_fifo_dbg.full_o_out())),
                        (unsigned long long)uint64_t(addr_fifo_dbg.status_cnt_q),
                        (unsigned long long)uint64_t(addr_fifo_dbg.read_pointer_q),
                        (unsigned long long)uint64_t(addr_fifo_dbg.write_pointer_q),
                        (unsigned long long)uint64_t(addr_fifo_dbg.data_i_in()),
                        (unsigned long long)uint64_t(addr_fifo_dbg.data_o_out()),
                        (unsigned long long)uint64_t(addr_fifo_dbg.mem_q[0]),
                        (unsigned long long)uint64_t(addr_fifo_dbg.mem_q[1]),
                        (unsigned long long)uint64_t(addr_fifo_dbg.mem_n_comb_func()[0]),
                        (unsigned long long)uint64_t(addr_fifo_dbg.mem_n_comb_func()[1]));
            std::printf("DBGID cycle=%llu dec_fu=%llu dec_op=%llu dec_valid=%u dec_pc=0x%llx dec_instr=0x%llx id_dec_fu=%llu id_dec_op=%llu id_q_fu=%llu id_q_op=%llu id_q_valid=%u id_out_fu=%llu id_out_op=%llu sb_in_fu=%llu sb_in_op=%llu sb_out_fu=%llu sb_out_op=%llu sb_out_valid=%u\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(dec0.fu),
                        (unsigned long long)uint64_t(dec0.op),
                        unsigned(bool(dec0.valid)),
                        (unsigned long long)uint64_t(dec0.pc),
                        (unsigned long long)uint64_t(id.decoder_i[0].instruction_i_in()),
                        (unsigned long long)uint64_t(id.decoded_instruction_comb_func()[0].fu),
                        (unsigned long long)uint64_t(id.decoded_instruction_comb_func()[0].op),
                        (unsigned long long)uint64_t(id.issue_q[0].sbe.fu),
                        (unsigned long long)uint64_t(id.issue_q[0].sbe.op),
                        unsigned(bool(id.issue_q[0].valid)),
                        (unsigned long long)uint64_t(id_out0.fu),
                        (unsigned long long)uint64_t(id_out0.op),
                        (unsigned long long)uint64_t(sb_in0.fu),
                        (unsigned long long)uint64_t(sb_in0.op),
                        (unsigned long long)uint64_t(sb_out0.fu),
                        (unsigned long long)uint64_t(sb_out0.op),
                        unsigned(bool(issue.i_scoreboard.issue_instr_valid_o_out()[0])));
            std::printf("DBGIDN cycle=%llu dec_valid_bits=0x%llx stall_fetch=0x%llx fe_valid=0x%llx fe_ready_comb=0x%llx ack=0x%llx flush=%u zcmt0=%u stall_zcmt=%u illegal_cvxif=%u stall_macro=%u\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(id.decoded_instruction_valid_comb_func()),
                        (unsigned long long)uint64_t(id.stall_instr_fetch_comb_func()),
                        (unsigned long long)uint64_t(id.fetch_entry_valid_i_in()),
                        (unsigned long long)uint64_t(id.fetch_entry_ready_o_out()),
                        (unsigned long long)uint64_t(id.issue_instr_ack_i_in()),
                        unsigned(bool(id.flush_i_in())),
                        unsigned(bool(id.is_zcmt_instr_comb_func()[0])),
                        unsigned(bool(id.stall_macro_deco_zcmt_comb_func())),
                        unsigned(bool(id.is_illegal_cvxif_i_comb_func())),
                        unsigned(bool(id.stall_macro_deco_comb_func())));
            if (cycle >= 444 && cycle <= 449) {
                auto dec_out_dbg = id.decoder_i[0].instruction_o_out();
                auto sb_dec_dbg = issue.i_scoreboard.decoded_instr_i_in()[0];
                auto sb_issue_dbg = issue.i_scoreboard.issue_instr_o_out()[0];
                std::printf("DBGDECEX cycle=%llu dec_ex=%u dec_cause=%llu dec_valid=%u dec_pc=0x%llx dec_fu=%llu dec_op=%llu dec_is_illegal_i=%u is_illegal_deco=0x%llx sb_dec_ex=%u sb_issue_ex=%u sb_issue_valid=%u sb_issue_pc=0x%llx\n",
                            (unsigned long long)cycle,
                            unsigned(bool(dec_out_dbg.ex.valid)),
                            (unsigned long long)uint64_t(dec_out_dbg.ex.cause),
                            unsigned(bool(dec_out_dbg.valid)),
                            (unsigned long long)uint64_t(dec_out_dbg.pc),
                            (unsigned long long)uint64_t(dec_out_dbg.fu),
                            (unsigned long long)uint64_t(dec_out_dbg.op),
                            unsigned(bool(id.decoder_i[0].is_illegal_i_in())),
                            (unsigned long long)uint64_t(id.is_illegal_deco_comb_func()),
                            unsigned(bool(sb_dec_dbg.ex.valid)),
                            unsigned(bool(sb_issue_dbg.ex.valid)),
                            unsigned(bool(sb_issue_dbg.valid)),
                            (unsigned long long)uint64_t(sb_issue_dbg.pc));
            }
            const auto resolved_dbg = dut.i_cva6.resolved_branch_comb_func();
            const auto& icreq_dbg = dut.i_cva6.i_frontend.icache_dreq_o_out();
            std::printf("DBGBR cycle=%llu rb_valid=%u rb_mis=%u rb_taken=%u rb_cf=%llu rb_pc=0x%llx rb_target=0x%llx ctrl_if=%u ctrl_id=%u ctrl_unissued=%u ctrl_ex=%u ctrl_setpc=%u fe_flush=%u fe_mis=%u fe_kill_s1=%u fe_kill_s2=%u fe_npc_q=0x%llx fe_npc_d=0x%llx fe_replay=%u ex_branch_valid=0x%llx ex_res_valid=%u ex_res_mis=%u ex_res_target=0x%llx br_q=0x%llx br_n=0x%llx\n",
                        (unsigned long long)cycle,
                        unsigned(bool(resolved_dbg.valid)),
                        unsigned(bool(resolved_dbg.is_mispredict)),
                        unsigned(bool(resolved_dbg.is_taken)),
                        (unsigned long long)uint64_t(resolved_dbg.cf_type),
                        (unsigned long long)uint64_t(resolved_dbg.pc),
                        (unsigned long long)uint64_t(resolved_dbg.target_address),
                        unsigned(bool(dut.i_cva6.flush_ctrl_if_comb_func())),
                        unsigned(bool(dut.i_cva6.flush_ctrl_id_comb_func())),
                        unsigned(bool(dut.i_cva6.flush_unissued_instr_ctrl_id_comb_func())),
                        unsigned(bool(dut.i_cva6.flush_ctrl_ex_comb_func())),
                        unsigned(bool(dut.i_cva6.set_pc_ctrl_pcgen_comb_func())),
                        unsigned(bool(fe.flush_i_in())),
                        unsigned(bool(fe.is_mispredict_comb_func())),
                        unsigned(bool(icreq_dbg.kill_s1)),
                        unsigned(bool(icreq_dbg.kill_s2)),
                        (unsigned long long)uint64_t(fe.npc_q),
                        (unsigned long long)uint64_t(fe.npc_d_comb_func()),
                        unsigned(bool(fe.replay_comb_func())),
                        (unsigned long long)uint64_t(ex.branch_valid_i_in()),
                        unsigned(bool(ex.resolved_branch_o_out().valid)),
                        unsigned(bool(ex.resolved_branch_o_out().is_mispredict)),
                        (unsigned long long)uint64_t(ex.resolved_branch_o_out().target_address),
                        (unsigned long long)uint64_t(iro.branch_valid_q),
                        (unsigned long long)uint64_t(iro.branch_valid_n_comb_func()));
            if (cycle >= 438 && cycle <= 458) {
                auto ex_commit_dbg = dut.i_cva6.ex_commit_comb_func();
                std::printf("DBGPCSEL cycle=%llu fe_ex_valid=%u fe_eret=%u fe_flush=%u fe_setpc=%u fe_replay=%u fe_mis=%u trap=0x%llx epc=0x%llx ex_commit_valid=%u ex_commit_cause=%llu ex_commit_tval=0x%llx npc_q=0x%llx npc_d=0x%llx\n",
                            (unsigned long long)cycle,
                            unsigned(bool(fe.ex_valid_i_in())),
                            unsigned(bool(fe.eret_i_in())),
                            unsigned(bool(fe.flush_i_in())),
                            unsigned(bool(fe.set_pc_commit_i_in())),
                            unsigned(bool(fe.replay_comb_func())),
                            unsigned(bool(fe.is_mispredict_comb_func())),
                            (unsigned long long)uint64_t(fe.trap_vector_base_i_in()),
                            (unsigned long long)uint64_t(fe.epc_i_in()),
                            unsigned(bool(ex_commit_dbg.valid)),
                            (unsigned long long)uint64_t(ex_commit_dbg.cause),
                            (unsigned long long)uint64_t(ex_commit_dbg.tval),
                            (unsigned long long)uint64_t(fe.npc_q),
                            (unsigned long long)uint64_t(fe.npc_d_comb_func()));
            }
            auto csr_ex_dbg = dut.i_cva6.csr_exception_csr_commit_comb_func();
            std::printf("DBG2 cycle=%llu idex alu=0x%llx br=0x%llx csr=0x%llx mult=0x%llx lsu=0x%llx fu0=%llu op0=%llu tid0=%llu rs1=0x%llx rs2=0x%llx ex_flu_v=%u ex_ld_v=%u ex_st_v=%u csr_ready=%u mult_ready=%u csr_commit=%u csr_ex_valid=%u csr_ex_cause=%llu csr_addr=0x%llx csr_op=%llu wt=0x%llx tid_flu=%llu tid_st=%llu tid_ld=%llu wb_flu=0x%llx wb_st=0x%llx wb_ld=0x%llx commit_valid=%u commit_pc=0x%llx commit_fu=%llu commit_op=%llu commit_tid=%llu commit_rd=%llu commit_result=0x%llx commit_ack=0x%llx we=0x%llx waddr0=%llu wdata0=0x%llx lsu_req_valid=%u lsu_req_fu=%llu lsu_req_op=%llu lsu_req_tid=%llu lsu_req_vaddr=0x%llx lsu_req_data=0x%llx lsu_ready=%u lsu_ld_valid=%u lsu_st_valid=%u\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(dut.i_cva6.alu_valid_id_ex_comb_func()),
                        (unsigned long long)uint64_t(dut.i_cva6.branch_valid_id_ex_comb_func()),
                        (unsigned long long)uint64_t(dut.i_cva6.csr_valid_id_ex_comb_func()),
                        (unsigned long long)uint64_t(dut.i_cva6.mult_valid_id_ex_comb_func()),
                        (unsigned long long)uint64_t(dut.i_cva6.lsu_valid_id_ex_comb_func()),
                        (unsigned long long)uint64_t(fu_data_id_ex[0].fu),
                        (unsigned long long)uint64_t(fu_data_id_ex[0].operation),
                        (unsigned long long)uint64_t(fu_data_id_ex[0].trans_id),
                        (unsigned long long)uint64_t(fu_data_id_ex[0].operand_a),
                        (unsigned long long)uint64_t(fu_data_id_ex[0].operand_b),
                        unsigned(bool(ex.flu_valid_o_out())),
                        unsigned(bool(ex.load_valid_o_out())),
                        unsigned(bool(ex.store_valid_o_out())),
                        unsigned(bool(ex.csr_ready_comb_func())),
                        unsigned(bool(ex.mult_ready_comb_func())),
                        unsigned(bool(dut.i_cva6.csr_commit_commit_ex_comb_func())),
                        unsigned(bool(csr_ex_dbg.valid)),
                        (unsigned long long)uint64_t(csr_ex_dbg.cause),
                        (unsigned long long)uint64_t(dut.i_cva6.csr_addr_ex_csr_comb_func()),
                        (unsigned long long)uint64_t(dut.i_cva6.csr_op_commit_csr_comb_func()),
                        (unsigned long long)uint64_t(wt_valid),
                        (unsigned long long)uint64_t(trans_id[ariane_pkg::FLU_WB]),
                        (unsigned long long)uint64_t(trans_id[ariane_pkg::STORE_WB]),
                        (unsigned long long)uint64_t(trans_id[ariane_pkg::LOAD_WB]),
                        (unsigned long long)uint64_t(wbdata[ariane_pkg::FLU_WB]),
                        (unsigned long long)uint64_t(wbdata[ariane_pkg::STORE_WB]),
                        (unsigned long long)uint64_t(wbdata[ariane_pkg::LOAD_WB]),
                        unsigned(bool(commit_instr[0].valid)),
                        (unsigned long long)uint64_t(commit_instr[0].pc),
                        (unsigned long long)uint64_t(commit_instr[0].fu),
                        (unsigned long long)uint64_t(commit_instr[0].op),
                        (unsigned long long)uint64_t(commit_instr[0].trans_id),
                        (unsigned long long)uint64_t(commit_instr[0].rd),
                        (unsigned long long)uint64_t(commit_instr[0].result),
                        (unsigned long long)uint64_t(commit_ack),
                        (unsigned long long)uint64_t(dut.i_cva6.we_gpr_commit_id_comb_func()),
                        (unsigned long long)uint64_t(dut.i_cva6.waddr_commit_id_comb_func()[0]),
                        (unsigned long long)uint64_t(dut.i_cva6.wdata_commit_id_comb_func()[0]),
                        unsigned(bool(lsu_req.valid)),
                        (unsigned long long)uint64_t(lsu_req.fu),
                        (unsigned long long)uint64_t(lsu_req.operation),
                        (unsigned long long)uint64_t(lsu_req.trans_id),
                        (unsigned long long)uint64_t(lsu_req.vaddr),
                        (unsigned long long)uint64_t(lsu_req.data),
                        unsigned(bool(ex.lsu_ready_o_out())),
                        unsigned(bool(lsu.load_valid_o_out())),
                        unsigned(bool(lsu.store_valid_o_out())));
            if (cycle >= 488 && cycle < 560) {
                auto& su_dbg = lsu.i_store_unit;
                auto& sbuf_dbg = su_dbg.store_buffer_i;
                const auto st_req_o = sbuf_dbg.req_port_o_out();
                const auto st_req_i = sbuf_dbg.req_port_i_in();
                std::printf("DBGSTBUF cycle=%llu lsu_commit_i=%u lsu_commit_ready=%u no_st=%u st_valid_i=%u st_valid_o=%u st_ready=%u pop=%u su_state=%llu sb_ready=%u sb_valid_i=%u sb_req=%u sb_we=%u sb_gnt=%u sb_rvalid=%u sb_addr_idx=0x%llx sb_addr_tag=0x%llx sb_wdata=0x%llx sb_be=0x%llx spec_cnt=%llu commit_cnt=%llu spec_r=%llu spec_w=%llu commit_r=%llu commit_w=%llu cq0_v=%u cq0_wait=%u cq0_addr=0x%llx cq1_v=%u cq1_wait=%u cq1_addr=0x%llx sq0_v=%u sq0_addr=0x%llx sq1_v=%u sq1_addr=0x%llx\n",
                            (unsigned long long)cycle,
                            unsigned(bool(lsu.commit_i_in())),
                            unsigned(bool(lsu.commit_ready_o_out())),
                            unsigned(bool(lsu.no_st_pending_o_out())),
                            unsigned(bool(su_dbg.valid_i_in())),
                            unsigned(bool(su_dbg.valid_o_out())),
                            unsigned(bool(su_dbg.st_ready_comb_func())),
                            unsigned(bool(su_dbg.pop_st_o_out())),
                            (unsigned long long)uint64_t(su_dbg.state_q),
                            unsigned(bool(sbuf_dbg.ready_o_out())),
                            unsigned(bool(sbuf_dbg.valid_i_in())),
                            unsigned(bool(st_req_o.data_req)),
                            unsigned(bool(st_req_o.data_we)),
                            unsigned(bool(st_req_i.data_gnt)),
                            unsigned(bool(st_req_i.data_rvalid)),
                            (unsigned long long)uint64_t(st_req_o.address_index),
                            (unsigned long long)uint64_t(st_req_o.address_tag),
                            (unsigned long long)uint64_t(st_req_o.data_wdata),
                            (unsigned long long)uint64_t(st_req_o.data_be),
                            (unsigned long long)uint64_t(sbuf_dbg.speculative_status_cnt_q),
                            (unsigned long long)uint64_t(sbuf_dbg.commit_status_cnt_q),
                            (unsigned long long)uint64_t(sbuf_dbg.speculative_read_pointer_q),
                            (unsigned long long)uint64_t(sbuf_dbg.speculative_write_pointer_q),
                            (unsigned long long)uint64_t(sbuf_dbg.commit_read_pointer_q),
                            (unsigned long long)uint64_t(sbuf_dbg.commit_write_pointer_q),
                            unsigned(bool(sbuf_dbg.commit_queue_q[0].valid)),
                            unsigned(bool(sbuf_dbg.commit_queue_q[0].wait_rvalid)),
                            (unsigned long long)uint64_t(sbuf_dbg.commit_queue_q[0].address),
                            unsigned(bool(sbuf_dbg.commit_queue_q[1].valid)),
                            unsigned(bool(sbuf_dbg.commit_queue_q[1].wait_rvalid)),
                            (unsigned long long)uint64_t(sbuf_dbg.commit_queue_q[1].address),
                            unsigned(bool(sbuf_dbg.speculative_queue_q[0].valid)),
                            (unsigned long long)uint64_t(sbuf_dbg.speculative_queue_q[0].address),
                            unsigned(bool(sbuf_dbg.speculative_queue_q[1].valid)),
                            (unsigned long long)uint64_t(sbuf_dbg.speculative_queue_q[1].address));
            }
            std::printf("DBGSB cycle=%llu issue_ptr=%llu commit_ptr=%llu flu_tid=%u flu_issued=%u flu_sbe_valid=%u flu_sbe_fu=%llu flu_sbe_op=%llu flu_pc=0x%llx commit0_issued=%u commit0_sbe_valid=%u commit0_fu=%llu commit0_op=%llu commit0_pc=0x%llx\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(issue.i_scoreboard.issue_pointer_q),
                        (unsigned long long)uint64_t(issue.i_scoreboard.commit_pointer_q[0]),
                        flu_tid_dbg,
                        unsigned(bool(issue.i_scoreboard.mem_q[flu_tid_dbg].issued)),
                        unsigned(bool(issue.i_scoreboard.mem_q[flu_tid_dbg].sbe.valid)),
                        (unsigned long long)uint64_t(issue.i_scoreboard.mem_q[flu_tid_dbg].sbe.fu),
                        (unsigned long long)uint64_t(issue.i_scoreboard.mem_q[flu_tid_dbg].sbe.op),
                        (unsigned long long)uint64_t(issue.i_scoreboard.mem_q[flu_tid_dbg].sbe.pc),
                        unsigned(bool(issue.i_scoreboard.mem_q[uint64_t(issue.i_scoreboard.commit_pointer_q[0])].issued)),
                        unsigned(bool(issue.i_scoreboard.mem_q[uint64_t(issue.i_scoreboard.commit_pointer_q[0])].sbe.valid)),
                        (unsigned long long)uint64_t(issue.i_scoreboard.mem_q[uint64_t(issue.i_scoreboard.commit_pointer_q[0])].sbe.fu),
                        (unsigned long long)uint64_t(issue.i_scoreboard.mem_q[uint64_t(issue.i_scoreboard.commit_pointer_q[0])].sbe.op),
                        (unsigned long long)uint64_t(issue.i_scoreboard.mem_q[uint64_t(issue.i_scoreboard.commit_pointer_q[0])].sbe.pc));
            if ((cycle >= 280 && cycle < 310) || (cycle >= 360 && cycle < 430) || (cycle >= 430 && cycle < 560)) {
                std::printf("DBGIRO cycle=%llu issue_valid_i=0x%llx issue_ack=0x%llx branch_n=0x%llx branch_q=0x%llx alu_n=0x%llx alu_q=0x%llx alu2_n=0x%llx use_alu2=0x%llx stall_raw=0x%llx fu_n0=%llu op_n0=%llu tid_n0=%llu pc_n=0x%llx instr_fu0=%llu instr_valid0=%u instr_ack0=%u stall=%u flu_ready=%u busy=0x%llx\n",
                            (unsigned long long)cycle,
                            (unsigned long long)uint64_t(iro.issue_instr_valid_i_in()),
                            (unsigned long long)uint64_t(iro.issue_ack_o_out()),
                            (unsigned long long)uint64_t(iro.branch_valid_n_comb_func()),
                            (unsigned long long)uint64_t(iro.branch_valid_q),
                            (unsigned long long)uint64_t(iro.alu_valid_n_comb_func()),
                            (unsigned long long)uint64_t(iro.alu_valid_q),
                            (unsigned long long)uint64_t(iro.alu2_valid_n_comb_func()),
                            (unsigned long long)uint64_t(iro.use_alu2_comb_func()),
                            (unsigned long long)uint64_t(iro.stall_raw_comb_func()),
                            (unsigned long long)uint64_t(iro.fu_data_n_comb_func()[0].fu),
                            (unsigned long long)uint64_t(iro.fu_data_n_comb_func()[0].operation),
                            (unsigned long long)uint64_t(iro.fu_data_n_comb_func()[0].trans_id),
                            (unsigned long long)uint64_t(iro.pc_n_comb_func()),
                            (unsigned long long)uint64_t(iro.issue_instr_i_in()[0].fu),
                            unsigned(bool(iro.issue_instr_valid_i_in()[0])),
                            unsigned(bool(iro.issue_ack_o_out()[0])),
                            unsigned(bool(iro.stall_i_in())),
                            unsigned(bool(iro.flu_ready_i_in())),
                            (unsigned long long)uint64_t(iro.fu_busy_comb_func()));
            }
            }
            if ((cycle >= 360 && cycle < 430) || (cycle >= 480 && cycle < 560)) {
                const auto issue_n0_dbg = id.issue_n_comb_func()[0];
                const auto id_out0_dbg = id.issue_entry_o_out()[0];
                const auto sb_in0_dbg = issue.i_scoreboard.decoded_instr_i_in()[0];
                const auto sb_out0_dbg = issue.i_scoreboard.issue_instr_o_out()[0];
                const auto iro_in0_dbg = iro.issue_instr_i_in()[0];
                const auto issue_ptr_dbg = uint64_t(issue.i_scoreboard.issue_pointer_q);
                const auto issue_slot_dbg = issue.i_scoreboard.mem_q[issue_ptr_dbg];
                std::printf("DBGIROA cycle=%llu fetch_v=0x%llx fetch_r=0x%llx id_ack=0x%llx id_flush=%u id_dec_v=0x%llx id_q_v=%u id_q_fu=%llu id_q_op=%llu id_q_pc=0x%llx id_n_v=%u id_n_fu=%llu id_n_op=%llu id_n_pc=0x%llx id_out_v=0x%llx id_out_fu=%llu id_out_op=%llu id_out_pc=0x%llx sb_dec_v=0x%llx sb_ack=0x%llx sb_issue_v=0x%llx sb_full=%u sb_in_fu=%llu sb_in_op=%llu sb_in_pc=0x%llx sb_out_fu=%llu sb_out_op=%llu sb_out_pc=0x%llx sb_slot_issued=%u sb_slot_valid=%u sb_slot_fu=%llu sb_slot_op=%llu sb_slot_pc=0x%llx issue_v_sb_iro=0x%llx iro_v=0x%llx iro_ack=0x%llx iro_in_fu=%llu iro_in_op=%llu iro_in_pc=0x%llx branch_n=0x%llx branch_q=0x%llx stall_raw=0x%llx busy=0x%llx\n",
                            (unsigned long long)cycle,
                            (unsigned long long)uint64_t(id.fetch_entry_valid_i_in()),
                            (unsigned long long)uint64_t(id.fetch_entry_ready_o_out()),
                            (unsigned long long)uint64_t(id.issue_instr_ack_i_in()),
                            unsigned(bool(id.flush_i_in())),
                            (unsigned long long)uint64_t(id.decoded_instruction_valid_comb_func()),
                            unsigned(bool(id.issue_q[0].valid)),
                            (unsigned long long)uint64_t(id.issue_q[0].sbe.fu),
                            (unsigned long long)uint64_t(id.issue_q[0].sbe.op),
                            (unsigned long long)uint64_t(id.issue_q[0].sbe.pc),
                            unsigned(bool(issue_n0_dbg.valid)),
                            (unsigned long long)uint64_t(issue_n0_dbg.sbe.fu),
                            (unsigned long long)uint64_t(issue_n0_dbg.sbe.op),
                            (unsigned long long)uint64_t(issue_n0_dbg.sbe.pc),
                            (unsigned long long)uint64_t(id.issue_entry_valid_o_out()),
                            (unsigned long long)uint64_t(id_out0_dbg.fu),
                            (unsigned long long)uint64_t(id_out0_dbg.op),
                            (unsigned long long)uint64_t(id_out0_dbg.pc),
                            (unsigned long long)uint64_t(issue.i_scoreboard.decoded_instr_valid_i_in()),
                            (unsigned long long)uint64_t(issue.i_scoreboard.decoded_instr_ack_o_out()),
                            (unsigned long long)uint64_t(issue.i_scoreboard.issue_instr_valid_o_out()),
                            unsigned(bool(issue.i_scoreboard.issue_full_comb_func()[0])),
                            (unsigned long long)uint64_t(sb_in0_dbg.fu),
                            (unsigned long long)uint64_t(sb_in0_dbg.op),
                            (unsigned long long)uint64_t(sb_in0_dbg.pc),
                            (unsigned long long)uint64_t(sb_out0_dbg.fu),
                            (unsigned long long)uint64_t(sb_out0_dbg.op),
                            (unsigned long long)uint64_t(sb_out0_dbg.pc),
                            unsigned(bool(issue_slot_dbg.issued)),
                            unsigned(bool(issue_slot_dbg.sbe.valid)),
                            (unsigned long long)uint64_t(issue_slot_dbg.sbe.fu),
                            (unsigned long long)uint64_t(issue_slot_dbg.sbe.op),
                            (unsigned long long)uint64_t(issue_slot_dbg.sbe.pc),
                            (unsigned long long)uint64_t(issue.issue_instr_valid_sb_iro_comb_func()),
                            (unsigned long long)uint64_t(iro.issue_instr_valid_i_in()),
                            (unsigned long long)uint64_t(iro.issue_ack_o_out()),
                            (unsigned long long)uint64_t(iro_in0_dbg.fu),
                            (unsigned long long)uint64_t(iro_in0_dbg.op),
                            (unsigned long long)uint64_t(iro_in0_dbg.pc),
                            (unsigned long long)uint64_t(iro.branch_valid_n_comb_func()),
                            (unsigned long long)uint64_t(iro.branch_valid_q),
                            (unsigned long long)uint64_t(iro.stall_raw_comb_func()),
                            (unsigned long long)uint64_t(iro.fu_busy_comb_func()));
            }
            if (cycle >= 288 && cycle < 360) {
                auto& bu_dbg = ex.branch_unit_i;
                const auto rb_top_dbg = dut.i_cva6.resolved_branch_comb_func();
                const auto rb_ex_dbg = ex.resolved_branch_o_out();
                const auto rb_bu_dbg = bu_dbg.resolved_branch_o_out();
                const auto one_dbg = ex.one_cycle_data_comb_func();
                const auto bu_fu_dbg = bu_dbg.fu_data_i_in();
                std::printf("DBGBRA cycle=%llu top_br=0x%llx ex_br=0x%llx bu_br=%u one_sel=0x%llx ex_pc=0x%llx one_fu=%llu one_op=%llu one_imm=0x%llx one_a=0x%llx one_b=0x%llx bu_fu=%llu bu_op=%llu bu_imm=0x%llx bu_a=0x%llx bu_b=0x%llx bu_pc_i=0x%llx comp=%u alu_br=%u pred_cf=%llu pred_addr=0x%llx rb_top_v=%u rb_top_mis=%u rb_top_taken=%u rb_top_cf=%llu rb_top_pc=0x%llx rb_top_target=0x%llx rb_ex_v=%u rb_bu_v=%u rb_bu_mis=%u rb_bu_taken=%u rb_bu_cf=%llu rb_bu_pc=0x%llx rb_bu_target=0x%llx br_result=0x%llx target=0x%llx next=0x%llx resolve=%u\n",
                            (unsigned long long)cycle,
                            (unsigned long long)uint64_t(dut.i_cva6.branch_valid_id_ex_comb_func()),
                            (unsigned long long)uint64_t(ex.branch_valid_i_in()),
                            unsigned(bool(bu_dbg.branch_valid_i_in())),
                            (unsigned long long)uint64_t(ex.one_cycle_select_comb_func()),
                            (unsigned long long)uint64_t(ex.pc_i_in()),
                            (unsigned long long)uint64_t(one_dbg.fu),
                            (unsigned long long)uint64_t(one_dbg.operation),
                            (unsigned long long)uint64_t(one_dbg.imm),
                            (unsigned long long)uint64_t(one_dbg.operand_a),
                            (unsigned long long)uint64_t(one_dbg.operand_b),
                            (unsigned long long)uint64_t(bu_fu_dbg.fu),
                            (unsigned long long)uint64_t(bu_fu_dbg.operation),
                            (unsigned long long)uint64_t(bu_fu_dbg.imm),
                            (unsigned long long)uint64_t(bu_fu_dbg.operand_a),
                            (unsigned long long)uint64_t(bu_fu_dbg.operand_b),
                            (unsigned long long)uint64_t(bu_dbg.pc_i_in()),
                            unsigned(bool(bu_dbg.branch_comp_res_i_in())),
                            unsigned(bool(ex.alu_branch_res_comb_func())),
                            (unsigned long long)uint64_t(bu_dbg.branch_predict_i_in().cf),
                            (unsigned long long)uint64_t(bu_dbg.branch_predict_i_in().predict_address),
                            unsigned(bool(rb_top_dbg.valid)),
                            unsigned(bool(rb_top_dbg.is_mispredict)),
                            unsigned(bool(rb_top_dbg.is_taken)),
                            (unsigned long long)uint64_t(rb_top_dbg.cf_type),
                            (unsigned long long)uint64_t(rb_top_dbg.pc),
                            (unsigned long long)uint64_t(rb_top_dbg.target_address),
                            unsigned(bool(rb_ex_dbg.valid)),
                            unsigned(bool(rb_bu_dbg.valid)),
                            unsigned(bool(rb_bu_dbg.is_mispredict)),
                            unsigned(bool(rb_bu_dbg.is_taken)),
                            (unsigned long long)uint64_t(rb_bu_dbg.cf_type),
                            (unsigned long long)uint64_t(rb_bu_dbg.pc),
                            (unsigned long long)uint64_t(rb_bu_dbg.target_address),
                            (unsigned long long)uint64_t(bu_dbg.branch_result_o_out()),
                            (unsigned long long)uint64_t(bu_dbg.target_address_comb_func()),
                            (unsigned long long)uint64_t(bu_dbg.next_pc_comb_func()),
                            unsigned(bool(bu_dbg.resolve_branch_o_out())));
            }
            if (cycle >= 288 && cycle < 360) {
                const auto instr0_dbg = iro.issue_instr_i_in()[0];
                const auto fu_n0_dbg = iro.fu_data_n_comb_func()[0];
                const auto fwd_dbg = issue.i_scoreboard.fwd_o_out();
                const unsigned rs1_idx_dbg = unsigned(uint64_t(iro.idx_hzd_rs1_comb_func()[0]));
                const unsigned rs2_idx_dbg = unsigned(uint64_t(iro.idx_hzd_rs2_comb_func()[0]));
                const auto& sbe1_dbg = fwd_dbg.sbe[rs1_idx_dbg];
                const auto& sbe2_dbg = fwd_dbg.sbe[rs2_idx_dbg];
                std::printf("DBGRF cycle=%llu instr_v=%u fu=%llu op=%llu pc=0x%llx rs1=%llu rs2=%llu rd=%llu result=0x%llx raddr0=%llu raddr1=%llu rdata0=0x%llx rdata1=0x%llx opa_reg=0x%llx opb_reg=0x%llx raw1=%u has1=%u valid1=%u fwd1=%u idx1=%u res1=0x%llx sbe1_rd=%llu sbe1_fu=%llu sbe1_op=%llu sbe1_valid=%u sbe1_result=0x%llx issued1=%u raw2=%u has2=%u valid2=%u fwd2=%u idx2=%u res2=0x%llx sbe2_rd=%llu sbe2_valid=%u sbe2_result=0x%llx still=0x%llx fwd_valid=0x%llx fu_n_a=0x%llx fu_n_b=0x%llx fu_q_a=0x%llx fu_q_b=0x%llx wt=0x%llx tid_flu=%llu wb_flu=0x%llx we=0x%llx waddr0=%llu wdata0=0x%llx\n",
                            (unsigned long long)cycle,
                            unsigned(bool(iro.issue_instr_valid_i_in()[0])),
                            (unsigned long long)uint64_t(instr0_dbg.fu),
                            (unsigned long long)uint64_t(instr0_dbg.op),
                            (unsigned long long)uint64_t(instr0_dbg.pc),
                            (unsigned long long)uint64_t(instr0_dbg.rs1),
                            (unsigned long long)uint64_t(instr0_dbg.rs2),
                            (unsigned long long)uint64_t(instr0_dbg.rd),
                            (unsigned long long)uint64_t(instr0_dbg.result),
                            (unsigned long long)uint64_t(iro.raddr_pack_comb_func()[0]),
                            (unsigned long long)uint64_t(iro.raddr_pack_comb_func()[1]),
                            (unsigned long long)uint64_t(iro.rdata_comb_func()[0]),
                            (unsigned long long)uint64_t(iro.rdata_comb_func()[1]),
                            (unsigned long long)uint64_t(iro.operand_a_regfile_comb_func()[0]),
                            (unsigned long long)uint64_t(iro.operand_b_regfile_comb_func()[0]),
                            unsigned(bool(iro.rs1_raw_check_comb_func()[0])),
                            unsigned(bool(iro.rs1_has_raw_comb_func()[0])),
                            unsigned(bool(iro.rs1_valid_comb_func()[0])),
                            unsigned(bool(iro.forward_rs1_comb_func()[0])),
                            rs1_idx_dbg,
                            (unsigned long long)uint64_t(iro.rs1_res_comb_func()[0]),
                            (unsigned long long)uint64_t(sbe1_dbg.rd),
                            (unsigned long long)uint64_t(sbe1_dbg.fu),
                            (unsigned long long)uint64_t(sbe1_dbg.op),
                            unsigned(bool(sbe1_dbg.valid)),
                            (unsigned long long)uint64_t(sbe1_dbg.result),
                            unsigned(bool(issue.i_scoreboard.mem_q[rs1_idx_dbg].issued)),
                            unsigned(bool(iro.rs2_raw_check_comb_func()[0])),
                            unsigned(bool(iro.rs2_has_raw_comb_func()[0])),
                            unsigned(bool(iro.rs2_valid_comb_func()[0])),
                            unsigned(bool(iro.forward_rs2_comb_func()[0])),
                            rs2_idx_dbg,
                            (unsigned long long)uint64_t(iro.rs2_res_comb_func()[0]),
                            (unsigned long long)uint64_t(sbe2_dbg.rd),
                            unsigned(bool(sbe2_dbg.valid)),
                            (unsigned long long)uint64_t(sbe2_dbg.result),
                            (unsigned long long)uint64_t(fwd_dbg.still_issued),
                            (unsigned long long)uint64_t(iro.fwd_res_valid_comb_func()),
                            (unsigned long long)uint64_t(fu_n0_dbg.operand_a),
                            (unsigned long long)uint64_t(fu_n0_dbg.operand_b),
                            (unsigned long long)uint64_t(iro.fu_data_q[0].operand_a),
                            (unsigned long long)uint64_t(iro.fu_data_q[0].operand_b),
                            (unsigned long long)uint64_t(wt_valid),
                            (unsigned long long)uint64_t(trans_id[ariane_pkg::FLU_WB]),
                            (unsigned long long)uint64_t(wbdata[ariane_pkg::FLU_WB]),
                            (unsigned long long)uint64_t(dut.i_cva6.we_gpr_commit_id_comb_func()),
                            (unsigned long long)uint64_t(dut.i_cva6.waddr_commit_id_comb_func()[0]),
                            (unsigned long long)uint64_t(dut.i_cva6.wdata_commit_id_comb_func()[0]));
            }
            std::fflush(stdout);
        }
        #endif

        if (debug && ((cycle >= 288 && cycle < 360) || (cycle >= 430 && cycle < 570))) {
            auto& ex_dbg = dut.i_cva6.ex_stage_i;
            auto& bu_dbg = ex_dbg.branch_unit_i;
            auto& fe_dbg = dut.i_cva6.i_frontend;
            auto& id_dbg = dut.i_cva6.id_stage_i;
            auto& ic_dbg = dut.i_cva6.i_cache_subsystem.i_cva6_icache;
            auto& iro_dbg = dut.i_cva6.issue_stage_i.i_issue_read_operands;
            const auto one_dbg = ex_dbg.one_cycle_data_comb_func();
            const auto bu_fu_dbg = bu_dbg.fu_data_i_in();
            const auto rb_top_dbg = dut.i_cva6.resolved_branch_comb_func();
            const auto fe_entry_dbg = fe_dbg.fetch_entry_o_out()[0];
            const auto id_fetch_dbg = id_dbg.fetch_entry_i_in()[0];
            const auto realign_addr_dbg = fe_dbg.addr_comb_func();
            const auto realign_instr_dbg = fe_dbg.instr_comb_func();
            const auto id_rvc_dbg = id_dbg.instruction_rvc_comb_func()[0];
            const auto id_deco_dbg = id_dbg.instruction_deco_comb_func()[0];
            const auto id_comp_i_dbg = id_dbg.compressed_decoder_i[0].instr_i_in();
            const auto id_comp_o_dbg = id_dbg.compressed_decoder_i[0].instr_o_out();
            const auto instr0_dbg = iro_dbg.issue_instr_i_in()[0];
            const auto fu_n0_dbg = iro_dbg.fu_data_n_comb_func()[0];
            std::printf("DBGINSTR cycle=%llu fe_v=0x%llx fe_pc=0x%llx fe_instr=0x%llx id_pc=0x%llx id_instr=0x%llx real_v=0x%llx real_a0=0x%llx real_a1=0x%llx real_i0=0x%llx real_i1=0x%llx comp_i=0x%llx comp_o=0x%llx rvc=0x%llx deco=0x%llx is_comp=0x%llx illegal=0x%llx\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(fe_dbg.fetch_entry_valid_o_out()),
                        (unsigned long long)uint64_t(fe_entry_dbg.address),
                        (unsigned long long)uint64_t(fe_entry_dbg.instruction),
                        (unsigned long long)uint64_t(id_fetch_dbg.address),
                        (unsigned long long)uint64_t(id_fetch_dbg.instruction),
                        (unsigned long long)uint64_t(fe_dbg.instruction_valid_comb_func()),
                        (unsigned long long)uint64_t(realign_addr_dbg[0]),
                        (unsigned long long)uint64_t(realign_addr_dbg[1]),
                        (unsigned long long)uint64_t(realign_instr_dbg[0]),
                        (unsigned long long)uint64_t(realign_instr_dbg[1]),
                        (unsigned long long)uint64_t(id_comp_i_dbg),
                        (unsigned long long)uint64_t(id_comp_o_dbg),
                        (unsigned long long)uint64_t(id_rvc_dbg),
                        (unsigned long long)uint64_t(id_deco_dbg),
                        (unsigned long long)uint64_t(id_dbg.is_compressed_deco_comb_func()),
                        (unsigned long long)uint64_t(id_dbg.is_illegal_deco_comb_func()));
            std::printf("DBGCACHE cycle=%llu rsp_valid=%u rsp_ready=%u rsp_vaddr=0x%llx rsp_data=0x%llx shamt=0x%llx data_comb=0x%llx data_q=0x%llx valid_q=%u vaddr_q=0x%llx realign_data=0x%llx realign_addr=0x%llx realign_valid=%u\n",
                        (unsigned long long)cycle,
                        unsigned(bool(fe_dbg.icache_dreq_i_in().valid)),
                        unsigned(bool(fe_dbg.icache_dreq_i_in().ready)),
                        (unsigned long long)uint64_t(fe_dbg.icache_dreq_i_in().vaddr),
                        (unsigned long long)uint64_t(fe_dbg.icache_dreq_i_in().data),
                        (unsigned long long)uint64_t(fe_dbg.shamt_comb_func()),
                        (unsigned long long)uint64_t(fe_dbg.icache_data_comb_func()),
                        (unsigned long long)uint64_t(fe_dbg.icache_data_q),
                        unsigned(bool(fe_dbg.icache_valid_q)),
                        (unsigned long long)uint64_t(fe_dbg.icache_vaddr_q),
                        (unsigned long long)uint64_t(fe_dbg.i_instr_realign.data_i_in()),
                        (unsigned long long)uint64_t(fe_dbg.i_instr_realign.address_i_in()),
                        unsigned(bool(fe_dbg.i_instr_realign.valid_i_in())));
            std::printf("DBGICACHE cycle=%llu state=%llu cmp=%u inv=%u cache_en=%u dreq_req=%u dreq_vaddr=0x%llx out_valid=%u out_ready=%u out_data=0x%llx vaddr_q=0x%llx cl_off_q=0x%llx cl_off_d=0x%llx hit=0x%llx hit_idx=%llu cl_sel0=0x%llx cl_sel1=0x%llx mem_vld=%u mem_type=%llu mem_data=0x%llx mem_req=%u mem_paddr=0x%llx rden=%u wren=%u\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(ic_dbg.state_q),
                        unsigned(bool(ic_dbg.cmp_en_q)),
                        unsigned(bool(ic_dbg.inv_q)),
                        unsigned(bool(ic_dbg.cache_en_q)),
                        unsigned(bool(ic_dbg.dreq_i_in().req)),
                        (unsigned long long)uint64_t(ic_dbg.dreq_i_in().vaddr),
                        unsigned(bool(ic_dbg.dreq_o_comb_func().valid)),
                        unsigned(bool(ic_dbg.dreq_o_comb_func().ready)),
                        (unsigned long long)uint64_t(ic_dbg.dreq_o_comb_func().data),
                        (unsigned long long)uint64_t(ic_dbg.vaddr_q),
                        (unsigned long long)uint64_t(ic_dbg.cl_offset_q),
                        (unsigned long long)uint64_t(ic_dbg.cl_offset_d_comb_func()),
                        (unsigned long long)uint64_t(ic_dbg.cl_hit_comb_func()),
                        (unsigned long long)uint64_t(ic_dbg.hit_idx_comb_func()),
                        (unsigned long long)uint64_t(ic_dbg.cl_sel_comb_func()[0]),
                        (unsigned long long)uint64_t(ic_dbg.cl_sel_comb_func()[1]),
                        unsigned(bool(ic_dbg.mem_rtrn_vld_i_in())),
                        (unsigned long long)uint64_t(ic_dbg.mem_rtrn_i_in().rtype),
                        (unsigned long long)uint64_t(ic_dbg.mem_rtrn_i_in().data),
                        unsigned(bool(ic_dbg.mem_data_req_o_out())),
                        (unsigned long long)uint64_t(ic_dbg.mem_data_o_out().paddr),
                        unsigned(bool(ic_dbg.cache_rden_comb_func())),
                        unsigned(bool(ic_dbg.cache_wren_comb_func())));
            std::printf("DBGBRANCH cycle=%llu top_br=0x%llx ex_br=0x%llx bu_br=%u instr_v=%u instr_pc=0x%llx instr_fu=%llu instr_op=%llu instr_rs1=%llu instr_rs2=%llu instr_rd=%llu one_fu=%llu one_op=%llu one_pc=0x%llx one_imm=0x%llx one_a=0x%llx one_b=0x%llx bu_op=%llu bu_imm=0x%llx bu_a=0x%llx bu_b=0x%llx bu_pc=0x%llx comp=%u alu_br=%u rb_v=%u rb_mis=%u rb_taken=%u rb_pc=0x%llx rb_target=0x%llx br_result=0x%llx target=0x%llx next=0x%llx fu_n_a=0x%llx fu_n_b=0x%llx fu_q_a=0x%llx fu_q_b=0x%llx\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(dut.i_cva6.branch_valid_id_ex_comb_func()),
                        (unsigned long long)uint64_t(ex_dbg.branch_valid_i_in()),
                        unsigned(bool(bu_dbg.branch_valid_i_in())),
                        unsigned(bool(iro_dbg.issue_instr_valid_i_in()[0])),
                        (unsigned long long)uint64_t(instr0_dbg.pc),
                        (unsigned long long)uint64_t(instr0_dbg.fu),
                        (unsigned long long)uint64_t(instr0_dbg.op),
                        (unsigned long long)uint64_t(instr0_dbg.rs1),
                        (unsigned long long)uint64_t(instr0_dbg.rs2),
                        (unsigned long long)uint64_t(instr0_dbg.rd),
                        (unsigned long long)uint64_t(one_dbg.fu),
                        (unsigned long long)uint64_t(one_dbg.operation),
                        (unsigned long long)uint64_t(ex_dbg.pc_i_in()),
                        (unsigned long long)uint64_t(one_dbg.imm),
                        (unsigned long long)uint64_t(one_dbg.operand_a),
                        (unsigned long long)uint64_t(one_dbg.operand_b),
                        (unsigned long long)uint64_t(bu_fu_dbg.operation),
                        (unsigned long long)uint64_t(bu_fu_dbg.imm),
                        (unsigned long long)uint64_t(bu_fu_dbg.operand_a),
                        (unsigned long long)uint64_t(bu_fu_dbg.operand_b),
                        (unsigned long long)uint64_t(bu_dbg.pc_i_in()),
                        unsigned(bool(bu_dbg.branch_comp_res_i_in())),
                        unsigned(bool(ex_dbg.alu_branch_res_comb_func())),
                        unsigned(bool(rb_top_dbg.valid)),
                        unsigned(bool(rb_top_dbg.is_mispredict)),
                        unsigned(bool(rb_top_dbg.is_taken)),
                        (unsigned long long)uint64_t(rb_top_dbg.pc),
                        (unsigned long long)uint64_t(rb_top_dbg.target_address),
                        (unsigned long long)uint64_t(bu_dbg.branch_result_o_out()),
                        (unsigned long long)uint64_t(bu_dbg.target_address_comb_func()),
                        (unsigned long long)uint64_t(bu_dbg.next_pc_comb_func()),
                        (unsigned long long)uint64_t(fu_n0_dbg.operand_a),
                        (unsigned long long)uint64_t(fu_n0_dbg.operand_b),
                        (unsigned long long)uint64_t(iro_dbg.fu_data_q[0].operand_a),
                        (unsigned long long)uint64_t(iro_dbg.fu_data_q[0].operand_b));
        }

        if (debug && cycle >= 438 && cycle <= 458) {
            auto& fe_pc = dut.i_cva6.i_frontend;
            auto ex_commit_dbg = dut.i_cva6.ex_commit_comb_func();
            std::printf("DBGPCSEL2 cycle=%llu ex=%u eret=%u flush=%u setpc=%u replay=%u mis=%u if_ready=%u bp=%u trap=0x%llx epc=0x%llx ex_commit=%u cause=%llu npc_q=0x%llx npc_d=0x%llx pc_commit=0x%llx\n",
                        (unsigned long long)cycle,
                        unsigned(bool(fe_pc.ex_valid_i_in())),
                        unsigned(bool(fe_pc.eret_i_in())),
                        unsigned(bool(fe_pc.flush_i_in())),
                        unsigned(bool(fe_pc.set_pc_commit_i_in())),
                        unsigned(bool(fe_pc.replay_comb_func())),
                        unsigned(bool(fe_pc.is_mispredict_comb_func())),
                        unsigned(bool(fe_pc.if_ready_comb_func())),
                        unsigned(bool(fe_pc.bp_valid_comb_func())),
                        (unsigned long long)uint64_t(fe_pc.trap_vector_base_i_in()),
                        (unsigned long long)uint64_t(fe_pc.epc_i_in()),
                        unsigned(bool(ex_commit_dbg.valid)),
                        (unsigned long long)uint64_t(ex_commit_dbg.cause),
                        (unsigned long long)uint64_t(fe_pc.npc_q),
                        (unsigned long long)uint64_t(fe_pc.npc_d_comb_func()),
                        (unsigned long long)uint64_t(fe_pc.pc_commit_i_in()));
            auto commit_dbg = dut.i_cva6.commit_instr_id_commit_comb_func()[0];
            auto& sb_dbg = dut.i_cva6.issue_stage_i.i_scoreboard;
            unsigned commit_idx_dbg = unsigned(uint64_t(sb_dbg.commit_pointer_q[0]));
            auto mem_dbg = sb_dbg.mem_q[commit_idx_dbg];
            auto wt_dbg = dut.i_cva6.wt_valid_ex_id_comb_func();
            auto tid_dbg = dut.i_cva6.trans_id_ex_id_comb_func();
            auto ex_wb_dbg = dut.i_cva6.ex_ex_ex_id_comb_func();
            auto& csr_dbg = dut.i_cva6.csr_regfile_i;
            auto csr_ex_dbg = dut.i_cva6.csr_exception_csr_commit_comb_func();
            auto commit_drop_dbg = dut.i_cva6.commit_drop_id_commit_comb_func();
            std::printf("DBGSBEX cycle=%llu cidx=%u cpc=0x%llx cvalid=%u cex=%u ccause=%llu mem_issued=%u mem_valid=%u mem_pc=0x%llx mem_ex=%u mem_cause=%llu wt=0x%llx tid0=%llu tid1=%llu tid2=%llu ex0=%u/%llu ex1=%u/%llu ex2=%u/%llu\n",
                        (unsigned long long)cycle,
                        commit_idx_dbg,
                        (unsigned long long)uint64_t(commit_dbg.pc),
                        unsigned(bool(commit_dbg.valid)),
                        unsigned(bool(commit_dbg.ex.valid)),
                        (unsigned long long)uint64_t(commit_dbg.ex.cause),
                        unsigned(bool(mem_dbg.issued)),
                        unsigned(bool(mem_dbg.sbe.valid)),
                        (unsigned long long)uint64_t(mem_dbg.sbe.pc),
                        unsigned(bool(mem_dbg.sbe.ex.valid)),
                        (unsigned long long)uint64_t(mem_dbg.sbe.ex.cause),
                        (unsigned long long)uint64_t(wt_dbg),
                        (unsigned long long)uint64_t(tid_dbg[0]),
                        (unsigned long long)uint64_t(tid_dbg[1]),
                        (unsigned long long)uint64_t(tid_dbg[2]),
                        unsigned(bool(ex_wb_dbg[0].valid)),
                        (unsigned long long)uint64_t(ex_wb_dbg[0].cause),
                        unsigned(bool(ex_wb_dbg[1].valid)),
                        (unsigned long long)uint64_t(ex_wb_dbg[1].cause),
                        unsigned(bool(ex_wb_dbg[2].valid)),
                        (unsigned long long)uint64_t(ex_wb_dbg[2].cause));
            std::printf("DBGCSR cycle=%llu csr_ex=%u/%llu csr_in=%u/%llu csr_op=%llu csr_addr=0x%llx conv=0x%llx priv=%llu csr_read=%u csr_we=%u upd_ex=%u read_ex=%u priv_ex=%u drop=0x%llx break=%u halt=%u commit_csr=%u ack=0x%llx\n",
                        (unsigned long long)cycle,
                        unsigned(bool(csr_ex_dbg.valid)),
                        (unsigned long long)uint64_t(csr_ex_dbg.cause),
                        unsigned(bool(dut.i_cva6.commit_stage_i.csr_exception_i_in().valid)),
                        (unsigned long long)uint64_t(dut.i_cva6.commit_stage_i.csr_exception_i_in().cause),
                        (unsigned long long)uint64_t(csr_dbg.csr_op_i_in()),
                        (unsigned long long)uint64_t(csr_dbg.csr_addr_i_in()),
                        (unsigned long long)uint64_t(csr_dbg.conv_csr_addr_comb_func().address),
                        (unsigned long long)uint64_t(csr_dbg.priv_lvl_o_out()),
                        unsigned(bool(csr_dbg.csr_read_comb_func())),
                        unsigned(bool(csr_dbg.csr_we_comb_func())),
                        unsigned(bool(csr_dbg.update_access_exception_comb_func())),
                        unsigned(bool(csr_dbg.read_access_exception_comb_func())),
                        unsigned(bool(csr_dbg.privilege_violation_comb_func())),
                        (unsigned long long)uint64_t(commit_drop_dbg),
                        unsigned(bool(dut.i_cva6.break_from_trigger_comb_func())),
                        unsigned(bool(dut.i_cva6.commit_stage_i.halt_i_in())),
                        unsigned(bool(dut.i_cva6.csr_commit_commit_ex_comb_func())),
                        (unsigned long long)uint64_t(dut.i_cva6.commit_ack_commit_id_comb_func()));
            std::fflush(stdout);
        }

        if (debug && cycle >= 486 && cycle <= 500) {
            auto& lsu_dbg = dut.i_cva6.ex_stage_i.lsu_i;
            auto& su_dbg = lsu_dbg.i_store_unit;
            auto& sbuf_dbg = su_dbg.store_buffer_i;
            auto& sb_dbg = dut.i_cva6.issue_stage_i.i_scoreboard;
            auto wt_top_dbg = dut.i_cva6.wt_valid_ex_id_comb_func();
            auto tid_top_dbg = dut.i_cva6.trans_id_ex_id_comb_func();
            auto wb_top_dbg = dut.i_cva6.wbdata_ex_id_comb_func();
            auto wt_sb_dbg = sb_dbg.wt_valid_i_in();
            auto tid_sb_dbg = sb_dbg.trans_id_i_in();
            auto wb_sb_dbg = sb_dbg.wbdata_i_in();
            const unsigned st_port_dbg = unsigned(ariane_pkg::STORE_WB);
            const unsigned st_tid_dbg = unsigned(uint64_t(tid_sb_dbg[st_port_dbg]));
            auto mem_n_dbg = sb_dbg.mem_n_comb_func();
            std::printf("DBGSTAFTER cycle=%llu su_state=%llu su_state_next=%llu su_valid_i=%u su_valid_o=%u su_st_valid=%u su_sb_valid=%u su_st_ready=%u su_pop=%u su_tid_q=%llu su_tid_n=%llu su_tid_next=%llu sb_valid_i=%u sb_cnt=%llu sb_cnt_n=%llu sb_cnt_next=%llu sb_w=%llu sb_w_n=%llu sb_w_next=%llu sb_sq0_v=%u sb_sq0_next_v=%u sb_sq0_addr=0x%llx sb_sq0_next_addr=0x%llx\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(su_dbg.state_q),
                        (unsigned long long)uint64_t(su_dbg.state_q._next),
                        unsigned(bool(su_dbg.valid_i_in())),
                        unsigned(bool(su_dbg.valid_o_out())),
                        unsigned(bool(su_dbg.st_valid_comb_func())),
                        unsigned(bool(su_dbg.store_buffer_valid_comb_func())),
                        unsigned(bool(su_dbg.st_ready_comb_func())),
                        unsigned(bool(su_dbg.pop_st_o_out())),
                        (unsigned long long)uint64_t(su_dbg.trans_id_q),
                        (unsigned long long)uint64_t(su_dbg.trans_id_n_comb_func()),
                        (unsigned long long)uint64_t(su_dbg.trans_id_q._next),
                        unsigned(bool(sbuf_dbg.valid_i_in())),
                        (unsigned long long)uint64_t(sbuf_dbg.speculative_status_cnt_q),
                        (unsigned long long)uint64_t(sbuf_dbg.speculative_status_cnt_n_comb_func()),
                        (unsigned long long)uint64_t(sbuf_dbg.speculative_status_cnt_q._next),
                        (unsigned long long)uint64_t(sbuf_dbg.speculative_write_pointer_q),
                        (unsigned long long)uint64_t(sbuf_dbg.speculative_write_pointer_n_comb_func()),
                        (unsigned long long)uint64_t(sbuf_dbg.speculative_write_pointer_q._next),
                        unsigned(bool(sbuf_dbg.speculative_queue_q[0].valid)),
                        unsigned(bool(sbuf_dbg.speculative_queue_q._next[0].valid)),
                        (unsigned long long)uint64_t(sbuf_dbg.speculative_queue_q[0].address),
                        (unsigned long long)uint64_t(sbuf_dbg.speculative_queue_q._next[0].address));
            std::printf("DBGWBAFTER cycle=%llu top_wt=0x%llx top_st_tid=%llu top_st_wb=0x%llx sb_wt=0x%llx sb_st_tid=%u sb_st_wb=0x%llx memq_issued=%u memq_valid=%u memq_fu=%llu memq_op=%llu memq_pc=0x%llx memn_valid=%u memn_result=0x%llx next_valid=%u next_result=0x%llx ex_st_valid=%u ex_st_tid=%llu lsu_st_valid=%u lsu_st_tid=%llu\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(wt_top_dbg),
                        (unsigned long long)uint64_t(tid_top_dbg[st_port_dbg]),
                        (unsigned long long)uint64_t(wb_top_dbg[st_port_dbg]),
                        (unsigned long long)uint64_t(wt_sb_dbg),
                        st_tid_dbg,
                        (unsigned long long)uint64_t(wb_sb_dbg[st_port_dbg]),
                        unsigned(bool(sb_dbg.mem_q[st_tid_dbg].issued)),
                        unsigned(bool(sb_dbg.mem_q[st_tid_dbg].sbe.valid)),
                        (unsigned long long)uint64_t(sb_dbg.mem_q[st_tid_dbg].sbe.fu),
                        (unsigned long long)uint64_t(sb_dbg.mem_q[st_tid_dbg].sbe.op),
                        (unsigned long long)uint64_t(sb_dbg.mem_q[st_tid_dbg].sbe.pc),
                        unsigned(bool(mem_n_dbg[st_tid_dbg].sbe.valid)),
                        (unsigned long long)uint64_t(mem_n_dbg[st_tid_dbg].sbe.result),
                        unsigned(bool(sb_dbg.mem_q._next[st_tid_dbg].sbe.valid)),
                        (unsigned long long)uint64_t(sb_dbg.mem_q._next[st_tid_dbg].sbe.result),
                        unsigned(bool(dut.i_cva6.ex_stage_i.store_valid_o_out())),
                        (unsigned long long)uint64_t(dut.i_cva6.ex_stage_i.store_trans_id_o_out()),
                        unsigned(bool(lsu_dbg.store_valid_o_out())),
                        (unsigned long long)uint64_t(lsu_dbg.store_trans_id_o_out()));
            std::fflush(stdout);
        }
        if (debug && cycle >= 500 && cycle <= 540) {
            auto& issue_dbg = dut.i_cva6.issue_stage_i;
            auto& sb_dbg = issue_dbg.i_scoreboard;
            auto& iro_dbg = issue_dbg.i_issue_read_operands;
            auto& lsu_dbg = dut.i_cva6.ex_stage_i.lsu_i;
            auto& su_dbg = lsu_dbg.i_store_unit;
            auto& sbuf_dbg = su_dbg.store_buffer_i;
            const auto st_req_o = sbuf_dbg.req_port_o_out();
            const auto st_req_i = sbuf_dbg.req_port_i_in();
            const auto commit_instr_dbg = dut.i_cva6.commit_instr_id_commit_comb_func()[0];
            const auto commit_ack_dbg = dut.i_cva6.commit_ack_commit_id_comb_func();
            const auto issue_slot = sb_dbg.mem_q[uint64_t(sb_dbg.issue_pointer_q)];
            const auto commit_slot = sb_dbg.mem_q[uint64_t(sb_dbg.commit_pointer_q[0])];
            const auto issue_out = sb_dbg.issue_instr_o_out()[0];
            const auto lsu_req_dbg = lsu_dbg.lsu_req_i_comb_func();
            std::printf("DBGSTALL cycle=%llu commit_ack=0x%llx commit_v=%u commit_pc=0x%llx commit_fu=%llu commit_op=%llu issue_ptr=%llu commit_ptr=%llu issue_slot_issued=%u issue_slot_v=%u issue_slot_pc=0x%llx commit_slot_issued=%u commit_slot_v=%u commit_slot_pc=0x%llx sb_issue_v=0x%llx sb_issue_pc=0x%llx iro_ack=0x%llx iro_stall=%u iro_busy=0x%llx lsu_req_v=%u lsu_req_fu=%llu lsu_req_op=%llu lsu_req_tid=%llu lsu_req_addr=0x%llx lsu_ready=%u lsu_commit=%u lsu_commit_ready=%u lsu_store_v=%u su_state=%llu su_valid_i=%u su_valid_o=%u su_st_ready=%u sbuf_ready=%u sbuf_valid_i=%u sbuf_req=%u sbuf_we=%u sbuf_gnt=%u sbuf_rvalid=%u sbuf_idx=0x%llx sbuf_tag=0x%llx sbuf_wdata=0x%llx sbuf_be=0x%llx spec_cnt=%llu commit_cnt=%llu spec_r=%llu spec_w=%llu commit_r=%llu commit_w=%llu sq0_v=%u sq0_addr=0x%llx cq0_v=%u cq0_wait=%u cq0_addr=0x%llx\n",
                        (unsigned long long)cycle,
                        (unsigned long long)uint64_t(commit_ack_dbg),
                        unsigned(bool(commit_instr_dbg.valid)),
                        (unsigned long long)uint64_t(commit_instr_dbg.pc),
                        (unsigned long long)uint64_t(commit_instr_dbg.fu),
                        (unsigned long long)uint64_t(commit_instr_dbg.op),
                        (unsigned long long)uint64_t(sb_dbg.issue_pointer_q),
                        (unsigned long long)uint64_t(sb_dbg.commit_pointer_q[0]),
                        unsigned(bool(issue_slot.issued)),
                        unsigned(bool(issue_slot.sbe.valid)),
                        (unsigned long long)uint64_t(issue_slot.sbe.pc),
                        unsigned(bool(commit_slot.issued)),
                        unsigned(bool(commit_slot.sbe.valid)),
                        (unsigned long long)uint64_t(commit_slot.sbe.pc),
                        (unsigned long long)uint64_t(sb_dbg.issue_instr_valid_o_out()),
                        (unsigned long long)uint64_t(issue_out.pc),
                        (unsigned long long)uint64_t(iro_dbg.issue_ack_o_out()),
                        unsigned(bool(iro_dbg.stall_i_in())),
                        (unsigned long long)uint64_t(iro_dbg.fu_busy_comb_func()),
                        unsigned(bool(lsu_req_dbg.valid)),
                        (unsigned long long)uint64_t(lsu_req_dbg.fu),
                        (unsigned long long)uint64_t(lsu_req_dbg.operation),
                        (unsigned long long)uint64_t(lsu_req_dbg.trans_id),
                        (unsigned long long)uint64_t(lsu_req_dbg.vaddr),
                        unsigned(bool(dut.i_cva6.ex_stage_i.lsu_ready_o_out())),
                        unsigned(bool(lsu_dbg.commit_i_in())),
                        unsigned(bool(lsu_dbg.commit_ready_o_out())),
                        unsigned(bool(lsu_dbg.store_valid_o_out())),
                        (unsigned long long)uint64_t(su_dbg.state_q),
                        unsigned(bool(su_dbg.valid_i_in())),
                        unsigned(bool(su_dbg.valid_o_out())),
                        unsigned(bool(su_dbg.st_ready_comb_func())),
                        unsigned(bool(sbuf_dbg.ready_o_out())),
                        unsigned(bool(sbuf_dbg.valid_i_in())),
                        unsigned(bool(st_req_o.data_req)),
                        unsigned(bool(st_req_o.data_we)),
                        unsigned(bool(st_req_i.data_gnt)),
                        unsigned(bool(st_req_i.data_rvalid)),
                        (unsigned long long)uint64_t(st_req_o.address_index),
                        (unsigned long long)uint64_t(st_req_o.address_tag),
                        (unsigned long long)uint64_t(st_req_o.data_wdata),
                        (unsigned long long)uint64_t(st_req_o.data_be),
                        (unsigned long long)uint64_t(sbuf_dbg.speculative_status_cnt_q),
                        (unsigned long long)uint64_t(sbuf_dbg.commit_status_cnt_q),
                        (unsigned long long)uint64_t(sbuf_dbg.speculative_read_pointer_q),
                        (unsigned long long)uint64_t(sbuf_dbg.speculative_write_pointer_q),
                        (unsigned long long)uint64_t(sbuf_dbg.commit_read_pointer_q),
                        (unsigned long long)uint64_t(sbuf_dbg.commit_write_pointer_q),
                        unsigned(bool(sbuf_dbg.speculative_queue_q[0].valid)),
                        (unsigned long long)uint64_t(sbuf_dbg.speculative_queue_q[0].address),
                        unsigned(bool(sbuf_dbg.commit_queue_q[0].valid)),
                        unsigned(bool(sbuf_dbg.commit_queue_q[0].wait_rvalid)),
                        (unsigned long long)uint64_t(sbuf_dbg.commit_queue_q[0].address));
            {
                auto& dcache_wrap = dut.i_cva6.i_cache_subsystem.i_dcache;
                auto& store_adapter = dcache_wrap.i_cva6_hpdcache_store_if_adapter;
                auto& hpdcache = dcache_wrap.i_hpdcache;
                auto& core_arb = hpdcache.core_req_arbiter_i;
                auto& ctrl = hpdcache.hpdcache_ctrl_i;
                auto& pe = ctrl.hpdcache_ctrl_pe_i;
                auto& memctrl = ctrl.hpdcache_memctrl_i;
                using dcache_req_ports_t = std::remove_cvref_t<decltype(dcache_wrap.dcache_req_ports_i_in())>;
                constexpr std::size_t store_port = dcache_req_ports_t::SIZE_BITS / dcache_req_ports_t::ELEMENT_BITS - 1;
                const auto dcache_in = dcache_wrap.dcache_req_ports_i_in();
                const auto dcache_out = dcache_wrap.dcache_req_ports_o_out();
                const auto req_valid = dcache_wrap.dcache_req_valid_comb_func();
                const auto req_ready = dcache_wrap.dcache_req_ready_comb_func();
                const auto req_vec = hpdcache.core_req_valid_i_in();
                const auto ready_vec = hpdcache.core_req_ready_o_out();
                const auto arb_req = core_arb.arb_req_o_out();
                std::printf("DBGCACHE cycle=%llu port=%zu in_req=%u in_we=%u in_idx=0x%llx in_tag=0x%llx in_wdata=0x%llx in_be=0x%llx out_gnt=%u ad_v=%u ad_rdy=%u ad_req_v=%u ad_req_idx=0x%llx ad_req_tag=0x%llx wrap_v=%u wrap_rdy=%u hpdc_vec=0x%llx hpdc_ready=0x%llx arb_gnt_d=0x%llx arb_valid=%u arb_ready=%u arb_op=%llu arb_off=0x%llx arb_tag=0x%llx ctrl_ready=%u pe_ready=%u pe_core_v=%u pe_scrub=%u pe_rtab=%u pe_refill=%u pe_rtab_full=%u pe_cmo_busy=%u pe_uc_busy=%u pe_err_busy=%u pe_rtab_fence=%u pe_st1_v=%u pe_st1_store=%u pe_st1_rtab=%u pe_st1_partial=%u pe_wbuf_ready=%u pe_cachedir_init=%u pe_refill_busy=%u pe_flush_busy=%u\n",
                            (unsigned long long)cycle,
                            store_port,
                            unsigned(bool(dcache_in[store_port].data_req)),
                            unsigned(bool(dcache_in[store_port].data_we)),
                            (unsigned long long)uint64_t(dcache_in[store_port].address_index),
                            (unsigned long long)uint64_t(dcache_in[store_port].address_tag),
                            (unsigned long long)uint64_t(dcache_in[store_port].data_wdata),
                            (unsigned long long)uint64_t(dcache_in[store_port].data_be),
                            unsigned(bool(dcache_out[store_port].data_gnt)),
                            unsigned(bool(store_adapter.hpdcache_req_valid_o_out())),
                            unsigned(bool(store_adapter.hpdcache_req_ready_i_in())),
                            unsigned(bool(store_adapter.hpdcache_req_o_out().op == hpdcache_pkg::HPDCACHE_REQ_STORE)),
                            (unsigned long long)uint64_t(store_adapter.hpdcache_req_o_out().addr_offset),
                            (unsigned long long)uint64_t(store_adapter.hpdcache_req_o_out().addr_tag),
                            unsigned(bool(req_valid[store_port])),
                            unsigned(bool(req_ready[store_port])),
                            (unsigned long long)uint64_t(cpphdl::pack_value<cpphdl::type_width<decltype(req_vec)>()>(req_vec)),
                            (unsigned long long)uint64_t(cpphdl::pack_value<cpphdl::type_width<decltype(ready_vec)>()>(ready_vec)),
                            (unsigned long long)uint64_t(core_arb.arb_req_gnt_d_comb_func()),
                            unsigned(bool(core_arb.arb_req_valid_o_out())),
                            unsigned(bool(core_arb.arb_req_ready_i_in())),
                            (unsigned long long)uint64_t(arb_req.op),
                            (unsigned long long)uint64_t(arb_req.addr_offset),
                            (unsigned long long)uint64_t(core_arb.arb_tag_o_out()),
                            unsigned(bool(ctrl.core_req_ready_o_out())),
                            unsigned(bool(pe.core_req_ready_o_out())),
                            unsigned(bool(pe.core_req_valid_i_in())),
                            unsigned(bool(pe.scrub_req_valid_i_in())),
                            unsigned(bool(pe.rtab_req_valid_i_in())),
                            unsigned(bool(pe.refill_req_valid_i_in())),
                            unsigned(bool(pe.rtab_full_i_in())),
                            unsigned(bool(pe.cmo_busy_i_in())),
                            unsigned(bool(pe.uc_busy_i_in())),
                            unsigned(bool(pe.err_busy_i_in())),
                            unsigned(bool(pe.rtab_fence_i_in())),
                            unsigned(bool(pe.st1_req_valid_i_in())),
                            unsigned(bool(pe.st1_req_is_store_i_in())),
                            unsigned(bool(pe.st1_req_rtab_i_in())),
                            unsigned(bool(pe.st1_req_is_partial_i_in())),
                            unsigned(bool(pe.wbuf_write_ready_i_in())),
                            unsigned(bool(pe.cachedir_init_ready_i_in())),
                            unsigned(bool(pe.refill_busy_i_in())),
                            unsigned(bool(pe.flush_busy_i_in())));
                const unsigned memctrl_rst = unsigned(bool(memctrl.rst_ni_in()));
                const unsigned memctrl_init_q = unsigned(bool(memctrl.init_q));
                const unsigned memctrl_init_next_before = unsigned(bool(memctrl.init_q._next));
                const unsigned long long memctrl_init_set_q = (unsigned long long)uint64_t(memctrl.init_set_q);
                const unsigned long long memctrl_init_set_next_before = (unsigned long long)uint64_t(memctrl.init_set_q._next);
                const unsigned memctrl_init_d = unsigned(bool(memctrl.init_d_comb_func()));
                const unsigned memctrl_init_next_after_d = unsigned(bool(memctrl.init_q._next));
                const unsigned long long memctrl_init_set_d = (unsigned long long)uint64_t(memctrl.init_set_d_comb_func());
                const unsigned long long memctrl_init_set_next_after_d = (unsigned long long)uint64_t(memctrl.init_set_q._next);
                const unsigned memctrl_ready = unsigned(bool(memctrl.ready_o_out()));
                const unsigned long long memctrl_dir_cs = (unsigned long long)uint64_t(memctrl.dir_cs_comb_func());
                const unsigned long long memctrl_dir_we = (unsigned long long)uint64_t(memctrl.dir_we_comb_func());
                const unsigned long long memctrl_dir_addr = (unsigned long long)uint64_t(memctrl.dir_addr_comb_func());
                std::printf("DBGMEMCTRL cycle=%llu rst=%u init_q=%u init_next_before=%u init_d=%u init_next_after_d=%u init_set_q=0x%llx init_set_next_before=0x%llx init_set_d=0x%llx init_set_next_after_d=0x%llx ready=%u dir_cs=0x%llx dir_we=0x%llx dir_addr=0x%llx\n",
                            (unsigned long long)cycle,
                            memctrl_rst,
                            memctrl_init_q,
                            memctrl_init_next_before,
                            memctrl_init_d,
                            memctrl_init_next_after_d,
                            memctrl_init_set_q,
                            memctrl_init_set_next_before,
                            memctrl_init_set_d,
                            memctrl_init_set_next_after_d,
                            memctrl_ready,
                            memctrl_dir_cs,
                            memctrl_dir_we,
                            memctrl_dir_addr);
            }
            std::fflush(stdout);
        }
        if (debug && cycle >= 288 && cycle < 296) {
            auto& id_after = dut.i_cva6.id_stage_i;
            std::printf("DBGIDNEXT cycle=%llu q_valid=%u q_fu=%llu n_valid=%u n_fu=%llu next_valid=%u next_fu=%llu next_pack=0x%llx\n",
                        (unsigned long long)cycle,
                        unsigned(bool(id_after.issue_q[0].valid)),
                        (unsigned long long)uint64_t(id_after.issue_q[0].sbe.fu),
                        unsigned(bool(id_after.issue_n_comb_func()[0].valid)),
                        (unsigned long long)uint64_t(id_after.issue_n_comb_func()[0].sbe.fu),
                        unsigned(bool(id_after.issue_q._next[0].valid)),
                        (unsigned long long)uint64_t(id_after.issue_q._next[0].sbe.fu),
                        (unsigned long long)uint64_t(id_after.issue_q._next[0].pack()));
            std::fflush(stdout);
        }
        if (trace_steps && cycle < 16) {
            std::fprintf(stderr, "TRACE cycle=%llu after _work\n", (unsigned long long)cycle);
            std::fflush(stderr);
        }
        if (progress && cycle != 0 && (cycle % 1000ull) == 0) {
            auto commit_instr = dut.i_cva6.commit_instr_id_commit_comb_func();
            auto commit_ack = dut.i_cva6.commit_ack_commit_id_comb_func();
            std::printf("cpphdl PROGRESS cycle=%llu ar=%llu r=%llu aw=%llu w=%llu b=%llu commit_ack=0x%llx commit_v=%u commit_pc=0x%llx\n",
                        (unsigned long long)cycle,
                        (unsigned long long)ar_count,
                        (unsigned long long)r_count,
                        (unsigned long long)aw_count,
                        (unsigned long long)w_count,
                        (unsigned long long)b_count,
                        (unsigned long long)uint64_t(commit_ack),
                        unsigned(bool(commit_instr[0].valid)),
                        (unsigned long long)uint64_t(commit_instr[0].pc));
            std::fflush(stdout);
        }
        if (early_debug && (cycle < 32 || (cycle >= 250 && cycle < 340))) {
            auto& fe = dut.i_cva6.i_frontend;
            auto& iq = fe.i_instr_queue;
            const auto& if_req = fe.icache_dreq_o_out();
            const auto& if_resp = fe.icache_dreq_i_in();
            std::printf("EARLY cycle=%llu rst=%u boot=0x%llx top_boot=0x%llx cva6_boot=0x%llx fe_boot=0x%llx "
                        "npc_q=0x%llx npc_d=0x%llx npc_rst=%u if_req=%u if_vaddr=0x%llx if_ready=%u if_valid=%u "
                        "ic_vaddr_q=0x%llx ic_valid_q=%u iq_pc=0x%llx iq_pc_d=0x%llx iq_reset=%u iq_valid=0x%llx iq_addr0=0x%llx\n",
                        (unsigned long long)cycle,
                        unsigned(bool(rst_n)),
                        (unsigned long long)uint64_t(boot_addr),
                        (unsigned long long)uint64_t(dut.boot_addr_i_in()),
                        (unsigned long long)uint64_t(dut.i_cva6.boot_addr_i_in()),
                        (unsigned long long)uint64_t(fe.boot_addr_i_in()),
                        (unsigned long long)uint64_t(fe.npc_q),
                        (unsigned long long)uint64_t(fe.npc_d_comb_func()),
                        unsigned(bool(fe.npc_rst_load_q)),
                        unsigned(bool(if_req.req)),
                        (unsigned long long)uint64_t(if_req.vaddr),
                        unsigned(bool(if_resp.ready)),
                        unsigned(bool(if_resp.valid)),
                        (unsigned long long)uint64_t(fe.icache_vaddr_q),
                        unsigned(bool(fe.icache_valid_q)),
                        (unsigned long long)uint64_t(iq.pc_q),
                        (unsigned long long)uint64_t(iq.pc_d_comb_func()),
                        unsigned(bool(iq.reset_address_q)),
                        (unsigned long long)uint64_t(iq.valid_i_in()),
                        (unsigned long long)uint64_t(iq.addr_i_in()[0]));
        }
        if (trace_steps && cycle < 16) {
            std::fprintf(stderr, "TRACE cycle=%llu after _strobe\n", (unsigned long long)cycle);
            std::fflush(stderr);
        }
    }
    } catch (const cpphdl_exception& e) {
        std::fprintf(stderr, "CPPHDL_EXCEPTION cycle=%llu: %s\n",
                     (unsigned long long)cycle,
                     e.text.c_str());
        return 3;
    }

    std::printf("cpphdl TIMEOUT cycles=%llu entry=0x%x tohost=0x%llx ar=%llu r=%llu aw=%llu w=%llu b=%llu\n",
                (unsigned long long)max_cycles,
                entry,
                (unsigned long long)tohost,
                (unsigned long long)ar_count,
                (unsigned long long)r_count,
                (unsigned long long)aw_count,
                (unsigned long long)w_count,
                (unsigned long long)b_count);
    return 1;
}
