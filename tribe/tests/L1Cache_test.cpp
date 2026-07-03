#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include "L1Cache.h"

#include <chrono>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <print>
#include <sstream>
#include <string>
#include "../../examples/tools.h"

using namespace cpphdl;

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static constexpr size_t LINE_SIZE = 32;
static constexpr size_t ADDR_BITS = 17;
static constexpr size_t RAM_WORDS = 32768;

#ifdef VERILATOR
#define PORT_VALUE(val) val
#define L1_MEM_READ_IN cache.mem_out___05Fread_out
#define L1_MEM_WRITE_IN cache.mem_out___05Fwrite_out
#define L1_MEM_ADDR_IN cache.mem_out___05Faddr_out
#define L1_MEM_WRITE_DATA_IN cache.mem_out___05Fwrite_data_out
#define L1_MEM_WRITE_MASK_IN cache.mem_out___05Fwrite_mask_out
#define L1_MEM_READ_DATA_OUT cache.mem_out___05Fread_data_in
#define L1_MEM_WAIT_OUT cache.mem_out___05Fwait_in
#else
#define PORT_VALUE(val) val()
#define L1_MEM_READ_IN cache.mem_out.read_in
#define L1_MEM_WRITE_IN cache.mem_out.write_in
#define L1_MEM_ADDR_IN cache.mem_out.addr_in
#define L1_MEM_WRITE_DATA_IN cache.mem_out.write_data_in
#define L1_MEM_WRITE_MASK_IN cache.mem_out.write_mask_in
#define L1_MEM_READ_DATA_OUT cache.mem_out.read_data_out
#define L1_MEM_WAIT_OUT cache.mem_out.wait_out
#endif
#define PORT_EXPR(val) _ASSIGN(PORT_VALUE(val))

static uint32_t prbs32(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static uint32_t mem_word(uint32_t word)
{
    uint32_t x = 0x12345678u ^ (word * 0x9e3779b9u);
    x = prbs32(x);
    x = prbs32(x + 0x6d2b79f5u);
    return x;
}

template<size_t CACHE_SIZE, size_t WAYS, int CACHE_ID, size_t PORT_BITS = 32>
class TestL1Cache : public Module
{
    static constexpr size_t SETS = CACHE_SIZE / LINE_SIZE / WAYS;
#ifdef VERILATOR
    VERILATOR_MODEL cache;
#else
    L1Cache<CACHE_SIZE, LINE_SIZE, WAYS, CACHE_ID, ADDR_BITS, PORT_BITS> cache;
#endif
    RAM<32, RAM_WORDS> ram;
    std::array<uint32_t, RAM_WORDS> ram_image = {};

    bool read = false;
    bool write = false;
    uint32_t addr = 0;
    uint32_t write_data = 0;
    uint8_t write_mask = 0;
    bool stall = false;
    bool flush = false;
    bool direct_mode = false;
    uint32_t direct_mem_data = 0;
    bool error = false;
    uint32_t stall_prbs = 0x13579bdf;

public:
    void _assign()
    {
#ifndef VERILATOR
        cache.read_in = _ASSIGN_REG(read);
        cache.write_in = _ASSIGN_REG(write);
        cache.addr_in = _ASSIGN_REG(addr);
        cache.write_data_in = _ASSIGN_REG(write_data);
        cache.write_mask_in = _ASSIGN_REG(write_mask);
        L1_MEM_READ_DATA_OUT = _ASSIGN(direct_mode ? (logic<32>)direct_mem_data : backing_read_data_comb_func());
        L1_MEM_WAIT_OUT = _ASSIGN(false);
        cache.stall_in = _ASSIGN_REG(stall);
        cache.flush_in = _ASSIGN_REG(flush);
        cache.invalidate_in = _ASSIGN(false);
        cache.cache_disable_in = _ASSIGN_REG(direct_mode);
        cache.debugen_in = false;
        cache.__inst_name = __inst_name + "/cache";
        cache._assign();
#endif

        ram.rd_in = PORT_EXPR(L1_MEM_READ_IN);
        ram.wr_in = PORT_EXPR(L1_MEM_WRITE_IN);
        ram.addr_in = _ASSIGN((u<clog2(RAM_WORDS)>)(PORT_VALUE(L1_MEM_ADDR_IN) >> 2));
        ram.data_in = PORT_EXPR(L1_MEM_WRITE_DATA_IN);
        ram.id_in = 7;
        ram.__inst_name = __inst_name + "/ram";
        ram._assign();
    }

    void preload_ram()
    {
        for (size_t i = 0; i < RAM_WORDS; ++i) {
            ram_image[i] = mem_word(i);
        }
    }

    void update_ram_image()
    {
        uint32_t word;
        uint32_t data;
        uint32_t mask;
        uint32_t byte;

        if (!PORT_VALUE(L1_MEM_WRITE_IN)) {
            return;
        }

        word = (uint32_t)PORT_VALUE(L1_MEM_ADDR_IN);
        data = (uint32_t)PORT_VALUE(L1_MEM_WRITE_DATA_IN);
        mask = (uint32_t)PORT_VALUE(L1_MEM_WRITE_MASK_IN);

        // Keep the byte-addressable backing image equivalent to the old
        // test-only Ram helper, including unaligned partial-word writes.
        for (byte = 0; byte < 4; ++byte) {
            uint32_t byte_addr;
            uint32_t image_word;
            uint32_t image_byte;
            uint32_t image_data;

            if ((mask & (1u << byte)) != 0) {
                byte_addr = word + byte;
                image_word = byte_addr >> 2;
                image_byte = byte_addr & 3u;
                image_data = (data >> (byte * 8u)) & 0xffu;
                if (image_word < RAM_WORDS) {
                    ram_image[image_word] = (ram_image[image_word] & ~(0xffu << (image_byte * 8u))) |
                        (image_data << (image_byte * 8u));
                }
            }
        }
    }

    bool busy()
    {
        return PORT_VALUE(cache.busy_out);
    }

    bool valid()
    {
        return PORT_VALUE(cache.read_valid_out);
    }

    uint32_t rdata()
    {
        return PORT_VALUE(cache.read_data_out);
    }

    uint32_t raddr()
    {
        return PORT_VALUE(cache.read_addr_out);
    }

    uint32_t expected_ram_read(uint32_t request_addr)
    {
        uint32_t lo = ram_image[request_addr >> 2];
        uint32_t shift = (request_addr & 0x3) * 8;
        if (shift == 0) {
            return lo;
        }
        uint32_t hi = ram_image[(request_addr >> 2) + 1];
        return (lo >> shift) | (hi << (32 - shift));
    }

    uint32_t backing_read_data_from_image(uint32_t request_addr)
    {
        return expected_ram_read(request_addr);
    }

    uint32_t backing_read_data_value()
    {
        uint32_t request_addr = PORT_VALUE(L1_MEM_ADDR_IN);
#ifdef VERILATOR
        // Verilated L1 samples mem_read_data_in on its clock edge, while the
        // C++ RAM model exposes a registered read output. Drive the backing
        // word directly from the test memory image so each refill beat matches
        // the current mem.addr_in and cannot reuse an earlier line beat.
        return backing_read_data_from_image(request_addr);
#else
        if ((request_addr & 3u) != 0 &&
            (((request_addr >> 2) & ((LINE_SIZE / 4) - 1)) == (LINE_SIZE / 4) - 1)) {
            // Instruction end-halfword fetches still use L2 direct cross-line behavior.
            return backing_read_data_from_image(request_addr);
        }
        return backing_read_data_from_image(request_addr);
#endif
    }

    _LAZY_COMB(backing_read_data_comb, logic<32>)
        return backing_read_data_comb = (logic<32>)backing_read_data_value();
    }

    void drive_cache(bool reset, bool clk)
    {
#ifdef VERILATOR
        cache.read_in = read;
        cache.write_in = write;
        cache.addr_in = addr;
        cache.write_data_in = write_data;
        cache.write_mask_in = write_mask;
        if (direct_mode) {
            L1_MEM_READ_DATA_OUT = direct_mem_data;
        } else {
            L1_MEM_READ_DATA_OUT = backing_read_data_from_image((uint32_t)L1_MEM_ADDR_IN);
        }
        L1_MEM_WAIT_OUT = false;
        cache.stall_in = stall;
        cache.flush_in = flush;
        cache.invalidate_in = false;
        cache.cache_disable_in = direct_mode;
        cache.debugen_in = false;
        cache.clk = clk;
        cache.reset = reset;
        cache.eval();
#else
        (void)reset;
        (void)clk;
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        drive_cache(reset, false);
        ram._work(reset);
        drive_cache(reset, true);
#else
        cache._work(reset);
        ram._work(reset);
#endif
    }

    void strobe()
    {
        update_ram_image();
#ifndef VERILATOR
        cache._strobe();
#endif
        ram._strobe();
    }

    void neg(bool reset)
    {
#ifdef VERILATOR
        drive_cache(reset, false);
#else
        (void)reset;
#endif
    }

    void cycle(bool reset = false)
    {
        strobe();
        ++_system_clock;
        eval(reset);
        neg(reset);
    }

    bool random_stall()
    {
        stall_prbs = prbs32(stall_prbs + 0x9e3779b9u);
        return (stall_prbs & 0x7) == 0;
    }

    void reset_cache()
    {
        read = false;
        write = false;
        addr = 0;
        stall = false;
        flush = false;
        cycle(true);
        cycle(false);
        for (size_t i = 0; i < SETS + 8 && busy(); ++i) {
            cycle(false);
        }
        if (busy()) {
            std::print("\nERROR: cache did not leave init state\n");
            error = true;
        }
    }

    void idle()
    {
        read = false;
        stall = false;
        for (size_t i = 0; i < 8 && (busy() || valid()); ++i) {
            cycle(false);
        }
    }

    void check_result(const char* phase, uint32_t request_addr)
    {
        uint32_t expected = expected_ram_read(request_addr);
        if (!valid() || raddr() != request_addr || rdata() != expected) {
            std::print("\n{} ERROR addr={:#x}: valid={} raddr={:#x} data={:#x} expected={:#x} busy={}\n",
                phase, request_addr, valid(), raddr(), rdata(), expected, busy());
            error = true;
        }
    }

    void read_check(const char* phase, uint32_t request_addr, bool expect_hit)
    {
        idle();
        addr = request_addr;
        read = true;
        stall = false;
        cycle(false);

        if (expect_hit) {
            bool got = valid() && raddr() == request_addr;
            for (size_t i = 0; i < 8 && !got; ++i) {
                stall = false;
                cycle(false);
                got = valid() && raddr() == request_addr;
                if (got && stall) {
                    check_result(phase, request_addr);
                    stall = false;
                    cycle(false);
                }
            }
            if (!got) {
                std::print("\n{} ERROR addr={:#x}: hit response was not returned\n", phase, request_addr);
                error = true;
            }
            check_result(phase, request_addr);
        }
        else {
            bool saw_busy = false;
            bool got = false;
            bool checked = false;
            for (size_t i = 0; i < 64 && !got; ++i) {
                stall = false;
                cycle(false);
                saw_busy |= busy();
                if (valid() && raddr() == request_addr) {
                    check_result(phase, request_addr);
                    checked = true;
                    if (stall) {
                        stall = false;
                        cycle(false);
                    }
                    got = true;
                }
            }
            if (!got) {
                stall = false;
                for (size_t i = 0; i < 64 && !(valid() && raddr() == request_addr); ++i) {
                    cycle(false);
                }
            }
            if (!saw_busy) {
                std::print("\n{} ERROR addr={:#x}: miss did not assert busy\n", phase, request_addr);
                error = true;
            }
            if (!checked) {
                check_result(phase, request_addr);
            }
        }

        stall = false;
        read = false;
        cycle(false);
    }

    void write_word(uint32_t request_addr, uint32_t data)
    {
        idle();
        addr = request_addr;
        write_data = data;
        write_mask = 0xF;
        write = true;
        read = false;
        stall = false;
        flush = false;
        cycle(false);
        write = false;
        write_mask = 0;
        cycle(false);
    }

    void focused_refill_assembly_check()
    {
        for (uint32_t byte = 1; byte < 4; ++byte) {
            uint32_t line_base = (12 + byte) * SETS * LINE_SIZE + (3 + byte) * LINE_SIZE;
            uint32_t request_addr = line_base + byte;
            read_check("focused refill byte offset", request_addr, false);
            read_check("focused hit byte offset", request_addr, true);
        }
    }

    void focused_store_invalidate_check()
    {
        uint32_t request_addr = 6 * SETS * LINE_SIZE + 4 * LINE_SIZE + 8;
        read_check("focused store fill", request_addr, false);
        write_word(request_addr, 0xa55a33cc);
        read_check("focused store reload miss", request_addr, false);
        read_check("focused store reload hit", request_addr, true);
    }

    void focused_cached_hit_stall_hold_check()
    {
        uint32_t request_addr = 12 * SETS * LINE_SIZE + 4 * LINE_SIZE + 8;
        read_check("focused stalled hit fill", request_addr, false);
        idle();

        addr = request_addr;
        read = true;
        stall = false;
        cycle(false);

        stall = true;
        bool held = false;
        for (size_t i = 0; i < 4; ++i) {
            cycle(false);
            if (valid() && raddr() == request_addr && rdata() == expected_ram_read(request_addr)) {
                held = true;
            }
            else {
                std::print("\nfocused stalled hit ERROR cycle={} addr={:#x}: valid={} raddr={:#x} data={:#x} expected={:#x} busy={}\n",
                    i, request_addr, valid(), raddr(), rdata(), expected_ram_read(request_addr), busy());
                error = true;
                break;
            }
        }

        stall = false;
        cycle(false);
        read = false;
        cycle(false);
        if (!held) {
            std::print("\nfocused stalled hit ERROR response was not held under stall\n");
            error = true;
        }
    }

    void focused_flush_cached_hit_check()
    {
        uint32_t old_addr = 7 * SETS * LINE_SIZE + 5 * LINE_SIZE;
        uint32_t target_addr = 8 * SETS * LINE_SIZE + 6 * LINE_SIZE + 2;
        read_check("focused flush target fill", target_addr, false);
        idle();

        addr = old_addr;
        read = true;
        stall = false;
        flush = false;
        cycle(false);

        addr = target_addr;
        stall = true;
        flush = true;
        cycle(false);

        flush = false;
        stall = false;
        cycle(false);

        if (busy()) {
            std::print("\nfocused flush cached hit ERROR addr={:#x}: redirect hit asserted busy\n", target_addr);
            error = true;
        }
        if (!valid()) {
            cycle(false);
        }
        check_result("focused flush cached hit", target_addr);

        read = false;
        cycle(false);
    }

    void focused_flush_miss_check()
    {
        uint32_t old_addr = 9 * SETS * LINE_SIZE + 7 * LINE_SIZE;
        uint32_t target_addr = 10 * SETS * LINE_SIZE + 8 * LINE_SIZE + 2;
        idle();

        addr = old_addr;
        read = true;
        stall = false;
        flush = false;
        cycle(false);

        addr = target_addr;
        stall = true;
        flush = true;
        cycle(false);

        flush = false;
        stall = false;

        bool got = false;
        bool saw_old = false;
        bool saw_busy = false;
        for (size_t i = 0; i < 80 && !got; ++i) {
            cycle(false);
            saw_busy |= busy();
            if (valid() && raddr() == old_addr) {
                saw_old = true;
            }
            if (valid() && raddr() == target_addr) {
                check_result("focused flush miss", target_addr);
                got = true;
            }
        }

        if (saw_old || !saw_busy || !got) {
            std::print("\nfocused flush miss ERROR old_seen={} saw_busy={} got={} target={:#x} valid={} raddr={:#x}\n",
                saw_old, saw_busy, got, target_addr, valid(), raddr());
            error = true;
        }

        read = false;
        cycle(false);
    }

    void focused_uncached_repeated_poll_check()
    {
        uint32_t request_addr = 0x700;
        idle();

        direct_mode = true;
        direct_mem_data = 1;
        addr = request_addr;
        read = true;
        stall = false;
        cycle(false);

        bool got_first = false;
        for (size_t i = 0; i < 32 && !got_first; ++i) {
            if (valid() && raddr() == request_addr) {
                got_first = true;
                if (rdata() != 1) {
                    std::print("\nuncached repeated poll ERROR first data={:#x}\n", rdata());
                    error = true;
                }
                break;
            }
            cycle(false);
        }

        // Regression for Linux PLIC claim polling: after one uncached read of
        // claim=1, a following same-address claim read must not retire the old
        // response while the new MMIO access is still being issued.
        direct_mem_data = 0;
        bool saw_second_request = false;
        bool got_second = false;
        for (size_t i = 0; i < 64 && !got_second; ++i) {
            cycle(false);
            if (PORT_VALUE(L1_MEM_READ_IN) && PORT_VALUE(L1_MEM_ADDR_IN) == request_addr) {
                saw_second_request = true;
            }
            if (valid() && raddr() == request_addr) {
                if (!saw_second_request && rdata() == 1) {
                    std::print("\nuncached repeated poll ERROR stale same-address data returned before second MMIO request\n");
                    error = true;
                    break;
                }
                if (saw_second_request && rdata() == 0) {
                    got_second = true;
                }
                else if (saw_second_request && rdata() != 0) {
                    std::print("\nuncached repeated poll ERROR second data={:#x} expected=0\n", rdata());
                    error = true;
                    break;
                }
            }
        }

        if (!got_first || !saw_second_request || !got_second) {
            std::print("\nuncached repeated poll ERROR got_first={} saw_second_request={} got_second={} valid={} raddr={:#x} data={:#x} busy={}\n",
                got_first, saw_second_request, got_second, valid(), raddr(), rdata(), busy());
            error = true;
        }

        // Same scenario with the first MMIO response held under downstream
        // stall. Releasing the stall while polling the same address must not
        // let the held value masquerade as a fresh device read.
        idle();
        direct_mode = true;
        direct_mem_data = 1;
        addr = request_addr;
        read = true;
        stall = false;
        cycle(false);
        got_first = false;
        for (size_t i = 0; i < 32 && !got_first; ++i) {
            cycle(false);
            if (valid() && raddr() == request_addr) {
                got_first = true;
            }
        }
        if (!got_first) {
            std::print("\nuncached stalled poll ERROR first response was not seen\n");
            error = true;
        }
        stall = true;
        for (size_t i = 0; i < 3; ++i) {
            cycle(false);
        }
        direct_mem_data = 0;
        stall = false;
        saw_second_request = false;
        got_second = false;
        for (size_t i = 0; i < 64 && !got_second; ++i) {
            cycle(false);
            if (PORT_VALUE(L1_MEM_READ_IN) && PORT_VALUE(L1_MEM_ADDR_IN) == request_addr) {
                saw_second_request = true;
            }
            if (valid() && raddr() == request_addr) {
                if (!saw_second_request && rdata() == 1) {
                    std::print("\nuncached stalled poll ERROR held value reused before second MMIO request\n");
                    error = true;
                    break;
                }
                if (saw_second_request && rdata() == 0) {
                    got_second = true;
                }
                else if (saw_second_request && rdata() != 0) {
                    std::print("\nuncached stalled poll ERROR second data={:#x} expected=0\n", rdata());
                    error = true;
                    break;
                }
            }
        }
        if (!saw_second_request || !got_second) {
            std::print("\nuncached stalled poll ERROR saw_second_request={} got_second={} valid={} raddr={:#x} data={:#x} busy={}\n",
                saw_second_request, got_second, valid(), raddr(), rdata(), busy());
            error = true;
        }

        read = false;
        direct_mode = false;
        stall = false;
        cycle(false);
    }

    void focused_instruction_end_halfword_check()
    {
        if constexpr (CACHE_ID != 0) {
            return;
        }

        uint32_t request_addr = 11 * SETS * LINE_SIZE + 9 * LINE_SIZE + LINE_SIZE - 2;
        uint32_t line_base = request_addr & ~(uint32_t)(LINE_SIZE - 1);
        uint32_t expected = expected_ram_read(request_addr);
        bool got = false;
        bool saw_busy = false;
        bool saw_line_refill = false;
        bool saw_direct_cross_line = false;

        idle();
        addr = request_addr;
        read = true;
        stall = false;
        direct_mode = false;
        cycle(false);

        for (size_t i = 0; i < 80 && !got; ++i) {
            saw_busy |= busy();
            if (PORT_VALUE(L1_MEM_READ_IN)) {
                uint32_t mem_addr = PORT_VALUE(L1_MEM_ADDR_IN);
                saw_line_refill |= mem_addr == line_base;
                saw_direct_cross_line |= mem_addr == request_addr;
            }
            if (valid() && raddr() == request_addr) {
                got = true;
                break;
            }
            cycle(false);
        }

        if (!got || !saw_busy || saw_line_refill || !saw_direct_cross_line || rdata() != expected) {
            std::print("\ninstruction end-halfword direct ERROR addr={:#x}: got={} saw_busy={} saw_line_refill={} saw_direct_cross_line={} data={:#x} expected={:#x} valid={} raddr={:#x} busy={}\n",
                request_addr, got, saw_busy, saw_line_refill, saw_direct_cross_line,
                rdata(), expected, valid(), raddr(), busy());
            error = true;
        }

        read = false;
        cycle(false);
        idle();

        addr = request_addr;
        read = true;
        stall = false;
        bool saw_mem_on_hit = false;
        got = false;
        cycle(false);
        for (size_t i = 0; i < 8 && !got; ++i) {
            saw_mem_on_hit |= PORT_VALUE(L1_MEM_READ_IN);
            if (valid() && raddr() == request_addr) {
                got = true;
                break;
            }
            cycle(false);
        }

        if (!got || !saw_mem_on_hit || rdata() != expected) {
            std::print("\ninstruction end-halfword repeat-direct ERROR addr={:#x}: got={} saw_mem_on_hit={} data={:#x} expected={:#x} valid={} raddr={:#x} busy={}\n",
                request_addr, got, saw_mem_on_hit, rdata(), expected, valid(), raddr(), busy());
            error = true;
        }

        read = false;
        cycle(false);
    }

    void focused_checks()
    {
        focused_refill_assembly_check();
        if (!error) {
            focused_store_invalidate_check();
        }
        if (!error) {
            focused_cached_hit_stall_hold_check();
        }
        if (!error) {
            focused_flush_cached_hit_check();
        }
        if (!error) {
            focused_flush_miss_check();
        }
        if (!error) {
            focused_uncached_repeated_poll_check();
        }
        if (!error) {
            focused_instruction_end_halfword_check();
        }
    }

    uint32_t line_addr(uint32_t tag, uint32_t set, uint32_t salt)
    {
        uint32_t half_offset = ((salt * 7 + set * 5 + tag * 3) % 15) * 2;
        if (half_offset == 30) {
            half_offset = 28;
        }
        return tag * SETS * LINE_SIZE + set * LINE_SIZE + half_offset;
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestL1Cache<SIZE={},WAYS={},ID={},PORT_BITS={}>...", CACHE_SIZE, WAYS, CACHE_ID, PORT_BITS);
#else
        std::print("CppHDL TestL1Cache<SIZE={},WAYS={},ID={},PORT_BITS={}>...", CACHE_SIZE, WAYS, CACHE_ID, PORT_BITS);
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "l1cache_test";
        _assign();
        preload_ram();
        reset_cache();

        for (uint32_t set = 0; set < SETS && !error; ++set) {
            read_check("intro miss", line_addr(0, set, 1), false);
        }
        for (uint32_t n = 0; n < 96 && !error; ++n) {
            uint32_t set = prbs32(n + 1) % SETS;
            read_check("cached hit", line_addr(0, set, n), true);
        }
        for (uint32_t set = 0; set < SETS && !error; ++set) {
            read_check("second tag miss", line_addr(1, set, 3), false);
        }
        for (uint32_t n = 0; n < 96 && !error; ++n) {
            uint32_t set = prbs32(0x100 + n) % SETS;
            read_check("second tag hit", line_addr(1, set, n), true);
        }
        for (uint32_t set = 0; set < SETS && !error; ++set) {
            read_check("third tag miss", line_addr(2, set, 5), false);
        }
        if (!error) {
            focused_checks();
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

#ifndef VERILATOR
template<size_t CACHE_SIZE, size_t WAYS, size_t PORT_BITS>
class TestL1CacheWideRefill : public Module
{
    static constexpr size_t SETS = CACHE_SIZE / LINE_SIZE / WAYS;
    static constexpr int CACHE_ID = 0;
    static constexpr size_t PORT_BYTES = PORT_BITS / 8;
    static constexpr size_t REFILL_BEATS = LINE_SIZE / PORT_BYTES;
    L1Cache<CACHE_SIZE, LINE_SIZE, WAYS, CACHE_ID, ADDR_BITS, PORT_BITS> cache;

    bool read = false;
    uint32_t addr = 0;
    bool error = false;

public:
    void _assign()
    {
        cache.read_in = _ASSIGN_REG(read);
        cache.write_in = _ASSIGN(false);
        cache.addr_in = _ASSIGN_REG(addr);
        cache.write_data_in = _ASSIGN(0);
        cache.write_mask_in = _ASSIGN(0);
        L1_MEM_READ_DATA_OUT = _ASSIGN(mem_read_data_comb_func());
        L1_MEM_WAIT_OUT = _ASSIGN(false);
        cache.stall_in = _ASSIGN(false);
        cache.flush_in = _ASSIGN(false);
        cache.invalidate_in = _ASSIGN(false);
        cache.cache_disable_in = _ASSIGN(false);
        cache.debugen_in = false;
        cache.__inst_name = __inst_name + "/cache";
        cache._assign();
    }

    _LAZY_COMB(mem_read_data_comb, logic<PORT_BITS>)
        uint32_t base_word;
        uint32_t request_addr;
        uint32_t low;
        uint32_t high;
        uint32_t byte;
        size_t i;
        mem_read_data_comb = 0;
        request_addr = (uint32_t)L1_MEM_ADDR_IN();
        if ((request_addr & 3u) != 0 &&
            (((request_addr % (PORT_BITS / 8)) / 4u) + 1 >= (PORT_BITS / 32))) {
            // Match L2 direct cross-beat behavior: assembled 32-bit data is returned in bits [31:0].
            byte = request_addr & 3u;
            low = mem_word(request_addr >> 2);
            high = mem_word((request_addr >> 2) + 1);
            mem_read_data_comb.bits(31, 0) = (low >> (byte * 8u)) | (high << (32u - byte * 8u));
        }
        else {
            base_word = (request_addr & ~(uint32_t)((PORT_BITS / 8) - 1)) >> 2;
            for (i = 0; i < PORT_BITS / 32; ++i) {
                mem_read_data_comb.bits(i * 32 + 31, i * 32) = mem_word(base_word + i);
            }
        }
        return mem_read_data_comb;
    }

    uint32_t expected_ram_read(uint32_t request_addr)
    {
        uint32_t lo = mem_word(request_addr >> 2);
        uint32_t shift = (request_addr & 0x3) * 8;
        if (shift == 0) {
            return lo;
        }
        uint32_t hi = mem_word((request_addr >> 2) + 1);
        return (lo >> shift) | (hi << (32 - shift));
    }

    void cycle(bool reset = false)
    {
        cache._work(reset);
        cache._strobe();
        ++_system_clock;
    }

    void reset_cache()
    {
        read = false;
        addr = 0;
        cycle(true);
        cycle(false);
        for (size_t i = 0; i < SETS + 8 && cache.busy_out(); ++i) {
            cycle(false);
        }
        if (cache.busy_out()) {
            std::print("\nERROR: wide cache did not leave init state\n");
            error = true;
        }
    }

    void idle()
    {
        read = false;
        for (size_t i = 0; i < 8 && (cache.busy_out() || cache.read_valid_out()); ++i) {
            cycle(false);
        }
    }

    void read_check(const char* phase, uint32_t request_addr, bool expect_hit, uint32_t data_mask = 0xffffffffu)
    {
        bool saw_busy = false;
        bool got = false;
        idle();
        addr = request_addr;
        read = true;
        cycle(false);
        for (size_t i = 0; i < 80 && !got; ++i) {
            saw_busy |= cache.busy_out();
            if (cache.read_valid_out() && (uint32_t)cache.read_addr_out() == request_addr) {
                got = true;
                break;
            }
            cycle(false);
        }
        if (!got || (!expect_hit && !saw_busy)) {
            std::print("\n{} ERROR addr={:#x}: got={} saw_busy={} valid={} raddr={:#x} busy={}\n",
                phase, request_addr, got, saw_busy, (bool)cache.read_valid_out(),
                (uint32_t)cache.read_addr_out(), (bool)cache.busy_out());
            error = true;
        }
        uint32_t data = (uint32_t)cache.read_data_out();
        uint32_t expected = expected_ram_read(request_addr);
        if ((request_addr & (LINE_SIZE - 1)) == LINE_SIZE - 2) {
            data &= 0xffffu;
            expected &= 0xffffu;
        }
        if ((data & data_mask) != (expected & data_mask)) {
            std::print("\n{} ERROR addr={:#x}: data={:#x} expected={:#x}\n",
                phase, request_addr, (uint32_t)cache.read_data_out(), expected_ram_read(request_addr));
            error = true;
        }
        read = false;
        cycle(false);
    }

    void requested_beat_hold_regression()
    {
        if constexpr (REFILL_BEATS > 1) {
            // Linux restores return addresses from the stack during a multi-beat D-cache refill.
            // The requested word can arrive before the final refill beat, so L1 must hold that
            // requested beat and return it when ST_DONE is reached, not the last beat on the port.
            uint32_t base = 11 * SETS * LINE_SIZE + 7 * LINE_SIZE;
            uint32_t request_addr = base + PORT_BYTES;
            read_check("wide requested-beat hold", request_addr, false);
            read_check("wide requested-beat hold hit", request_addr, true);
        }
    }

    void final_word_byte_direct_regression()
    {
        // Scenario from CPU byte-copy: odd byte loads from the final 32-bit word
        // must stay beat-aligned so L2 can return dirty cached data. The CPU only
        // consumes the low byte for LBU; full cross-line words are split earlier.
        uint32_t base = 13 * SETS * LINE_SIZE + 3 * LINE_SIZE;
        uint32_t request_addr = base + LINE_SIZE - 3;
        read_check("wide final-word byte direct", request_addr, false, 0xffu);
    }

    void cross_beat_direct_word_regression()
    {
        // Linux's __riscv_copy_words_unaligned uses LW from odd addresses. When
        // such an LW starts at the last 32-bit lane of an L2 beat, L1 must ask L2
        // for a cross-beat direct read instead of wrapping within the same beat.
        if constexpr (PORT_BITS > 32) {
            uint32_t base = 15 * SETS * LINE_SIZE + 5 * LINE_SIZE;
            uint32_t request_addr = base + PORT_BYTES - 1;
            read_check("wide cross-beat direct word", request_addr, false);
        }
    }

    bool run()
    {
        std::print("CppHDL TestL1CacheWideRefill<SIZE={},WAYS={},PORT_BITS={}>...", CACHE_SIZE, WAYS, PORT_BITS);
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "l1cache_wide_refill_test";
        _assign();
        reset_cache();

        uint32_t base = 3 * SETS * LINE_SIZE + 5 * LINE_SIZE;
        for (uint32_t half = 0; half < LINE_SIZE && !error; half += 2) {
            read_check("wide refill", base + half, half != 0);
        }
        if (!error) {
            read_check("wide cached odd byte", base + 1, true);
        }
        if (!error) {
            requested_beat_hold_regression();
        }
        if (!error) {
            final_word_byte_direct_regression();
        }
        if (!error) {
            cross_beat_direct_word_regression();
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};
#endif

int main(int argc, char** argv)
{
    bool noveril = false;
    int only = -1;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        if (argv[i][0] != '-') {
            only = atoi(argv[i]);
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        const auto source_root = CpphdlSourceRootFrom(__FILE__);
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        auto compile_l1 = [&](size_t cache_size, size_t ways, int id, size_t port_bits) {
            return VerilatorCompile(__FILE__, "L1Cache", {"Predef_pkg", "L1CachePerf_pkg", "RAM"},
                {(source_root / "include").string(),
                 (source_root / "tribe" / "common").string(),
                 (source_root / "tribe" / "cache").string()},
                cache_size, LINE_SIZE, ways, id, ADDR_BITS, port_bits);
        };
        ok &= compile_l1(1024, 1, 0, 32);
        ok &= compile_l1(1024, 2, 0, 32);
        ok &= compile_l1(1024, 4, 0, 32);
        ok &= compile_l1(8192, 1, 0, 32);
        ok &= compile_l1(8192, 2, 0, 32);
        ok &= compile_l1(8192, 4, 0, 32);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok &&
            std::system("L1Cache_1024_32_1_0_17_32/obj_dir/VL1Cache 0") == 0 &&
            std::system("L1Cache_1024_32_2_0_17_32/obj_dir/VL1Cache 1") == 0 &&
            std::system("L1Cache_1024_32_4_0_17_32/obj_dir/VL1Cache 2") == 0 &&
            std::system("L1Cache_8192_32_1_0_17_32/obj_dir/VL1Cache 3") == 0 &&
            std::system("L1Cache_8192_32_2_0_17_32/obj_dir/VL1Cache 4") == 0 &&
            std::system("L1Cache_8192_32_4_0_17_32/obj_dir/VL1Cache 5") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && ((only != -1 && only != 0) || TestL1Cache<1024,1,0>().run());
    ok = ok && ((only != -1 && only != 1) || TestL1Cache<1024,2,0>().run());
    ok = ok && ((only != -1 && only != 2) || TestL1Cache<1024,4,0>().run());
    ok = ok && ((only != -1 && only != 3) || TestL1Cache<8192,1,0>().run());
    ok = ok && ((only != -1 && only != 4) || TestL1Cache<8192,2,0>().run());
    ok = ok && ((only != -1 && only != 5) || TestL1Cache<8192,4,0>().run());
#ifndef VERILATOR
    ok = ok && TestL1Cache<1024,1,1>().run();
    ok = ok && TestL1Cache<1024,2,1>().run();
    ok = ok && TestL1Cache<1024,4,1>().run();
    ok = ok && TestL1Cache<8192,1,1>().run();
    ok = ok && TestL1Cache<8192,2,1>().run();
    ok = ok && TestL1Cache<8192,4,1>().run();
    ok = ok && TestL1CacheWideRefill<1024,1,64>().run();
    ok = ok && TestL1CacheWideRefill<1024,2,64>().run();
    ok = ok && TestL1CacheWideRefill<1024,4,64>().run();
    ok = ok && TestL1CacheWideRefill<8192,1,256>().run();
    ok = ok && TestL1CacheWideRefill<8192,2,256>().run();
    ok = ok && TestL1CacheWideRefill<8192,4,256>().run();
#endif
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
