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

static uint32_t expected_read(uint32_t addr)
{
    uint32_t lo = mem_word(addr >> 2);
    if ((addr & 0x3) == 0) {
        return lo;
    }
    uint32_t hi = mem_word((addr >> 2) + 1);
    return (lo >> 16) | (hi << 16);
}

class TestL1Cache : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL cache;
#else
    L1Cache<CACHE_SIZE, LINE_SIZE, WAYS, 0, ADDR_BITS> cache;
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
        cache.stall_in = __VAR(stall);
        cache.flush_in = __VAR(flush);
        cache.debugen_in = false;
        cache.__inst_name = __inst_name + "/cache";
        cache._assign();

        ram.read_in = cache.mem_read_out;
        ram.read_addr_in = cache.mem_addr_out;
        ram.write_in = cache.mem_write_out;
        ram.write_addr_in = cache.mem_addr_out;
        ram.write_data_in = cache.mem_write_data_out;
        ram.write_mask_in = cache.mem_write_mask_out;
        ram.debugen_in = false;
        ram.__inst_name = __inst_name + "/ram";
        ram._assign();
#else
        ram.read_in = __EXPR((bool)cache.mem_read_out);
        ram.read_addr_in = __EXPR((uint32_t)cache.mem_addr_out);
        ram.write_in = __EXPR((bool)cache.mem_write_out);
        ram.write_addr_in = __EXPR((uint32_t)cache.mem_addr_out);
        ram.write_data_in = __EXPR((uint32_t)cache.mem_write_data_out);
        ram.write_mask_in = __EXPR((uint8_t)cache.mem_write_mask_out);
        ram.debugen_in = false;
        ram.__inst_name = __inst_name + "/ram";
        ram._assign();
#endif
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
#ifdef VERILATOR
        return cache.busy_out;
#else
        return cache.busy_out();
#endif
    }

    bool valid()
    {
#ifdef VERILATOR
        return cache.read_valid_out;
#else
        return cache.read_valid_out();
#endif
    }

    uint32_t rdata()
    {
#ifdef VERILATOR
        return cache.read_data_out;
#else
        return cache.read_data_out();
#endif
    }

    uint32_t raddr()
    {
#ifdef VERILATOR
        return cache.read_addr_out;
#else
        return cache.read_addr_out();
#endif
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
        cache.stall_in = stall;
        cache.flush_in = flush;
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
        uint32_t expected = expected_read(request_addr);
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
        std::print("CppHDL TestL1Cache...");
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

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

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
        ok &= VerilatorCompile(__FILE__, "L1Cache", {"Predef_pkg", "RAM1PORT"},
            {"../../../../../include", "../../../../../tribe/common", "../../../../../tribe/cache"},
            CACHE_SIZE, LINE_SIZE, WAYS, 0, ADDR_BITS);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("L1Cache_1024_32_2_0_13/obj_dir/VL1Cache") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestL1Cache().run());
}

/////////////////////////////////////////////////////////////////////////

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
