#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <fesvr/dtm.h>

#include "run_cpphdl_testharness_model.h"

void cpphdl_model_trace_irq(uint64_t cycle);

long _system_clock = 0;

namespace {

class PreloadAwareDtm : public dtm_t {
public:
    PreloadAwareDtm(int argc, char** argv) : dtm_t(argc, argv) {}
    bool is_address_preloaded(addr_t, size_t) override { return true; }
    void reset() override {}
};

dtm_t* debugTransport = nullptr;

} // namespace

extern "C" int debug_tick(unsigned char* req_valid, unsigned char req_ready,
                          int* req_addr, int* req_op, int* req_data,
                          unsigned char resp_valid, unsigned char* resp_ready,
                          int resp_status, int resp_data)
{
    dtm_t::resp response{
        .resp = static_cast<uint32_t>(resp_status),
        .data = static_cast<uint32_t>(resp_data),
    };
    if (std::getenv("CPPHDL_TRACE_DTM") != nullptr && resp_valid) {
        std::fprintf(stderr,
                     "CPPDTM_IN clock=%ld ready=%u resp=%u status=%d data=0x%x\n",
                     _system_clock, static_cast<unsigned>(req_ready),
                     static_cast<unsigned>(resp_valid), resp_status,
                     static_cast<unsigned>(resp_data));
    }
    debugTransport->tick(req_ready, resp_valid, response);
    *resp_ready = debugTransport->resp_ready();
    *req_valid = debugTransport->req_valid();
    *req_addr = debugTransport->req_bits().addr;
    *req_op = debugTransport->req_bits().op;
    *req_data = debugTransport->req_bits().data;
    if (std::getenv("CPPHDL_TRACE_DTM") != nullptr &&
        (_system_clock >= 480 || *req_valid || resp_valid)) {
        std::fprintf(stderr,
                     "CPPDTM clock=%ld req=%u/%u addr=0x%x op=%d data=0x%x "
                     "resp=%u/%u status=%d data=0x%x\n",
                     _system_clock, static_cast<unsigned>(*req_valid),
                     static_cast<unsigned>(req_ready), *req_addr, *req_op,
                     static_cast<unsigned>(*req_data), static_cast<unsigned>(resp_valid),
                     static_cast<unsigned>(*resp_ready), resp_status,
                     static_cast<unsigned>(resp_data));
    }
    return debugTransport->done() ? (debugTransport->exit_code() << 1 | 1) : 0;
}

extern "C" int jtag_tick(unsigned char* tck, unsigned char* tms,
                         unsigned char* tdi, unsigned char* trstn, unsigned char)
{
    *tck = 0;
    *tms = 0;
    *tdi = 0;
    *trstn = 1;
    return 0;
}

namespace {

struct Elf32Ehdr {
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
};

struct Elf32Phdr {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
};

struct Elf32Shdr {
    uint32_t name;
    uint32_t type;
    uint32_t flags;
    uint32_t addr;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t addralign;
    uint32_t entsize;
};

struct Elf32Sym {
    uint32_t name;
    uint32_t value;
    uint32_t size;
    uint8_t info;
    uint8_t other;
    uint16_t shndx;
};

struct LoadedElf {
    uint32_t entry;
    uint32_t tohost;
};

std::vector<unsigned char> readFile(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::fprintf(stderr, "failed to open %s\n", path);
        std::exit(2);
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void writeByte(uint64_t offset, uint8_t value)
{
    cpphdl_model_write_byte(offset, value);
}

uint32_t findElfSymbol(const std::vector<unsigned char>& file,
                       const Elf32Ehdr& header, const char* wanted)
{
    constexpr uint32_t symbolTable = 2;
    constexpr uint32_t dynamicSymbolTable = 11;

    auto readSection = [&](uint32_t index) {
        Elf32Shdr section{};
        const uint64_t offset = uint64_t(header.shoff) + uint64_t(index) * header.shentsize;
        if (index >= header.shnum || header.shentsize < sizeof(section) ||
            offset + sizeof(section) > file.size()) {
            std::fprintf(stderr, "invalid ELF section header\n");
            std::exit(2);
        }
        std::memcpy(&section, file.data() + offset, sizeof(section));
        return section;
    };

    for (uint32_t sectionIndex = 0; sectionIndex < header.shnum; ++sectionIndex) {
        const Elf32Shdr symbols = readSection(sectionIndex);
        if ((symbols.type != symbolTable && symbols.type != dynamicSymbolTable) ||
            symbols.entsize < sizeof(Elf32Sym) || symbols.link >= header.shnum ||
            uint64_t(symbols.offset) + symbols.size > file.size()) {
            continue;
        }

        const Elf32Shdr strings = readSection(symbols.link);
        if (uint64_t(strings.offset) + strings.size > file.size()) {
            std::fprintf(stderr, "invalid ELF string table\n");
            std::exit(2);
        }

        const uint32_t count = symbols.size / symbols.entsize;
        for (uint32_t symbolIndex = 0; symbolIndex < count; ++symbolIndex) {
            Elf32Sym symbol{};
            const uint64_t offset = uint64_t(symbols.offset) +
                                    uint64_t(symbolIndex) * symbols.entsize;
            std::memcpy(&symbol, file.data() + offset, sizeof(symbol));
            if (symbol.name >= strings.size) {
                continue;
            }
            const char* name = reinterpret_cast<const char*>(
                file.data() + uint64_t(strings.offset) + symbol.name);
            const std::size_t available = strings.size - symbol.name;
            if (std::memchr(name, '\0', available) && std::strcmp(name, wanted) == 0) {
                return symbol.value;
            }
        }
    }
    return 0;
}

LoadedElf loadElf(const char* path)
{
    constexpr uint64_t dramBase = 0x80000000ull;
    constexpr uint64_t preloadSize = 0x00ffffffull;
    constexpr uint32_t loadSegment = 1;

    const auto file = readFile(path);
    if (file.size() < sizeof(Elf32Ehdr)) {
        std::fprintf(stderr, "ELF file is too small: %s\n", path);
        std::exit(2);
    }

    Elf32Ehdr header{};
    std::memcpy(&header, file.data(), sizeof(header));
    if (header.ident[0] != 0x7f || header.ident[1] != 'E' ||
        header.ident[2] != 'L' || header.ident[3] != 'F' || header.ident[4] != 1) {
        std::fprintf(stderr, "expected an ELF32 file: %s\n", path);
        std::exit(2);
    }

    for (uint16_t i = 0; i < header.phnum; ++i) {
        const uint64_t headerOffset = uint64_t(header.phoff) + uint64_t(i) * header.phentsize;
        if (headerOffset + sizeof(Elf32Phdr) > file.size()) {
            std::fprintf(stderr, "invalid ELF program header: %s\n", path);
            std::exit(2);
        }
        Elf32Phdr segment{};
        std::memcpy(&segment, file.data() + headerOffset, sizeof(segment));
        if (segment.type != loadSegment || segment.paddr < dramBase ||
            uint64_t(segment.paddr) >= dramBase + preloadSize) {
            continue;
        }
        if (uint64_t(segment.offset) + segment.filesz > file.size()) {
            std::fprintf(stderr, "invalid ELF load segment: %s\n", path);
            std::exit(2);
        }
        const uint64_t memoryOffset = uint64_t(segment.paddr) - dramBase;
        if (memoryOffset + segment.memsz > preloadSize) {
            std::fprintf(stderr, "ELF segment exceeds native preload window: %s\n", path);
            std::exit(2);
        }
        for (uint32_t byte = 0; byte < segment.filesz; ++byte) {
            writeByte(memoryOffset + byte, file[uint64_t(segment.offset) + byte]);
        }
        for (uint32_t byte = segment.filesz; byte < segment.memsz; ++byte) {
            writeByte(memoryOffset + byte, 0);
        }
    }
    cpphdl_model_apply_memory();
    return {header.entry, findElfSymbol(file, header, "tohost")};
}

uint64_t parseU64(const char* text)
{
    return std::strtoull(text, nullptr, 0);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s program.riscv [max_cycles]\n", argv[0]);
        return 2;
    }

    const uint64_t maxCycles = argc > 2 ? parseU64(argv[2]) : 500000;
    char* dtmArgv[] = {argv[0], argv[1]};
    debugTransport = new PreloadAwareDtm(2, dtmArgv);
    bool rtc = false;
    cpphdl_model_create();
    cpphdl_model_set_reset(false);
    cpphdl_model_set_rtc(false);
    cpphdl_model_assign();
    if (std::getenv("CPPHDL_TRACE_DTM") != nullptr) {
        cpphdl_model_trace_hartinfo();
    }

    uint64_t mainTime = 0;
    cpphdl_model_work(true);
    for (; mainTime < 10; ++mainTime) {
        cpphdl_model_set_reset(false);
        cpphdl_model_set_rtc(false);
        cpphdl_model_strobe();
        ++_system_clock;
        if (mainTime + 1 < 10) {
            cpphdl_model_work(true);
        }
    }

    cpphdl_model_set_reset(true);
    const LoadedElf elf = loadElf(argv[1]);
    if (elf.tohost == 0) {
        std::fprintf(stderr, "ELF has no nonzero tohost symbol: %s\n", argv[1]);
        return 2;
    }

    // The native SV initial block obtains these values through plusargs and DPI ELF helpers.
    // Initialize the converted tracer from the equivalent C++ testbench services.
    cpphdl_model_configure(maxCycles, elf.tohost);
    std::fprintf(stderr, "cpphdl testharness loaded entry=0x%08x tohost=0x%08x\n",
                 elf.entry, elf.tohost);
    cpphdl_model_work(false);

    uint64_t committedInstructions = 0;
    uint32_t lastCommittedPc = 0;
    uint32_t lastCommittedInsn = 0;
    const bool traceCommits = std::getenv("CPPHDL_TRACE_COMMITS") != nullptr;
    const bool traceMemory = std::getenv("CPPHDL_TRACE_MEMORY") != nullptr;
    const bool traceIrq = std::getenv("CPPHDL_TRACE_IRQ") != nullptr;
    const bool traceRvfiRd = std::getenv("CPPHDL_TRACE_RVFI_RD") != nullptr;
    // Keep retired deep-path probes out of template instantiation in the runner.
#if 0
    const bool traceXbar = std::getenv("CPPHDL_TRACE_XBAR") != nullptr;
    const bool traceFetch = std::getenv("CPPHDL_TRACE_FETCH") != nullptr;
#endif
    for (; mainTime < maxCycles; ++mainTime) {
        cpphdl_model_strobe();
        if (traceRvfiRd) {
            cpphdl_model_trace_rvfi_rd(mainTime);
        }

        const std::size_t commitLanes = cpphdl_model_commit_lanes();
        for (std::size_t lane = 0; lane < commitLanes; ++lane) {
            const CpphdlCommit committed = cpphdl_model_commit(lane);
            if (committed.valid) {
                ++committedInstructions;
                lastCommittedPc = committed.pc;
                lastCommittedInsn = committed.insn;
                if (traceCommits) {
                    std::fprintf(stderr,
                                 "CPPCOMMIT cycle=%llu ordinal=%llu lane=%zu order=%llu pc=0x%08x "
                                 "next=0x%08x insn=0x%08x trap=%u cause=0x%08x "
                                 "rd=x%u data=0x%08x\n",
                                 static_cast<unsigned long long>(mainTime),
                                 static_cast<unsigned long long>(committedInstructions), lane,
                                 static_cast<unsigned long long>(committed.order),
                                 lastCommittedPc,
                                 committed.nextPc,
                                 lastCommittedInsn,
                                 static_cast<unsigned>(committed.trap),
                                 committed.cause,
                                 static_cast<unsigned>(committed.rd),
                                 committed.data);
                }
            }
        }
        if ((mainTime % 10000) == 0) {
            std::fprintf(stderr,
                         "cpphdl progress cycle=%llu commits=%llu pc=0x%08x insn=0x%08x\n",
                         static_cast<unsigned long long>(mainTime),
                         static_cast<unsigned long long>(committedInstructions),
                         lastCommittedPc, lastCommittedInsn);
        }

#if 0
        if (traceStall && ((mainTime >= 980 && mainTime <= 1050) ||
                           mainTime == 1100 || mainTime == 1500 ||
                           mainTime + 1 == maxCycles)) {
            auto& core = dut.i_ariane.i_cva6;
            auto& frontend = core.i_frontend;
            auto& id = core.id_stage_i;
            auto& issue = core.issue_stage_i;
            auto& scoreboard = issue.i_scoreboard;
            auto& store = core.ex_stage_i.lsu_i.i_store_unit;
            auto& storeBuffer = store.store_buffer_i;

            uint64_t scoreboardIssued = 0;
            uint64_t scoreboardCancelled = 0;
            for (std::size_t i = 0; i < decltype(scoreboard.mem_q)::COUNT_VALUE; ++i) {
                scoreboardIssued |= uint64_t(static_cast<bool>(scoreboard.mem_q[i].issued)) << i;
                scoreboardCancelled |= uint64_t(static_cast<bool>(scoreboard.mem_q[i].cancelled)) << i;
            }

            std::fprintf(stderr,
                "CPPSTALL cycle=%llu commits=%llu npc=0x%08llx "
                "fe_valid=0x%llx fe_ready=0x%llx "
                "id_q=%u,%u id_pc=0x%08llx,0x%08llx id_valid=0x%llx id_ack=0x%llx "
                "issue_valid=0x%llx issue_ack=0x%llx hs=%u stall=%u full=%u lsu_valid=0x%llx lsu_ready=%u "
                "sb_ip=%llu sb_cp=%llu,%llu sb_issued=0x%llx sb_cancel=0x%llx commit_ack=0x%llx "
                "st_state=%llu st_valid=%u st_ready=%u st_commit=%u st_commit_ready=%u "
                "st_empty=%u spec_cnt=%llu commit_cnt=%llu spec_rp=%llu spec_wp=%llu commit_rp=%llu commit_wp=%llu "
                "dc_req=%u dc_gnt=%u dc_rvalid=%u\n",
                static_cast<unsigned long long>(mainTime),
                static_cast<unsigned long long>(committedInstructions),
                static_cast<unsigned long long>(static_cast<uint64_t>(frontend.npc_q)),
                static_cast<unsigned long long>(static_cast<uint64_t>(frontend.fetch_entry_valid_o_out())),
                static_cast<unsigned long long>(static_cast<uint64_t>(id.fetch_entry_ready_o_out())),
                static_cast<unsigned>(static_cast<bool>(id.issue_q[0].valid)),
                static_cast<unsigned>(static_cast<bool>(id.issue_q[1].valid)),
                static_cast<unsigned long long>(static_cast<uint64_t>(id.issue_q[0].sbe.pc)),
                static_cast<unsigned long long>(static_cast<uint64_t>(id.issue_q[1].sbe.pc)),
                static_cast<unsigned long long>(static_cast<uint64_t>(issue.decoded_instr_valid_i_in())),
                static_cast<unsigned long long>(static_cast<uint64_t>(issue.decoded_instr_ack_o_out())),
                static_cast<unsigned>(static_cast<bool>(issue.issue_instr_hs_o_out())),
                static_cast<unsigned>(static_cast<bool>(issue.stall_issue_o_out())),
                static_cast<unsigned>(static_cast<bool>(issue.sb_full_o_out())),
                static_cast<unsigned long long>(static_cast<uint64_t>(issue.lsu_valid_o_out())),
                static_cast<unsigned>(static_cast<bool>(issue.lsu_ready_i_in())),
                static_cast<unsigned long long>(static_cast<uint64_t>(scoreboard.issue_pointer_q)),
                static_cast<unsigned long long>(static_cast<uint64_t>(scoreboard.commit_pointer_q[0])),
                static_cast<unsigned long long>(static_cast<uint64_t>(scoreboard.commit_pointer_q[1])),
                static_cast<unsigned long long>(scoreboardIssued),
                static_cast<unsigned long long>(scoreboardCancelled),
                static_cast<unsigned long long>(static_cast<uint64_t>(issue.commit_ack_i_in())),
                static_cast<unsigned long long>(static_cast<uint64_t>(store.state_q)),
                static_cast<unsigned>(static_cast<bool>(store.valid_i_in())),
                static_cast<unsigned>(static_cast<bool>(store.st_ready)),
                static_cast<unsigned>(static_cast<bool>(store.commit_i_in())),
                static_cast<unsigned>(static_cast<bool>(store.commit_ready_o_out())),
                static_cast<unsigned>(static_cast<bool>(store.store_buffer_empty_o_out())),
                static_cast<unsigned long long>(static_cast<uint64_t>(storeBuffer.speculative_status_cnt_q)),
                static_cast<unsigned long long>(static_cast<uint64_t>(storeBuffer.commit_status_cnt_q)),
                static_cast<unsigned long long>(static_cast<uint64_t>(storeBuffer.speculative_read_pointer_q)),
                static_cast<unsigned long long>(static_cast<uint64_t>(storeBuffer.speculative_write_pointer_q)),
                static_cast<unsigned long long>(static_cast<uint64_t>(storeBuffer.commit_read_pointer_q)),
                static_cast<unsigned long long>(static_cast<uint64_t>(storeBuffer.commit_write_pointer_q)),
                static_cast<unsigned>(static_cast<bool>(storeBuffer.req_port_o_out__field_data_req())),
                static_cast<unsigned>(static_cast<bool>(storeBuffer.req_port_i_in__field_data_gnt())),
                static_cast<unsigned>(static_cast<bool>(storeBuffer.req_port_i_in__field_data_rvalid())));
        }
#endif

        ++_system_clock;
        cpphdl_model_work(false);
        if (traceIrq && mainTime >= 2990 && mainTime <= 3060) {
            cpphdl_model_trace_irq(mainTime);
        }

#if 0
        if (traceFetch && mainTime >= 250 && mainTime <= 400) {
            auto& core = dut.i_ariane.i_cva6;
            auto& frontend = core.i_frontend;
            auto& cacheSubsystem = core.i_cache_subsystem;
            auto& icache = cacheSubsystem.i_cva6_icache;
            auto& cacheAxi = cacheSubsystem.i_axi_arbiter;
            std::fprintf(stderr,
                "CPPFETCH cycle=%llu npc=0x%llx fe_req=%u/0x%llx "
                "cache_req=%u/0x%llx ic_state=%llu ic_vaddr=0x%llx "
                "miss=%u/%u axi_r=%u/0x%llx refill=%u/0x%llx/%llu "
                "ic_refill=%u/0x%llx/%llu ic_rsp=%u/%u/0x%llx/0x%llx "
                "fe_rsp=%u/%u/0x%llx/0x%llx fetch_valid=%llu\n",
                static_cast<unsigned long long>(mainTime),
                static_cast<unsigned long long>(static_cast<uint64_t>(frontend.npc_q)),
                static_cast<unsigned>(static_cast<bool>(frontend.icache_dreq_o_out__field_req())),
                static_cast<unsigned long long>(static_cast<uint64_t>(frontend.icache_dreq_o_out__field_vaddr())),
                static_cast<unsigned>(static_cast<bool>(icache.dreq_i_in__field_req())),
                static_cast<unsigned long long>(static_cast<uint64_t>(icache.dreq_i_in__field_vaddr())),
                static_cast<unsigned long long>(static_cast<uint64_t>(icache.state_q)),
                static_cast<unsigned long long>(static_cast<uint64_t>(icache.vaddr_q)),
                static_cast<unsigned>(static_cast<bool>(icache.mem_data_req_o_out())),
                static_cast<unsigned>(static_cast<bool>(icache.mem_data_ack_i_in())),
                static_cast<unsigned>(static_cast<bool>(cacheAxi.axi_resp_i_in__field_r_valid())),
                static_cast<unsigned long long>(static_cast<uint64_t>(cacheAxi.axi_resp_i_in__field_r_data())),
                static_cast<unsigned>(static_cast<bool>(cacheAxi.icache_miss_resp_valid_o_out())),
                static_cast<unsigned long long>(static_cast<uint64_t>(cacheAxi.icache_miss_resp_o_out__field_data())),
                static_cast<unsigned long long>(static_cast<uint64_t>(cacheAxi.icache_miss_resp_o_out__field_rtype())),
                static_cast<unsigned>(static_cast<bool>(icache.mem_rtrn_vld_i_in())),
                static_cast<unsigned long long>(static_cast<uint64_t>(icache.mem_rtrn_i_in__field_data())),
                static_cast<unsigned long long>(static_cast<uint64_t>(icache.mem_rtrn_i_in__field_rtype())),
                static_cast<unsigned>(static_cast<bool>(icache.dreq_o_out__field_ready())),
                static_cast<unsigned>(static_cast<bool>(icache.dreq_o_out__field_valid())),
                static_cast<unsigned long long>(static_cast<uint64_t>(icache.dreq_o_out__field_vaddr())),
                static_cast<unsigned long long>(static_cast<uint64_t>(icache.dreq_o_out__field_data())),
                static_cast<unsigned>(static_cast<bool>(frontend.icache_dreq_i_in__field_ready())),
                static_cast<unsigned>(static_cast<bool>(frontend.icache_dreq_i_in__field_valid())),
                static_cast<unsigned long long>(static_cast<uint64_t>(frontend.icache_dreq_i_in__field_vaddr())),
                static_cast<unsigned long long>(static_cast<uint64_t>(frontend.icache_dreq_i_in__field_data())),
                static_cast<unsigned long long>(static_cast<uint64_t>(frontend.fetch_entry_valid_o_out())));
            if (mainTime >= 268 && mainTime <= 280) {
                auto& readAdapter = cacheAxi.i_hpdcache_mem_to_axi_read;
                auto& responseDemux = cacheAxi.i_mem_resp_read_demux;
                auto& refillMeta = cacheAxi.i_icache_refill_meta_fifo;
                auto& refillData = cacheAxi.i_icache_hpdcache_data_upsize;
                const auto demuxValid = responseDemux.mem_resp_valid_o_out();
                std::fprintf(stderr,
                    "CPPREFILL cycle=%llu expected_id=%llu "
                    "axi=%u/id%llu/last%u/data0x%llx "
                    "adapter=%u/id%llu/last%u/data0x%llx/ready%u "
                    "demux=%u/id%llu/ready%u/out%u,%u "
                    "meta=%u/%u/%u data=%u/%u/%u/last%u\n",
                    static_cast<unsigned long long>(mainTime),
                    static_cast<unsigned long long>(static_cast<uint64_t>(cacheAxi.icache_miss_id_i_in())),
                    static_cast<unsigned>(static_cast<bool>(readAdapter.axi_r_valid_i_in())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(readAdapter.axi_r_i_in__field_id())),
                    static_cast<unsigned>(static_cast<bool>(readAdapter.axi_r_i_in__field_last())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(readAdapter.axi_r_i_in__field_data())),
                    static_cast<unsigned>(static_cast<bool>(readAdapter.resp_valid_o_out())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(readAdapter.resp_o_out__field_mem_resp_r_id())),
                    static_cast<unsigned>(static_cast<bool>(readAdapter.resp_o_out__field_mem_resp_r_last())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(readAdapter.resp_o_out__field_mem_resp_r_data())),
                    static_cast<unsigned>(static_cast<bool>(readAdapter.resp_ready_i_in())),
                    static_cast<unsigned>(static_cast<bool>(responseDemux.mem_resp_valid_i_in())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(responseDemux.mem_resp_id_i_in())),
                    static_cast<unsigned>(static_cast<bool>(responseDemux.mem_resp_ready_o_out())),
                    static_cast<unsigned>(static_cast<bool>(demuxValid[0])),
                    static_cast<unsigned>(static_cast<bool>(demuxValid[1])),
                    static_cast<unsigned>(static_cast<bool>(refillMeta.w_i_in())),
                    static_cast<unsigned>(static_cast<bool>(refillMeta.wok_o_out())),
                    static_cast<unsigned>(static_cast<bool>(refillMeta.rok_o_out())),
                    static_cast<unsigned>(static_cast<bool>(refillData.w_i_in())),
                    static_cast<unsigned>(static_cast<bool>(refillData.wok_o_out())),
                    static_cast<unsigned>(static_cast<bool>(refillData.rok_o_out())),
                    static_cast<unsigned>(static_cast<bool>(refillData.wlast_i_in())));
            }
        }

        if (traceXbar && mainTime <= 1000) {
            auto& xbar = dut.i_axi_xbar.i_xbar;
            auto& demux = xbar.i_axi_demux[0];
            auto& mux = xbar.i_axi_mux[8];
            auto& prepend = mux.i_id_prepend[0];
            const auto muxWholeInputs = mux.slv_reqs_i_in();
            const auto muxInputs = mux.slv_reqs_i_in__field_ar();
            const auto prependInputs = prepend.slv_ar_chans_i_in();
            const auto prependOutputs = prepend.mst_ar_chans_o_out();
            const auto prependValid = prepend.mst_ar_valids_o_out();
            const auto arbReq = mux.i_ar_arbiter.req_i_in();
            const auto arbReqOut = mux.i_ar_arbiter.req_o_out();
            const auto arbGrantIn = mux.i_ar_arbiter.gnt_i_in();
            const auto arbGrant = mux.i_ar_arbiter.gnt_o_out();
            const auto arbIndex = mux.i_ar_arbiter.idx_o_out();
            const auto arbInputs = mux.i_ar_arbiter.data_i_in();
            const auto arbOutput = mux.i_ar_arbiter.data_o_out();
            const auto spillInput = mux.i_ar_spill_reg.data_i_in();
            const auto spillOutput = mux.i_ar_spill_reg.data_o_out();
            const auto muxOutput = mux.mst_req_o_out__field_ar();
            const auto xbarInputs = xbar.slv_ports_req_i_in();
            const auto demuxOutputs = demux.mst_reqs_o_out();
            const auto demuxSpillInput = demux.i_ar_spill_reg.data_i_in();
            const auto demuxSpillOutput = demux.i_ar_spill_reg.data_o_out();
            if (static_cast<bool>(xbarInputs[0].ar_valid) ||
                static_cast<bool>(demuxOutputs[8].ar_valid) ||
                static_cast<bool>(mux.slv_reqs_i_in__field_ar_valid()[0]) ||
                static_cast<bool>(mux.mst_req_o_out__field_ar_valid())) {
                std::fprintf(stderr,
                    "CPPXBAR cycle=%llu xbar_in=%u/0x%llx demux_out=%u/0x%llx "
                    "demux_in=%u sel=%llu spill=%u/%u/%u spill_sel=%llu/%llu projected=%u "
                    "mux_in=%u/0x%llx whole_in=%u/0x%llx prepend_in=0x%llx "
                    "prepend_out=0x%llx prepend_valid=%u arb_req=0x%llx arb_req_o=%u "
                    "arb_gnt_i=%u arb_gnt=0x%llx arb_idx=%llu arb_in=0x%llx arb_out=0x%llx "
                    "spill_in=0x%llx spill_out=0x%llx mux_out=%u/0x%llx\n",
                    static_cast<unsigned long long>(mainTime),
                    static_cast<unsigned>(static_cast<bool>(xbarInputs[0].ar_valid)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(xbarInputs[0].ar.addr)),
                    static_cast<unsigned>(static_cast<bool>(demuxOutputs[8].ar_valid)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(demuxOutputs[8].ar.addr)),
                    static_cast<unsigned>(static_cast<bool>(demux.slv_req_i_in__field_ar_valid())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(demux.slv_ar_select_i_in())),
                    static_cast<unsigned>(static_cast<bool>(demux.i_ar_spill_reg.valid_i_in())),
                    static_cast<unsigned>(static_cast<bool>(demux.i_ar_spill_reg.valid_o_out())),
                    static_cast<unsigned>(static_cast<bool>(demux.i_ar_spill_reg.ready_i_in())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(demuxSpillInput.ar_select)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(demuxSpillOutput.ar_select)),
                    static_cast<unsigned>(static_cast<bool>(demux.mst_reqs_o_out__field_ar_valid()[8])),
                    static_cast<unsigned>(static_cast<bool>(mux.slv_reqs_i_in__field_ar_valid()[0])),
                    static_cast<unsigned long long>(static_cast<uint64_t>(muxInputs[0].addr)),
                    static_cast<unsigned>(static_cast<bool>(muxWholeInputs[0].ar_valid)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(muxWholeInputs[0].ar.addr)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(prependInputs[0].addr)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(prependOutputs[0].addr)),
                    static_cast<unsigned>(static_cast<bool>(prependValid)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(arbReq)),
                    static_cast<unsigned>(static_cast<bool>(arbReqOut)),
                    static_cast<unsigned>(static_cast<bool>(arbGrantIn)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(arbGrant)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(arbIndex)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(arbInputs[0].addr)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(arbOutput.addr)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(spillInput.addr)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(spillOutput.addr)),
                    static_cast<unsigned>(static_cast<bool>(mux.mst_req_o_out__field_ar_valid())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(muxOutput.addr)));
            }

            auto& xbarDram = dut.master[ariane_soc::DRAM];
            auto& xbarRom = dut.master[ariane_soc::ROM];
            auto& romAxi = dut.i_axi2rom.slave();
            auto& atomicsSlv = dut.i_axi_riscv_atomics.slv();
            auto& atomicsMst = dut.i_axi_riscv_atomics.mst();
            auto& delayerSlv = dut.i_axi_delayer.slv();
            auto& delayerMst = dut.i_axi_delayer.mst();
            auto& memAxi = dut.i_axi2mem.slave();
            const bool romActivity = static_cast<bool>(xbarRom.ar_valid()) ||
                                     static_cast<bool>(romAxi.ar_valid()) ||
                                     static_cast<bool>(dut.i_axi2rom.req_o_out()) ||
                                     static_cast<bool>(romAxi.r_valid());
            if (romActivity) {
                std::fprintf(stderr,
                    "CPPROM cycle=%llu xbar=%u/%u/id%llu/0x%llx/%u/%u/id%llu/0x%llx "
                    "axi=%u/%u/id%llu/0x%llx/%u/%u/id%llu/qid%llu/0x%llx req=%u/0x%llx/0x%llx "
                    "boot=%u/0x%llx/0x%llx "
                    "ret_mux=%u/0x%llx->%u/0x%llx ret_demux=%u/0x%llx->%u/0x%llx "
                    "ret_core=%u/id%llu/0x%llx ret_slave=%u/id%llu/0x%llx "
                    "ret_cpu=%u/id%llu/0x%llx\n",
                    static_cast<unsigned long long>(mainTime),
                    static_cast<unsigned>(static_cast<bool>(xbarRom.ar_valid())),
                    static_cast<unsigned>(static_cast<bool>(xbarRom.ar_ready())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(xbarRom.ar_id())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(xbarRom.ar_addr())),
                    static_cast<unsigned>(static_cast<bool>(xbarRom.r_valid())),
                    static_cast<unsigned>(static_cast<bool>(xbarRom.r_ready())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(xbarRom.r_id())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(xbarRom.r_data())),
                    static_cast<unsigned>(static_cast<bool>(romAxi.ar_valid())),
                    static_cast<unsigned>(static_cast<bool>(romAxi.ar_ready())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(romAxi.ar_id())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(romAxi.ar_addr())),
                    static_cast<unsigned>(static_cast<bool>(romAxi.r_valid())),
                    static_cast<unsigned>(static_cast<bool>(romAxi.r_ready())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(romAxi.r_id())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(dut.i_axi2rom.ax_req_q.id)),
                    static_cast<unsigned long long>(static_cast<uint64_t>(romAxi.r_data())),
                    static_cast<unsigned>(static_cast<bool>(dut.i_axi2rom.req_o_out())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(dut.i_axi2rom.addr_o_out())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(dut.i_axi2rom.data_i_in())),
                    static_cast<unsigned>(static_cast<bool>(dut.i_bootrom.req_i_in())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(dut.i_bootrom.addr_i_in())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(dut.i_bootrom.rdata_o_out())),
                    static_cast<unsigned>(static_cast<bool>(mux.mst_resp_i_in__field_r_valid())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(mux.mst_resp_i_in().r.data)),
                    static_cast<unsigned>(static_cast<bool>(mux.slv_resps_o_out__field_r_valid()[0])),
                    static_cast<unsigned long long>(static_cast<uint64_t>(mux.slv_resps_o_out__field_r_data()[0])),
                    static_cast<unsigned>(static_cast<bool>(demux.mst_resps_i_in__field_r_valid()[8])),
                    static_cast<unsigned long long>(static_cast<uint64_t>(demux.mst_resps_i_in()[8].r.data)),
                    static_cast<unsigned>(static_cast<bool>(demux.slv_resp_o_out__field_r_valid())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(demux.slv_resp_o_out__field_r().data)),
                    static_cast<unsigned>(static_cast<bool>(xbar.slv_ports_resp_o_out__field_r_valid()[0])),
                    static_cast<unsigned long long>(static_cast<uint64_t>(xbar.slv_ports_resp_o_out__field_r_id()[0])),
                    static_cast<unsigned long long>(static_cast<uint64_t>(xbar.slv_ports_resp_o_out__field_r_data()[0])),
                    static_cast<unsigned>(static_cast<bool>(dut.slave[0].r_valid())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(dut.slave[0].r_id())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(dut.slave[0].r_data())),
                    static_cast<unsigned>(static_cast<bool>(dut.i_ariane.noc_resp_i_in__field_r_valid())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(dut.i_ariane.noc_resp_i_in__field_r_id())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(dut.i_ariane.noc_resp_i_in__field_r_data())));
            }
            const bool memoryActivity = static_cast<bool>(xbarDram.ar_valid()) ||
                                        static_cast<bool>(atomicsSlv.ar_valid()) ||
                                        static_cast<bool>(atomicsMst.ar_valid()) ||
                                        static_cast<bool>(delayerMst.ar_valid()) ||
                                        static_cast<bool>(memAxi.ar_valid()) ||
                                        static_cast<bool>(dut.i_axi2mem.req_o_out()) ||
                                        static_cast<bool>(memAxi.r_valid());
            if (memoryActivity) {
                std::fprintf(stderr,
                    "CPPMEM cycle=%llu "
                    "xbar=%u/%u/0x%llx/%u "
                    "atom_slv=%u/%u/0x%llx/%u atom_mst=%u/%u/0x%llx/%u "
                    "delay_slv=%u/%u/0x%llx/%u delay_mst=%u/%u/0x%llx/%u "
                    "mem_axi=%u/%u/0x%llx/%u/%u/0x%llx "
                    "mem=%u/%u/0x%llx/0x%llx sram=%u/0x%llx/0x%llx\n",
                    static_cast<unsigned long long>(mainTime),
                    static_cast<unsigned>(static_cast<bool>(xbarDram.ar_valid())),
                    static_cast<unsigned>(static_cast<bool>(xbarDram.ar_ready())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(xbarDram.ar_addr())),
                    static_cast<unsigned>(static_cast<bool>(xbarDram.r_valid())),
                    static_cast<unsigned>(static_cast<bool>(atomicsSlv.ar_valid())),
                    static_cast<unsigned>(static_cast<bool>(atomicsSlv.ar_ready())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(atomicsSlv.ar_addr())),
                    static_cast<unsigned>(static_cast<bool>(atomicsSlv.r_valid())),
                    static_cast<unsigned>(static_cast<bool>(atomicsMst.ar_valid())),
                    static_cast<unsigned>(static_cast<bool>(atomicsMst.ar_ready())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(atomicsMst.ar_addr())),
                    static_cast<unsigned>(static_cast<bool>(atomicsMst.r_valid())),
                    static_cast<unsigned>(static_cast<bool>(delayerSlv.ar_valid())),
                    static_cast<unsigned>(static_cast<bool>(delayerSlv.ar_ready())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(delayerSlv.ar_addr())),
                    static_cast<unsigned>(static_cast<bool>(delayerSlv.r_valid())),
                    static_cast<unsigned>(static_cast<bool>(delayerMst.ar_valid())),
                    static_cast<unsigned>(static_cast<bool>(delayerMst.ar_ready())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(delayerMst.ar_addr())),
                    static_cast<unsigned>(static_cast<bool>(delayerMst.r_valid())),
                    static_cast<unsigned>(static_cast<bool>(memAxi.ar_valid())),
                    static_cast<unsigned>(static_cast<bool>(memAxi.ar_ready())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(memAxi.ar_addr())),
                    static_cast<unsigned>(static_cast<bool>(memAxi.r_valid())),
                    static_cast<unsigned>(static_cast<bool>(memAxi.r_ready())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(memAxi.r_data())),
                    static_cast<unsigned>(static_cast<bool>(dut.i_axi2mem.req_o_out())),
                    static_cast<unsigned>(static_cast<bool>(dut.i_axi2mem.we_o_out())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(dut.i_axi2mem.addr_o_out())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(dut.i_axi2mem.data_i_in())),
                    static_cast<unsigned>(static_cast<bool>(dut.i_sram.req_i_in())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(dut.i_sram.addr_i_in())),
                    static_cast<unsigned long long>(static_cast<uint64_t>(dut.i_sram.rdata_o_out())));
            }
        }
#endif

        const uint32_t exit = cpphdl_model_exit();
        if (exit & 1u) {
            const uint32_t code = exit >> 1;
            if (code == 0) {
                std::fprintf(stderr, "%s *** SUCCESS *** (tohost = 0) after %llu cycles\n",
                             argv[1], static_cast<unsigned long long>(mainTime));
                return 0;
            }
            std::fprintf(stderr, "%s *** FAILED *** (tohost = %u) after %llu cycles\n",
                         argv[1], code, static_cast<unsigned long long>(mainTime));
            return static_cast<int>(code);
        }

        if ((mainTime % 2) == 0) {
            rtc = !rtc;
            cpphdl_model_set_rtc(rtc);
        }
    }

    if (traceMemory) {
        for (uint64_t address = 0x00024fc0; address < 0x00025020; address += 8) {
            const uint64_t word = cpphdl_model_read_word(address);
            std::fprintf(stderr, "CPPMEM address=0x%08llx data=0x%016llx\n",
                         static_cast<unsigned long long>(0x80000000ull + address),
                         static_cast<unsigned long long>(word));
        }
    }

    std::fprintf(stderr,
                 "%s *** FAILED *** (timeout after %llu cycles, commits=%llu, "
                 "pc=0x%08x, insn=0x%08x)\n",
                 argv[1], static_cast<unsigned long long>(mainTime),
                 static_cast<unsigned long long>(committedInstructions),
                 lastCommittedPc, lastCommittedInsn);
    return 1;
}
