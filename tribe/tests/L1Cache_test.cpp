#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include "L1Cache.h"
#include "Ram.h"

#include <chrono>
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

long sys_clock = -1;

static constexpr size_t CACHE_SIZE = 1024;
static constexpr size_t LINE_SIZE = 32;
static constexpr size_t WAYS = 2;
static constexpr size_t ADDR_BITS = 13;
static constexpr size_t RAM_WORDS = 2048;
static constexpr size_t SETS = CACHE_SIZE / LINE_SIZE / WAYS;

#ifdef VERILATOR
#define PORT_VALUE(val) val
#else
#define PORT_VALUE(val) val()
#endif
#define PORT_EXPR(val) __EXPR(PORT_VALUE(val))

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

template<int CACHE_ID>
class TestL1Cache : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL cache;
#else
    L1Cache<CACHE_SIZE, LINE_SIZE, WAYS, CACHE_ID, ADDR_BITS> cache;
#endif
    Ram<32, RAM_WORDS, 7> ram;

    bool read = false;
    bool write = false;
    uint32_t addr = 0;
    uint32_t write_data = 0;
    uint8_t write_mask = 0;
    bool stall = false;
    bool flush = false;
    bool error = false;
    uint32_t stall_prbs = 0x13579bdf;

public:
    void _assign()
    {
#ifndef VERILATOR
        cache.read_in = __VAR(read);
        cache.write_in = __VAR(write);
        cache.addr_in = __VAR(addr);
        cache.write_data_in = __VAR(write_data);
        cache.write_mask_in = __VAR(write_mask);
        cache.mem_read_data_in = ram.read_data_out;
        cache.mem_wait_in = __EXPR(false);
        cache.stall_in = __VAR(stall);
        cache.flush_in = __VAR(flush);
        cache.invalidate_in = __EXPR(false);
        cache.debugen_in = false;
        cache.__inst_name = __inst_name + "/cache";
        cache._assign();
#endif

        ram.read_in = PORT_EXPR(cache.mem_read_out);
        ram.read_addr_in = PORT_EXPR(cache.mem_addr_out);
        ram.write_in = PORT_EXPR(cache.mem_write_out);
        ram.write_addr_in = PORT_EXPR(cache.mem_addr_out);
        ram.write_data_in = PORT_EXPR(cache.mem_write_data_out);
        ram.write_mask_in = PORT_EXPR(cache.mem_write_mask_out);
        ram.debugen_in = false;
        ram.__inst_name = __inst_name + "/ram";
        ram._assign();
    }

    void preload_ram()
    {
        for (size_t i = 0; i < RAM_WORDS; ++i) {
            ram.ram.buffer[i] = mem_word(i);
        }
        ram.ram.buffer.apply();
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
        uint32_t lo = (uint32_t)ram.ram.buffer[request_addr >> 2];
        uint32_t shift = (request_addr & 0x3) * 8;
        if (shift == 0) {
            return lo;
        }
        uint32_t hi = (uint32_t)ram.ram.buffer[(request_addr >> 2) + 1];
        return (lo >> shift) | (hi << (32 - shift));
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        cache.read_in = read;
        cache.write_in = write;
        cache.addr_in = addr;
        cache.write_data_in = write_data;
        cache.write_mask_in = write_mask;
        cache.mem_read_data_in = ram.read_data_out();
        cache.mem_wait_in = false;
        cache.stall_in = stall;
        cache.flush_in = flush;
        cache.invalidate_in = false;
        cache.debugen_in = false;
        cache.clk = 1;
        cache.reset = reset;
        cache.eval();
#else
        cache._work(reset);
#endif
        ram._work(reset);
    }

    void strobe()
    {
#ifndef VERILATOR
        cache._strobe();
#endif
        ram._strobe();
    }

    void neg(bool reset)
    {
#ifdef VERILATOR
        cache.clk = 0;
        cache.reset = reset;
        cache.eval();
#else
        (void)reset;
#endif
    }

    void cycle(bool reset = false)
    {
        strobe();
        ++sys_clock;
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
            if (!valid()) {
                stall = random_stall();
                cycle(false);
            }
            if (valid() && stall) {
                check_result(phase, request_addr);
                stall = false;
                cycle(false);
            }
            if (busy()) {
                std::print("\n{} ERROR addr={:#x}: hit asserted busy\n", phase, request_addr);
                error = true;
            }
            if (!valid() || raddr() != request_addr) {
                cycle(false);
            }
            check_result(phase, request_addr);
        }
        else {
            bool saw_busy = false;
            bool got = false;
            for (size_t i = 0; i < 64 && !got; ++i) {
                stall = random_stall();
                cycle(false);
                saw_busy |= busy();
                if (valid() && raddr() == request_addr) {
                    check_result(phase, request_addr);
                    got = !stall;
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
            check_result(phase, request_addr);
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
        uint32_t request_addr = 5 * SETS * LINE_SIZE + 3 * LINE_SIZE + 2;
        read_check("focused refill addr[1]", request_addr, false);
        read_check("focused hit addr[1]", request_addr, true);
    }

    void focused_store_invalidate_check()
    {
        uint32_t request_addr = 6 * SETS * LINE_SIZE + 4 * LINE_SIZE + 8;
        read_check("focused store fill", request_addr, false);
        write_word(request_addr, 0xa55a33cc);
        read_check("focused store reload miss", request_addr, false);
        read_check("focused store reload hit", request_addr, true);
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

    void focused_checks()
    {
        focused_refill_assembly_check();
        if (!error) {
            focused_store_invalidate_check();
        }
        if (!error) {
            focused_flush_cached_hit_check();
        }
        if (!error) {
            focused_flush_miss_check();
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
        std::print("VERILATOR TestL1Cache...");
#else
        std::print("CppHDL TestL1Cache<ID={}>...", CACHE_ID);
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
template<size_t PORT_BITS>
class TestL1CacheWideRefill : public Module
{
    static constexpr int CACHE_ID = 0;
    L1Cache<CACHE_SIZE, LINE_SIZE, WAYS, CACHE_ID, ADDR_BITS, PORT_BITS> cache;

    bool read = false;
    uint32_t addr = 0;
    bool error = false;

public:
    void _assign()
    {
        cache.read_in = __VAR(read);
        cache.write_in = __EXPR(false);
        cache.addr_in = __VAR(addr);
        cache.write_data_in = __EXPR(0);
        cache.write_mask_in = __EXPR(0);
        cache.mem_read_data_in = __EXPR(mem_read_data_comb_func());
        cache.mem_wait_in = __EXPR(false);
        cache.stall_in = __EXPR(false);
        cache.flush_in = __EXPR(false);
        cache.invalidate_in = __EXPR(false);
        cache.debugen_in = false;
        cache.__inst_name = __inst_name + "/cache";
        cache._assign();
    }

    __LAZY_COMB(mem_read_data_comb, logic<PORT_BITS>)
        uint32_t base_word;
        uint32_t request_addr;
        uint32_t low;
        uint32_t high;
        uint32_t byte;
        size_t i;
        mem_read_data_comb = 0;
        request_addr = (uint32_t)cache.mem_addr_out();
        if ((request_addr & 3u) != 0 && ((request_addr & (LINE_SIZE - 1)) == LINE_SIZE - 2)) {
            // Match L2 direct cross-line behavior: assembled 32-bit data is returned in bits [31:0].
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
        ++sys_clock;
    }

    void reset_cache()
    {
        read = false;
        addr = 0;
        cycle(true);
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

    void read_check(const char* phase, uint32_t request_addr, bool expect_hit)
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
        if (data != expected) {
            std::print("\n{} ERROR addr={:#x}: data={:#x} expected={:#x}\n",
                phase, request_addr, (uint32_t)cache.read_data_out(), expected_ram_read(request_addr));
            error = true;
        }
        read = false;
        cycle(false);
    }

    bool run()
    {
        std::print("CppHDL TestL1CacheWideRefill<{}>...", PORT_BITS);
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "l1cache_wide_refill_test";
        _assign();
        reset_cache();

        uint32_t base = 3 * SETS * LINE_SIZE + 5 * LINE_SIZE;
        for (uint32_t half = 0; half < LINE_SIZE && !error; half += 2) {
            read_check("wide refill", base + half, half != 0);
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
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "L1Cache", {"Predef_pkg", "L1CachePerf_pkg", "RAM1PORT"},
            {"../../../../../include", "../../../../../tribe/common", "../../../../../tribe/cache"},
            CACHE_SIZE, LINE_SIZE, WAYS, 0, ADDR_BITS, 32);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("L1Cache_1024_32_2_0_13_32/obj_dir/VL1Cache") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

#ifdef VERILATOR
    ok = ok && TestL1Cache<0>().run();
#else
    ok = ok && TestL1Cache<0>().run();
    ok = ok && TestL1Cache<1>().run();
    ok = ok && TestL1CacheWideRefill<256>().run();
#endif
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
