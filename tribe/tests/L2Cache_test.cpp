#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#ifndef L2CACHE_TEST_DUT_HEADER
#define L2CACHE_TEST_DUT_HEADER "l2/L2Cache.h"
#endif
#include L2CACHE_TEST_DUT_HEADER
#include "Axi4Ram.h"
#include "Config.h"

#include <chrono>
#include <cstdlib>
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

template<size_t WIDTH, typename T>
static logic<WIDTH> copy_to_logic(const T& bits)
{
    logic<WIDTH> out = 0;
    const size_t copy_bytes = sizeof(bits) < sizeof(out.bytes) ? sizeof(bits) : sizeof(out.bytes);
    memcpy(out.bytes, &bits, copy_bytes);
    return out;
}

#ifdef VERILATOR
template<size_t WIDTH, size_t WORDS>
static void verilator_logic_to_wide(VlWide<WORDS>& out, const logic<WIDTH>& bits)
{
    static_assert(WIDTH <= WORDS * 32);
    memset(out.m_storage, 0, sizeof(out.m_storage));
    memcpy(out.m_storage, bits.bytes, sizeof(bits.bytes));
}

template<size_t WIDTH, size_t WORDS>
static void verilator_logic_to_wide(WData (&out)[WORDS], const logic<WIDTH>& bits)
{
    static_assert(WIDTH <= WORDS * 32);
    memset(out, 0, sizeof(out));
    memcpy(out, bits.bytes, sizeof(bits.bytes));
}

static void verilator_logic_to_wide(QData& out, const logic<64>& bits)
{
    memcpy(&out, bits.bytes, sizeof(out));
}

static void verilator_logic_to_wide(uint32_t& out, const logic<32>& bits)
{
    memcpy(&out, bits.bytes, sizeof(out));
}

template<size_t WORDS>
static uint32_t port_word(const VlWide<WORDS>& bits, size_t word)
{
    return bits.m_storage[word];
}

template<size_t WORDS>
static uint32_t port_word(const WData (&bits)[WORDS], size_t word)
{
    return bits[word];
}

static uint32_t port_word(QData bits, size_t word)
{
    return (uint32_t)(bits >> (word * 32));
}

static uint32_t port_word(uint32_t bits, size_t word)
{
    return word == 0 ? bits : 0;
}
#else
template<size_t WIDTH>
static uint32_t port_word(logic<WIDTH> bits, size_t word)
{
    return (uint32_t)bits.bits(word * 32 + 31, word * 32);
}
#endif

long _system_clock = -1;

static constexpr size_t LINE_SIZE = 32;
static constexpr size_t WAIT_LIMIT = 128;

#ifndef L2CACHE_TEST_DUT
#define L2CACHE_TEST_DUT L2Cache
#endif

#ifndef L2CACHE_TEST_TOP_NAME
#define L2CACHE_TEST_TOP_NAME "L2Cache"
#endif

#ifndef L2CACHE_TEST_SOURCE_FILE
#define L2CACHE_TEST_SOURCE_FILE __FILE__
#endif

#ifdef VERILATOR
#define PORT_VALUE(val) val
#define L2_VALUE(val) (eval_l2(false), val)
#define L2_I_READ_IN l2.i_mem_in___05Fread_in[0]
#define L2_I_WRITE_IN l2.i_mem_in___05Fwrite_in[0]
#define L2_I_ADDR_IN l2.i_mem_in___05Faddr_in[0]
#define L2_I_WRITE_DATA_IN l2.i_mem_in___05Fwrite_data_in[0]
#define L2_I_WRITE_MASK_IN l2.i_mem_in___05Fwrite_mask_in[0]
#define L2_I_CACHE_DISABLE_IN l2.i_mem_in___05Fcache_disable_in[0]
#define L2_I_READ_DATA_OUT l2.i_mem_in___05Fread_data_out[0]
#define L2_I_WAIT_OUT l2.i_mem_in___05Fwait_out[0]
#define L2_D_READ_IN l2.d_mem_in___05Fread_in[0]
#define L2_D_WRITE_IN l2.d_mem_in___05Fwrite_in[0]
#define L2_D_ADDR_IN l2.d_mem_in___05Faddr_in[0]
#define L2_D_WRITE_DATA_IN l2.d_mem_in___05Fwrite_data_in[0]
#define L2_D_WRITE_MASK_IN l2.d_mem_in___05Fwrite_mask_in[0]
#define L2_D_CACHE_DISABLE_IN l2.d_mem_in___05Fcache_disable_in[0]
#define L2_D_READ_DATA_OUT l2.d_mem_in___05Fread_data_out[0]
#define L2_D_WAIT_OUT l2.d_mem_in___05Fwait_out[0]
#define L2_I_READ_DATA_OUT_AT(index) l2.i_mem_in___05Fread_data_out[index]
#define L2_I_WAIT_OUT_AT(index) l2.i_mem_in___05Fwait_out[index]
#define L2_D_WAIT_OUT_AT(index) l2.d_mem_in___05Fwait_out[index]
#else
#define PORT_VALUE(val) val()
#define L2_VALUE(val) val()
#define L2_I_READ_IN l2.i_mem_in[0].read_in
#define L2_I_WRITE_IN l2.i_mem_in[0].write_in
#define L2_I_ADDR_IN l2.i_mem_in[0].addr_in
#define L2_I_WRITE_DATA_IN l2.i_mem_in[0].write_data_in
#define L2_I_WRITE_MASK_IN l2.i_mem_in[0].write_mask_in
#define L2_I_CACHE_DISABLE_IN l2.i_mem_in[0].cache_disable_in
#define L2_I_READ_DATA_OUT l2.i_mem_in[0].read_data_out
#define L2_I_WAIT_OUT l2.i_mem_in[0].wait_out
#define L2_D_READ_IN l2.d_mem_in[0].read_in
#define L2_D_WRITE_IN l2.d_mem_in[0].write_in
#define L2_D_ADDR_IN l2.d_mem_in[0].addr_in
#define L2_D_WRITE_DATA_IN l2.d_mem_in[0].write_data_in
#define L2_D_WRITE_MASK_IN l2.d_mem_in[0].write_mask_in
#define L2_D_CACHE_DISABLE_IN l2.d_mem_in[0].cache_disable_in
#define L2_D_READ_DATA_OUT l2.d_mem_in[0].read_data_out
#define L2_D_WAIT_OUT l2.d_mem_in[0].wait_out
#define L2_I_READ_DATA_OUT_AT(index) l2.i_mem_in[index].read_data_out
#define L2_I_WAIT_OUT_AT(index) l2.i_mem_in[index].wait_out
#define L2_D_WAIT_OUT_AT(index) l2.d_mem_in[index].wait_out
#endif
#define PORT_EXPR(val) _ASSIGN(PORT_VALUE(val))

struct L2CpuTestInput
{
    bool i_read;
    bool d_read;
    bool d_write;
    uint32_t i_addr;
    uint32_t d_addr;
    uint32_t write_data;
    uint8_t write_mask;
};

template<size_t L2_SIZE, size_t PORT_BITS, size_t MEM_PORTS, size_t WAYS, size_t CPU_PORTS>
class TestL2Cache : public Module
{
    static constexpr size_t SETS = L2_SIZE / LINE_SIZE / WAYS;
    static constexpr uint64_t REGION_SIZE64 = 0x100000000ull / MEM_PORTS;
    static constexpr uint32_t REGION_SIZE = (uint32_t)REGION_SIZE64;
    static constexpr uint32_t PRBS_REGION_SIZE = 2u * 1024u * 1024u;
    static constexpr uint32_t PRBS_TOTAL_SIZE = 3u * PRBS_REGION_SIZE;
    static constexpr size_t RAM_DEPTH_PER_PORT = PRBS_REGION_SIZE / (PORT_BITS / 8);
    static constexpr size_t DEVICE_PORT = MEM_PORTS - 1;
#ifdef VERILATOR
    VERILATOR_MODEL l2;
#else
    L2CACHE_TEST_DUT<L2_SIZE, PORT_BITS, LINE_SIZE, WAYS, 32, 32, MEM_PORTS, CPU_PORTS> l2;
#endif
    Axi4Ram<32, 4, PORT_BITS, RAM_DEPTH_PER_PORT> ram[MEM_PORTS];

    bool read = false;
    bool d_read = false;
    bool write = false;
    uint32_t i_addr = 0;
    uint32_t d_addr = 0;
    uint32_t wdata = 0;
    uint8_t wmask = 0;
    L2CpuTestInput extra_cpu[CPU_PORTS] = {};
    Axi4Driver<32, 4, PORT_BITS> slave_axi[MEM_PORTS] = {};
    bool region_uncached[MEM_PORTS] = {};
    uint32_t region_size[MEM_PORTS] = {};
    uint32_t memory_base = 0;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        L2_I_READ_IN = _ASSIGN_REG(read);
        L2_I_WRITE_IN = _ASSIGN(false);
        L2_I_ADDR_IN = _ASSIGN_REG(i_addr);
        L2_I_WRITE_DATA_IN = _ASSIGN((uint32_t)0);
        L2_I_WRITE_MASK_IN = _ASSIGN((uint8_t)0);
        L2_I_CACHE_DISABLE_IN = _ASSIGN(false);

        L2_D_READ_IN = _ASSIGN_REG(d_read);
        L2_D_WRITE_IN = _ASSIGN_REG(write);
        L2_D_ADDR_IN = _ASSIGN_REG(d_addr);
        L2_D_WRITE_DATA_IN = _ASSIGN_REG(wdata);
        L2_D_WRITE_MASK_IN = _ASSIGN_REG(wmask);
        L2_D_CACHE_DISABLE_IN = _ASSIGN(false);
        for (size_t i = 1; i < CPU_PORTS; ++i) {
            l2.i_mem_in[i].read_in = _ASSIGN_REG_I(extra_cpu[i].i_read);
            l2.i_mem_in[i].write_in = _ASSIGN(false);
            l2.i_mem_in[i].addr_in = _ASSIGN_REG_I(extra_cpu[i].i_addr);
            l2.i_mem_in[i].write_data_in = _ASSIGN((uint32_t)0);
            l2.i_mem_in[i].write_mask_in = _ASSIGN((uint8_t)0);
            l2.i_mem_in[i].cache_disable_in = _ASSIGN(false);
            l2.d_mem_in[i].read_in = _ASSIGN_REG_I(extra_cpu[i].d_read);
            l2.d_mem_in[i].write_in = _ASSIGN_REG_I(extra_cpu[i].d_write);
            l2.d_mem_in[i].addr_in = _ASSIGN_REG_I(extra_cpu[i].d_addr);
            l2.d_mem_in[i].write_data_in = _ASSIGN_REG_I(extra_cpu[i].write_data);
            l2.d_mem_in[i].write_mask_in = _ASSIGN_REG_I(extra_cpu[i].write_mask);
            l2.d_mem_in[i].cache_disable_in = _ASSIGN(false);
        }
        l2.memory_base_in = _ASSIGN_REG(memory_base);
        l2.memory_size_in = _ASSIGN((uint32_t)0xffffffffu);
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            l2.mem_region_size_in[i] = _ASSIGN_REG_I(region_size[i]);
            l2.mem_region_uncached_in[i] = _ASSIGN_REG_I(region_uncached[i]);
            AXI4_DRIVER_FROM_DRIVER_I(l2.axi_in[i], slave_axi[i]);
        }
        l2.debugen_in = std::getenv("TRIBE_TRACE_L2") != nullptr;
        l2.__inst_name = "l2";
        l2._assign();
#endif

        for (size_t i = 0; i < MEM_PORTS; ++i) {
#ifndef VERILATOR
            AXI4_DRIVER_FROM(ram[i].axi_in, l2.axi_out[i]);
#else
            AXI4_DRIVER_FROM_VERILATOR(ram[i].axi_in, l2, i, u<32>, copy_to_logic<PORT_BITS>);
#endif

            ram[i].debugen_in = false;
            ram[i].__inst_name = "ram" + std::to_string(i);
            ram[i]._assign();
        }

#ifndef VERILATOR
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            AXI4_RESPONDER_FROM(l2.axi_out[i], ram[i].axi_in);
        }
#endif
    }

#ifdef VERILATOR
    void eval_l2(bool reset)
    {
        L2_I_READ_IN = read;
        L2_I_WRITE_IN = false;
        L2_I_ADDR_IN = i_addr;
        L2_I_WRITE_DATA_IN = 0;
        L2_I_WRITE_MASK_IN = 0;
        L2_I_CACHE_DISABLE_IN = 0;
        L2_D_READ_IN = d_read;
        L2_D_WRITE_IN = write;
        L2_D_ADDR_IN = d_addr;
        L2_D_WRITE_DATA_IN = wdata;
        L2_D_WRITE_MASK_IN = wmask;
        L2_D_CACHE_DISABLE_IN = 0;
        for (size_t i = 1; i < CPU_PORTS; ++i) {
            l2.i_mem_in___05Fread_in[i] = extra_cpu[i].i_read;
            l2.i_mem_in___05Fwrite_in[i] = false;
            l2.i_mem_in___05Faddr_in[i] = extra_cpu[i].i_addr;
            l2.i_mem_in___05Fwrite_data_in[i] = 0;
            l2.i_mem_in___05Fwrite_mask_in[i] = 0;
            l2.i_mem_in___05Fcache_disable_in[i] = 0;
            l2.d_mem_in___05Fread_in[i] = extra_cpu[i].d_read;
            l2.d_mem_in___05Fwrite_in[i] = extra_cpu[i].d_write;
            l2.d_mem_in___05Faddr_in[i] = extra_cpu[i].d_addr;
            l2.d_mem_in___05Fwrite_data_in[i] = extra_cpu[i].write_data;
            l2.d_mem_in___05Fwrite_mask_in[i] = extra_cpu[i].write_mask;
            l2.d_mem_in___05Fcache_disable_in[i] = 0;
        }
        l2.memory_base_in = memory_base;
        l2.memory_size_in = 0xffffffffu;
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            l2.mem_region_size_in[i] = region_size[i];
            l2.mem_region_uncached_in[i] = region_uncached[i];
            AXI4_DRIVER_POKE_VERILATOR_IF_FROM_DRIVER_I(l2, axi_in, i, slave_axi[i]);
        }
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            AXI4_RESPONDER_FROM_VERILATOR(l2, ram[i].axi_in, i);
        }
        l2.debugen_in = std::getenv("TRIBE_TRACE_L2") != nullptr;
        l2.reset = reset;
        l2.eval();
    }
#endif

    bool cpu_i_wait(size_t index)
    {
#ifdef VERILATOR
        eval_l2(false);
#endif
        return L2_VALUE(L2_I_WAIT_OUT_AT(index));
    }

    bool cpu_d_wait(size_t index)
    {
#ifdef VERILATOR
        eval_l2(false);
#endif
        return L2_VALUE(L2_D_WAIT_OUT_AT(index));
    }

    logic<PORT_BITS> cpu_i_data(size_t index)
    {
#ifdef VERILATOR
        eval_l2(false);
        return copy_to_logic<PORT_BITS>(L2_I_READ_DATA_OUT_AT(index));
#else
        return L2_I_READ_DATA_OUT_AT(index)();
#endif
    }

    void cycle(bool reset = false)
    {
#ifdef VERILATOR
        l2.clk = 0;
        eval_l2(reset);
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            ram[i]._work(reset);
        }
        eval_l2(reset);
        // The RAM responder reads L2 master outputs through function_refs, then
        // feeds responder data back into Verilated inputs. A second low-clock
        // eval settles that L2->RAM->L2 path before the sampling edge.
        eval_l2(reset);
        l2.clk = 1;
        eval_l2(reset);
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            ram[i]._strobe();
        }
        l2.clk = 0;
        eval_l2(reset);
#else
        l2._work(reset);
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            ram[i]._work(reset);
        }
        l2._strobe();
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            ram[i]._strobe();
        }
#endif
        ++_system_clock;
    }

    void preload()
    {
        for (size_t port = 0; port < MEM_PORTS; ++port) {
            region_size[port] = REGION_SIZE;
            region_uncached[port] = false;
            slave_axi[port] = {};
        }
        // Keep backing RAM at reset-zero for the first fill/hit smoke check.
        // Later sections write explicit data through the CPU and AXI slave ports.
        for (size_t port = 0; port < MEM_PORTS; ++port) {
            for (size_t beat = 0; beat < RAM_DEPTH_PER_PORT; ++beat) {
                ram[port].ram.buffer.data[beat] = 0;
            }
        }
    }

    bool address_to_region(uint32_t request_addr, size_t& port, uint32_t& local_addr)
    {
        uint64_t base = 0;
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            uint64_t size = region_size[i] ? region_size[i] : REGION_SIZE64;
            if ((uint64_t)request_addr >= base && (uint64_t)request_addr < base + size) {
                port = i;
                local_addr = request_addr - (uint32_t)base;
                return true;
            }
            base += size;
        }
        port = MEM_PORTS - 1;
        local_addr = request_addr;
        return false;
    }

    void set_backing_word(uint32_t request_addr, uint32_t data)
    {
        size_t port;
        uint32_t local_addr;
        if (!address_to_region(request_addr, port, local_addr)) {
            std::print("\nbacking write address outside regions addr={:#x}\n", request_addr);
            error = true;
            return;
        }
        size_t beat = local_addr / (PORT_BITS / 8);
        size_t lane = (local_addr % (PORT_BITS / 8)) / 4;
        if (beat >= RAM_DEPTH_PER_PORT) {
            std::print("\nbacking write beyond RAM port={} addr={:#x}\n", port, request_addr);
            error = true;
            return;
        }
        auto& row = ram[port].ram.buffer.data[beat];
        row.data[lane * 4 + 0] = (u8)(data >> 0);
        row.data[lane * 4 + 1] = (u8)(data >> 8);
        row.data[lane * 4 + 2] = (u8)(data >> 16);
        row.data[lane * 4 + 3] = (u8)(data >> 24);
    }

    static uint32_t prbs_mix(uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    static uint8_t prbs_byte(uint64_t pos, uint32_t seed)
    {
        return (uint8_t)prbs_mix((uint32_t)pos ^ seed ^ (uint32_t)(pos >> 32));
    }

    static uint32_t prbs_word(uint64_t pos, uint32_t seed)
    {
        return (uint32_t)prbs_byte(pos + 0, seed) |
               ((uint32_t)prbs_byte(pos + 1, seed) << 8) |
               ((uint32_t)prbs_byte(pos + 2, seed) << 16) |
               ((uint32_t)prbs_byte(pos + 3, seed) << 24);
    }

    void read_check(uint32_t request_addr, uint32_t expected)
    {
        read = true;
        d_read = false;
        write = false;
        i_addr = request_addr;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(L2_I_WAIT_OUT); ++i) {
            cycle(false);
        }
        uint32_t beat_word = ((request_addr % (PORT_BITS / 8)) / 4);
        uint32_t data = port_word(L2_VALUE(L2_I_READ_DATA_OUT), beat_word);
        if (L2_VALUE(L2_I_WAIT_OUT) || data != expected) {
            std::print("\nread ERROR addr={:#x} wait={} data={:#x} expected={:#x}\n",
                request_addr, L2_VALUE(L2_I_WAIT_OUT), data, expected);
            error = true;
        }
        cycle(false);
        read = false;
        cycle(false);
    }

    void instruction_cross_line_direct_check()
    {
        // Scenario: compressed code may place a 32-bit instruction at the final
        // halfword of a cache line. L2 must bypass the cached-line hit path and
        // return the assembled low 32 bits from the tail of this line plus the
        // head of the next line for every AXI beat width.
        constexpr uint32_t line_base = 0x00000400u;
        constexpr uint32_t low_word = 0x44332211u;
        constexpr uint32_t high_word = 0x88776655u;
        constexpr uint32_t expected = 0x66554433u;
        set_backing_word(line_base + LINE_SIZE - 4, low_word);
        set_backing_word(line_base + LINE_SIZE, high_word);

        read_check(line_base + LINE_SIZE - 4, low_word);

        read = true;
        d_read = false;
        write = false;
        i_addr = line_base + LINE_SIZE - 2;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(L2_I_WAIT_OUT); ++i) {
            cycle(false);
        }
        uint32_t data = port_word(L2_VALUE(L2_I_READ_DATA_OUT), 0);
        if (L2_VALUE(L2_I_WAIT_OUT) || data != expected) {
            std::print("\ninstruction cross-line read ERROR addr={:#x} wait={} data={:#x} expected={:#x}\n",
                i_addr, L2_VALUE(L2_I_WAIT_OUT), data, expected);
            error = true;
        }
        cycle(false);
        read = false;
        cycle(false);
    }

    void data_port_unaligned_beat_end_direct_read_check()
    {
        // Scenario from Linux string output and CPU byte loads: L1 d-cache asks
        // L2 for a direct unaligned read at the final word of an L2 AXI beat.
        // L2 must assemble the low 32 bits from the tail word and the next beat.
        constexpr uint32_t beat_base = 0x00000800u;
        constexpr uint32_t low_word = 0xbbaa4433u;
        constexpr uint32_t next_word = 0x2211ddccu;
        constexpr uint32_t request_addr = beat_base + (uint32_t)(PORT_BITS / 8) - 3u;
        constexpr uint32_t expected = (low_word >> 8) | (next_word << 24);
        set_backing_word(beat_base + (uint32_t)(PORT_BITS / 8) - 4u, low_word);
        set_backing_word(beat_base + (uint32_t)(PORT_BITS / 8), next_word);

        read = false;
        d_read = true;
        write = false;
        d_addr = request_addr;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(L2_D_WAIT_OUT); ++i) {
            cycle(false);
        }
        uint32_t data = port_word(L2_VALUE(L2_D_READ_DATA_OUT), 0);
        if (L2_VALUE(L2_D_WAIT_OUT) || data != expected) {
            std::print("\ndirect d-read cross-beat ERROR addr={:#x} wait={} data={:#x} expected={:#x}\n",
                request_addr, L2_VALUE(L2_D_WAIT_OUT), data, expected);
            error = true;
        }
        cycle(false);
        d_read = false;
        cycle(false);
    }

    void d_read_check(uint32_t request_addr, uint32_t expected)
    {
        read = false;
        d_read = true;
        write = false;
        d_addr = request_addr;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(L2_D_WAIT_OUT); ++i) {
            cycle(false);
        }
        uint32_t beat_word = ((request_addr % (PORT_BITS / 8)) / 4);
        if ((request_addr & 3u) != 0 && beat_word + 1 >= PORT_BITS / 32) {
            beat_word = 0;
        }
        uint32_t data = port_word(L2_VALUE(L2_D_READ_DATA_OUT), beat_word);
        if (L2_VALUE(L2_D_WAIT_OUT) || data != expected) {
            std::print("\nd-read ERROR addr={:#x} wait={} data={:#x} expected={:#x}\n",
                request_addr, L2_VALUE(L2_D_WAIT_OUT), data, expected);
            error = true;
        }
        cycle(false);
        d_read = false;
        cycle(false);
    }

    void write_then_read_check(uint32_t request_addr, uint32_t data, uint8_t mask, uint32_t expected)
    {
        read = false;
        d_read = false;
        write = true;
        d_addr = request_addr;
        wdata = data;
        wmask = mask;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(L2_D_WAIT_OUT); ++i) {
            cycle(false);
        }
        if (L2_VALUE(L2_D_WAIT_OUT)) {
            std::print("\nwrite ERROR addr={:#x} wait=1\n", request_addr);
            error = true;
        }
        cycle(false);
        write = false;
        cycle(false);
        read_check(request_addr, expected);
    }

    void write_only(uint32_t request_addr, uint32_t data, uint8_t mask)
    {
        read = false;
        write = true;
        d_addr = request_addr;
        wdata = data;
        wmask = mask;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(L2_D_WAIT_OUT); ++i) {
            cycle(false);
        }
        if (L2_VALUE(L2_D_WAIT_OUT)) {
            std::print("\nwrite ERROR addr={:#x} wait=1\n", request_addr);
            error = true;
        }
        cycle(false);
        write = false;
        cycle(false);
    }

    bool slave_awready(size_t port)
    {
#ifdef VERILATOR
        eval_l2(false);
        return l2.axi_in___05Fawready_out[port];
#else
        return l2.axi_in[port].awready_out();
#endif
    }

    bool slave_wready(size_t port)
    {
#ifdef VERILATOR
        eval_l2(false);
        return l2.axi_in___05Fwready_out[port];
#else
        return l2.axi_in[port].wready_out();
#endif
    }

    bool slave_bvalid(size_t port)
    {
#ifdef VERILATOR
        eval_l2(false);
        return l2.axi_in___05Fbvalid_out[port];
#else
        return l2.axi_in[port].bvalid_out();
#endif
    }

    bool slave_arready(size_t port)
    {
#ifdef VERILATOR
        eval_l2(false);
        return l2.axi_in___05Farready_out[port];
#else
        return l2.axi_in[port].arready_out();
#endif
    }

    bool slave_rvalid(size_t port)
    {
#ifdef VERILATOR
        eval_l2(false);
        return l2.axi_in___05Frvalid_out[port];
#else
        return l2.axi_in[port].rvalid_out();
#endif
    }

    logic<PORT_BITS> slave_rdata(size_t port)
    {
#ifdef VERILATOR
        eval_l2(false);
        const auto& raw = l2.axi_in___05Frdata_out[port];
        return copy_to_logic<PORT_BITS>(raw);
#else
        return l2.axi_in[port].rdata_out();
#endif
    }

    u<4> slave_rid(size_t port)
    {
#ifdef VERILATOR
        eval_l2(false);
        return (u<4>)(uint32_t)l2.axi_in___05Frid_out[port];
#else
        return l2.axi_in[port].rid_out();
#endif
    }

    logic<PORT_BITS> axi_read_beat(size_t port, uint32_t request_addr)
    {
        slave_axi[port].ar.valid = true;
        slave_axi[port].ar.addr = request_addr & ~(uint32_t)((PORT_BITS / 8) - 1);
        slave_axi[port].ar.id = 5;
        slave_axi[port].r.ready = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_arready(port); ++i) {
            cycle(false);
        }
        if (!slave_arready(port)) {
            std::print("\naxi read beat address ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        cycle(false);
        slave_axi[port].ar.valid = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_rvalid(port); ++i) {
            cycle(false);
        }
        logic<PORT_BITS> beat = slave_rdata(port);
        if (!slave_rvalid(port)) {
            std::print("\naxi read beat response ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        slave_axi[port].r.ready = true;
        cycle(false);
        slave_axi[port].r.ready = false;
        cycle(false);
        return beat;
    }

    void axi_write_beat(size_t port, uint32_t request_addr, logic<PORT_BITS> beat)
    {
        slave_axi[port].aw.valid = true;
        slave_axi[port].aw.addr = request_addr & ~(uint32_t)((PORT_BITS / 8) - 1);
        slave_axi[port].aw.id = 3;
        slave_axi[port].w.valid = false;
        slave_axi[port].w.data = beat;
        slave_axi[port].w.strb = ~logic<PORT_BITS / 8>(0);
        slave_axi[port].w.last = true;
        slave_axi[port].b.ready = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_awready(port); ++i) {
            cycle(false);
        }
        if (!slave_awready(port)) {
            std::print("\naxi write address ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        cycle(false);
        slave_axi[port].aw.valid = false;
        slave_axi[port].w.valid = true;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_wready(port); ++i) {
            cycle(false);
        }
        if (!slave_wready(port)) {
            std::print("\naxi write data ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        cycle(false);
        slave_axi[port].w.valid = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_bvalid(port); ++i) {
            cycle(false);
        }
        if (!slave_bvalid(port)) {
            std::print("\naxi write response ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        slave_axi[port].b.ready = true;
        cycle(false);
        slave_axi[port].b.ready = false;
        cycle(false);
    }

    void axi_write_masked_beat(size_t port, uint32_t request_addr, logic<PORT_BITS> beat, logic<PORT_BITS / 8> strb)
    {
        slave_axi[port].aw.valid = true;
        slave_axi[port].aw.addr = request_addr & ~(uint32_t)((PORT_BITS / 8) - 1);
        slave_axi[port].aw.id = 4;
        slave_axi[port].w.valid = false;
        slave_axi[port].w.data = beat;
        slave_axi[port].w.strb = strb;
        slave_axi[port].w.last = true;
        slave_axi[port].b.ready = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_awready(port); ++i) {
            cycle(false);
        }
        if (!slave_awready(port)) {
            std::print("\nmasked AXI write address ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        cycle(false);
        slave_axi[port].aw.valid = false;
        slave_axi[port].w.valid = true;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_wready(port); ++i) {
            cycle(false);
        }
        if (!slave_wready(port)) {
            std::print("\nmasked AXI write data ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        cycle(false);
        slave_axi[port].w.valid = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_bvalid(port); ++i) {
            cycle(false);
        }
        if (!slave_bvalid(port)) {
            std::print("\nmasked AXI write response ERROR port={} addr={:#x}\n", port, request_addr);
            error = true;
        }
        slave_axi[port].b.ready = true;
        cycle(false);
        slave_axi[port].b.ready = false;
        slave_axi[port].w.strb = ~logic<PORT_BITS / 8>(0);
        cycle(false);
    }

    void axi_write_word(size_t port, uint32_t request_addr, uint32_t data)
    {
        logic<PORT_BITS> beat = axi_read_beat(port, request_addr);
        size_t lane = (request_addr % (PORT_BITS / 8)) / 4;
        beat.bits(lane * 32 + 31, lane * 32) = data;
        axi_write_beat(port, request_addr, beat);
    }

    uint32_t axi_read_word(size_t port, uint32_t request_addr)
    {
        logic<PORT_BITS> beat = axi_read_beat(port, request_addr);
        uint32_t lane = (request_addr % (PORT_BITS / 8)) / 4;
        uint32_t data = (uint32_t)beat.bits(lane * 32 + 31, lane * 32);
        return data;
    }

    void slave_coherence_check()
    {
        write_only(0x00000104u, 0x11223344u, 0xf);
        uint32_t by_master = axi_read_word(0, 0x00000104u);
        if (by_master != 0x11223344u) {
            std::print("\nCPU->AXI coherence ERROR got={:#x}\n", by_master);
            error = true;
        }

        axi_write_word(1, 0x00000108u, 0x55667788u);
        read_check(0x00000108u, 0x55667788u);

        slave_axi[0].ar.valid = true;
        slave_axi[0].ar.addr = 0x00000104u;
        slave_axi[0].ar.id = 1;
        slave_axi[0].r.ready = false;
        slave_axi[1].aw.valid = true;
        slave_axi[1].aw.addr = 0x0000010cu;
        slave_axi[1].aw.id = 2;
        slave_axi[1].w.valid = true;
        slave_axi[1].w.last = true;
        slave_axi[1].b.ready = false;
        slave_axi[1].w.data = 0;
        {
            size_t lane = (0x0000010cu % (PORT_BITS / 8)) / 4;
            slave_axi[1].w.data.bits(lane * 32 + 31, lane * 32) = 0x99aabbccu;
        }
        for (size_t i = 0; i < WAIT_LIMIT && (!slave_rvalid(0) || !slave_bvalid(1)); ++i) {
            cycle(false);
        }
        if (!slave_rvalid(0) || !slave_bvalid(1)) {
            std::print("\nsimultaneous AXI masters ERROR rvalid={} bvalid={}\n", slave_rvalid(0), slave_bvalid(1));
            error = true;
        }
        slave_axi[0].ar.valid = false;
        slave_axi[1].aw.valid = false;
        slave_axi[1].w.valid = false;
        slave_axi[0].r.ready = true;
        slave_axi[1].b.ready = true;
        cycle(false);
        slave_axi[0].r.ready = false;
        slave_axi[1].b.ready = false;
        cycle(false);
        read_check(0x0000010cu, 0x99aabbccu);
    }

    void slave_response_turnover_check()
    {
        uint32_t base;
        size_t lane;
        uint32_t data;
        bool sticky_replayed;

        base = 0x00000600u;
        write_only(base, 0x10203040u, 0xf);
        write_only(base + (uint32_t)(PORT_BITS / 8), 0x50607080u, 0xf);

        slave_axi[0].ar.valid = true;
        slave_axi[0].ar.addr = base;
        slave_axi[0].ar.id = 9;
        slave_axi[0].r.ready = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_arready(0); ++i) {
            cycle(false);
        }
        cycle(false);
        // Keep the next AR and response ready asserted before the first R
        // appears, as a real AXI master does for back-to-back transactions.
        slave_axi[0].ar.valid = true;
        slave_axi[0].ar.addr = base + (uint32_t)(PORT_BITS / 8);
        slave_axi[0].ar.id = 10;
        slave_axi[0].r.ready = true;
        for (size_t i = 0; i < WAIT_LIMIT && (!slave_rvalid(0) || !slave_arready(0)); ++i) {
            cycle(false);
        }
        // Retire the first R response while accepting the next AR into the
        // request register, proving the response stage adds no idle bubble.
        if (!slave_rvalid(0) || !slave_arready(0)) {
            std::print("\nAXI response turnover ERROR rvalid={} arready={}\n",
                slave_rvalid(0), slave_arready(0));
            error = true;
        }
        cycle(false);
        slave_axi[0].ar.valid = false;
        slave_axi[0].r.ready = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_rvalid(0); ++i) {
            cycle(false);
        }
        lane = ((base + (uint32_t)(PORT_BITS / 8)) % (PORT_BITS / 8)) / 4;
        data = port_word(slave_rdata(0), lane);
        if (!slave_rvalid(0) || slave_rid(0) != 10 || data != 0x50607080u) {
            std::print("\nAXI response turnover data ERROR valid={} id={} data={:#x}\n",
                slave_rvalid(0), (uint32_t)slave_rid(0), data);
            error = true;
        }
        slave_axi[0].r.ready = true;
        cycle(false);
        slave_axi[0].r.ready = false;
        cycle(false);

        // A master may retain the accepted AR payload until R arrives. It is
        // not a second request unless VALID drops or the payload changes.
        base += (uint32_t)(2 * (PORT_BITS / 8));
        write_only(base, 0x90abcdefu, 0xf);
        slave_axi[0].ar.valid = true;
        slave_axi[0].ar.addr = base;
        slave_axi[0].ar.id = 11;
        slave_axi[0].r.ready = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_arready(0); ++i) {
            cycle(false);
        }
        cycle(false);
        for (size_t i = 0; i < WAIT_LIMIT && !slave_rvalid(0); ++i) {
            cycle(false);
        }
        slave_axi[0].r.ready = true;
        sticky_replayed = slave_arready(0);
        cycle(false);
        for (size_t i = 0; i < 3; ++i) {
            sticky_replayed = sticky_replayed || slave_arready(0);
            cycle(false);
        }
        if (sticky_replayed) {
            std::print("\nsticky AXI AR replay ERROR\n");
            error = true;
        }
        slave_axi[0].ar.valid = false;
        slave_axi[0].r.ready = false;
        cycle(false);
        slave_axi[0].ar.valid = true;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_arready(0); ++i) {
            cycle(false);
        }
        if (!slave_arready(0)) {
            std::print("\nsticky AXI AR rearm ERROR\n");
            error = true;
        }
        cycle(false);
        slave_axi[0].ar.valid = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_rvalid(0); ++i) {
            cycle(false);
        }
        lane = (base % (PORT_BITS / 8)) / 4;
        data = port_word(slave_rdata(0), lane);
        if (!slave_rvalid(0) || data != 0x90abcdefu) {
            std::print("\nsticky AXI AR rearm data ERROR valid={} data={:#x}\n", slave_rvalid(0), data);
            error = true;
        }
        slave_axi[0].r.ready = true;
        cycle(false);
        slave_axi[0].r.ready = false;
        cycle(false);
    }

    void slave_full_width_write_check()
    {
        logic<PORT_BITS> beat = 0;
        uint32_t base = 0x00000400u;
        read_check(base, 0);
        for (size_t word = 0; word < PORT_BITS / 32; ++word) {
            beat.bits(word * 32 + 31, word * 32) = 0x45000000u + (uint32_t)word * 0x01111111u;
        }
        axi_write_beat(1, base, beat);
        for (size_t word = 0; word < PORT_BITS / 32; ++word) {
            read_check(base + (uint32_t)word * 4u, 0x45000000u + (uint32_t)word * 0x01111111u);
        }

        beat = 0;
        for (size_t word = 0; word < PORT_BITS / 32; ++word) {
            beat.bits(word * 32 + 31, word * 32) = 0x73000000u + (uint32_t)word * 0x00010101u;
        }
        axi_write_beat(1, base + (uint32_t)(PORT_BITS / 8), beat);
        logic<PORT_BITS> by_master = axi_read_beat(0, base + (uint32_t)(PORT_BITS / 8));
        for (size_t word = 0; word < PORT_BITS / 32; ++word) {
            uint32_t data = (uint32_t)by_master.bits(word * 32 + 31, word * 32);
            uint32_t expected = 0x73000000u + (uint32_t)word * 0x00010101u;
            if (data != expected) {
                std::print("\nfull-width AXI write/read ERROR word={} data={:#x} expected={:#x}\n", word, data, expected);
                error = true;
            }
        }
    }

    void slave_write_strobe_preserves_neighbor_words_check()
    {
        uint32_t base = 0x00000500u;
        logic<PORT_BITS> beat;
        logic<PORT_BITS / 8> strb;
        size_t word;

        for (word = 0; word < PORT_BITS / 32; ++word) {
            write_only(base + (uint32_t)word * 4u, 0x81000000u + (uint32_t)word, 0xf);
        }

        beat = 0;
        for (word = 0; word < PORT_BITS / 32; ++word) {
            beat.bits(word * 32 + 31, word * 32) = 0xdead0000u + (uint32_t)word;
        }
        strb = 0;
        strb.bits(7, 4) = 0xf;
        beat.bits(63, 32) = 0x0000003cu;
        axi_write_masked_beat(1, base, beat, strb);

        for (word = 0; word < PORT_BITS / 32; ++word) {
            uint32_t expected = (word == 1) ? 0x0000003cu : (0x81000000u + (uint32_t)word);
            uint32_t got = axi_read_word(0, base + (uint32_t)word * 4u);
            if (got != expected) {
                std::print("\nAXI wstrb preserve ERROR word={} got={:#x} expected={:#x}\n", word, got, expected);
                error = true;
            }
        }
    }

    void local_slave_address_with_nonzero_memory_base_check()
    {
        memory_base = 0x80000000u;
        axi_write_word(1, 0x00000040u, 0x13579bdfu);
        read_check(memory_base + 0x00000040u, 0x13579bdfu);
        write_only(memory_base + 0x00000044u, 0x2468ace0u, 0xf);
        uint32_t by_master = axi_read_word(1, 0x00000044u);
        if (by_master != 0x2468ace0u) {
            std::print("\nnonzero memory_base external AXI local read ERROR got={:#x}\n", by_master);
            error = true;
        }
        memory_base = 0;
    }

    void uncached_device_region_check()
    {
        region_uncached[DEVICE_PORT] = true;
        uint32_t device_addr = (uint32_t)(REGION_SIZE64 * DEVICE_PORT) + 0x20u;
        write_only(device_addr, 0xdeadbeefu, 0xf);
        uint32_t by_master = axi_read_word(DEVICE_PORT, device_addr);
        if (by_master != 0xdeadbeefu) {
            std::print("\nuncached CPU write/device read ERROR got={:#x}\n", by_master);
            error = true;
        }
        axi_write_word(DEVICE_PORT, device_addr + 4, 0xfeed1234u);
        read_check(device_addr + 4, 0xfeed1234u);
    }

    void slave_completion_does_not_release_cpu_iport_check()
    {
        slave_axi[0].ar.valid = true;
        slave_axi[0].ar.addr = 0x00000000u;
        slave_axi[0].ar.id = 6;
        slave_axi[0].r.ready = false;
        read = true;
        i_addr = 0x00000008u;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_rvalid(0); ++i) {
            cycle(false);
        }
        if (!slave_rvalid(0)) {
            std::print("\nAXI read while CPU iport waits ERROR: no slave response\n");
            error = true;
        }
        if (!L2_VALUE(L2_I_WAIT_OUT)) {
            std::print("\nAXI read while CPU iport waits ERROR: slave completion released iport\n");
            error = true;
        }
        slave_axi[0].ar.valid = false;
        slave_axi[0].r.ready = true;
        cycle(false);
        slave_axi[0].r.ready = false;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(L2_I_WAIT_OUT); ++i) {
            cycle(false);
        }
        uint32_t beat_word = ((0x00000008u % (PORT_BITS / 8)) / 4);
        uint32_t data = port_word(L2_VALUE(L2_I_READ_DATA_OUT), beat_word);
        if (L2_VALUE(L2_I_WAIT_OUT) || data != 0) {
            std::print("\nCPU iport after AXI read ERROR wait={} data={:#x}\n", L2_VALUE(L2_I_WAIT_OUT), data);
            error = true;
        }
        cycle(false);
        read = false;
        cycle(false);
    }

    void slave_request_does_not_hide_cpu_write_completion_check()
    {
        uint32_t device_addr = (uint32_t)(REGION_SIZE64 * DEVICE_PORT) + 0x40u;
        region_uncached[DEVICE_PORT] = true;
        read = false;
        d_read = false;
        write = true;
        d_addr = device_addr;
        wdata = 0x1234abcdu;
        wmask = 0xf;
        cycle(false);
        slave_axi[0].ar.valid = true;
        slave_axi[0].ar.addr = 0x00000000u;
        slave_axi[0].ar.id = 7;
        slave_axi[0].r.ready = false;
        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(L2_D_WAIT_OUT); ++i) {
            cycle(false);
        }
        if (L2_VALUE(L2_D_WAIT_OUT)) {
            std::print("\nAXI request while CPU write completes ERROR: dport completion hidden\n");
            error = true;
        }
        cycle(false);
        write = false;
        for (size_t i = 0; i < WAIT_LIMIT && !slave_rvalid(0); ++i) {
            cycle(false);
        }
        if (!slave_rvalid(0)) {
            std::print("\nAXI request after CPU write ERROR: no slave response\n");
            error = true;
        }
        slave_axi[0].ar.valid = false;
        slave_axi[0].r.ready = true;
        cycle(false);
        slave_axi[0].r.ready = false;
        cycle(false);
        read_check(device_addr, 0x1234abcdu);
    }

    void slave_request_does_not_drop_cpu_dport_read_check()
    {
        uint32_t device_addr = (uint32_t)(REGION_SIZE64 * DEVICE_PORT) + 0x80u;
        region_uncached[DEVICE_PORT] = true;
        axi_write_word(DEVICE_PORT, device_addr, 0x0badc0deu);

        slave_axi[0].ar.valid = true;
        slave_axi[0].ar.addr = 0x00000000u;
        slave_axi[0].ar.id = 8;
        slave_axi[0].r.ready = false;
        read = false;
        d_read = true;
        write = false;
        d_addr = device_addr;

        for (size_t i = 0; i < WAIT_LIMIT && !slave_rvalid(0); ++i) {
            cycle(false);
        }
        if (!slave_rvalid(0)) {
            std::print("\nAXI request while CPU dport read waits ERROR: no slave response\n");
            error = true;
        }
        slave_axi[0].ar.valid = false;
        slave_axi[0].r.ready = true;
        cycle(false);
        slave_axi[0].r.ready = false;

        for (size_t i = 0; i < WAIT_LIMIT && L2_VALUE(L2_D_WAIT_OUT); ++i) {
            cycle(false);
        }
        uint32_t beat_word = ((device_addr % (PORT_BITS / 8)) / 4);
        uint32_t data = port_word(L2_VALUE(L2_D_READ_DATA_OUT), beat_word);
        if (L2_VALUE(L2_D_WAIT_OUT) || data != 0x0badc0deu) {
            std::print("\nCPU dport after AXI read ERROR wait={} data={:#x}\n", L2_VALUE(L2_D_WAIT_OUT), data);
            error = true;
        }
        cycle(false);
        d_read = false;
        cycle(false);
    }

    void stack_alias_check()
    {
        write_then_read_check(0xfffffb08u, 0x000002bcu, 0xf, 0x000002bcu);
        read_check(0x000001a0u, 0x00000000u);
        read_check(0x000001c0u, 0x00000000u);
        read_check(0x000002bcu, 0x00000000u);
        read_check(0xfffffb08u, 0x000002bcu);
    }

    void byte_store_check()
    {
        write_only(0xfffffe54u, 0x00000002u, 0x1);
        write_only(0xfffffe55u, 0x00000000u, 0x1);
        write_only(0xfffffe56u, 0x00000000u, 0x1);
        write_only(0xfffffe57u, 0x00000000u, 0x1);
        read_check(0xfffffe54u, 0x00000002u);

        write_only(0x0000017cu, 0x0000004du, 0x1);
        write_only(0x0000017du, 0x00000045u, 0x1);
        read_check(0x0000017cu, 0x0000454du);
        d_read_check(0x0000017du, 0x00000045u);
    }

    void cpu_byte_store_visible_to_axi_master_check(bool fill_before_store, bool force_evict)
    {
        uint32_t saved_memory_base = memory_base;
        uint32_t local_base = fill_before_store ?
            (force_evict ? 0x000007c0u : 0x000006c0u) :
            (force_evict ? 0x00000840u : 0x00000740u);
        uint32_t cpu_base = 0x80000000u + local_base;
        constexpr uint32_t set_stride = (L2_SIZE / LINE_SIZE / WAYS) * LINE_SIZE;
        uint8_t expected[PORT_BITS / 8];

        memory_base = 0x80000000u;
        for (size_t word = 0; word < PORT_BITS / 32; ++word) {
            uint32_t value = 0x44332211u + (uint32_t)word * 0x11111111u;
            set_backing_word(local_base + (uint32_t)word * 4u, value);
            expected[word * 4u + 0u] = (uint8_t)(value >> 0);
            expected[word * 4u + 1u] = (uint8_t)(value >> 8);
            expected[word * 4u + 2u] = (uint8_t)(value >> 16);
            expected[word * 4u + 3u] = (uint8_t)(value >> 24);
        }

        // Mimic Linux networking: byte stores patch an unaligned Ethernet
        // header just before DMA reads it. Run both hit and write-miss cases.
        if (fill_before_store) {
            d_read_check(cpu_base, 0x44332211u);
        }
        const uint8_t mac_dst[6] = {0x82, 0x80, 0x79, 0x17, 0x0a, 0x07};
        const uint8_t mac_src[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
        const uint8_t eth_type[2] = {0x08, 0x00};
        for (size_t i = 0; i < 6; ++i) {
            write_only(cpu_base + 2u + (uint32_t)i, mac_dst[i], 0x1);
            expected[2u + i] = mac_dst[i];
        }
        for (size_t i = 0; i < 6; ++i) {
            write_only(cpu_base + 8u + (uint32_t)i, mac_src[i], 0x1);
            expected[8u + i] = mac_src[i];
        }
        for (size_t i = 0; i < 2; ++i) {
            write_only(cpu_base + 14u + (uint32_t)i, eth_type[i], 0x1);
            expected[14u + i] = eth_type[i];
        }
        // Also patch the destination IPv4 bytes in the next word, matching the
        // failed ping trace where CPU reads saw c0.a8.4c.01 but DMA did not.
        const uint8_t ip_dst[4] = {0xc0, 0xa8, 0x4c, 0x01};
        for (size_t i = 0; i < 4; ++i) {
            write_only(cpu_base + 32u + (uint32_t)i, ip_dst[i], 0x1);
            expected[32u + i] = ip_dst[i];
        }

        if (force_evict) {
            for (size_t way = 0; way <= WAYS; ++way) {
                write_only(cpu_base + (uint32_t)((way + 1u) * set_stride), 0x70000000u + (uint32_t)way, 0xf);
            }
        }

        logic<PORT_BITS> beat = axi_read_beat(0, local_base);
        for (size_t i = 0; i < PORT_BITS / 8; ++i) {
            uint8_t got = (uint8_t)beat.bits(i * 8u + 7u, i * 8u);
            if (got != expected[i]) {
                std::print("\nCPU byte store AXI visibility ERROR fill_before_store={} force_evict={} byte={} got={:#x} expected={:#x}\n",
                    fill_before_store, force_evict, i, got, expected[i]);
                error = true;
            }
        }
        memory_base = saved_memory_base;
    }

    void dirty_eviction_check()
    {
        constexpr uint32_t set_stride = (L2_SIZE / LINE_SIZE / WAYS) * LINE_SIZE;
        // Linux stack frames save ra/s0/s1 in adjacent words near the end of a line.
        // Dirty eviction must preserve every word lane, including offset +12.
        write_then_read_check(0 * set_stride + 4, 0x11111111u, 0xf, 0x11111111u);
        write_then_read_check(0 * set_stride + 12, 0x1a2b3c4du, 0xf, 0x1a2b3c4du);
        write_then_read_check(1 * set_stride + 4, 0x22222222u, 0xf, 0x22222222u);
        write_then_read_check(2 * set_stride + 4, 0x33333333u, 0xf, 0x33333333u);
        write_then_read_check(3 * set_stride + 4, 0x44444444u, 0xf, 0x44444444u);
        write_then_read_check(4 * set_stride + 4, 0x55555555u, 0xf, 0x55555555u);
        read_check(0 * set_stride + 4, 0x11111111u);
        read_check(0 * set_stride + 12, 0x1a2b3c4du);
    }

    void immediate_hit_after_fill_check()
    {
        // Linux D-cache refill asks L2 for several PORT_BITS beats from one line.
        // A hit immediately after an AXI fill must wait until the selected beat is
        // registered, otherwise L1 can capture stale zero instead of the stack word.
        uint32_t base = 0x00000600u;
        set_backing_word(base + 0,  0x11112222u);
        set_backing_word(base + 4,  0x33334444u);
        set_backing_word(base + 8,  0x55556666u);
        set_backing_word(base + 12, 0x77778888u);
        set_backing_word(base + 16, 0x9999aaaau);
        set_backing_word(base + 20, 0xbbbbccccu);
        set_backing_word(base + 24, 0xddddeeeeu);
        set_backing_word(base + 28, 0xffff0001u);

        d_read_check(base + 0,  0x11112222u);
        d_read_check(base + 12, 0x77778888u);
        d_read_check(base + 20, 0xbbbbccccu);
        d_read_check(base + 28, 0xffff0001u);
    }

    void cycle_prbs_test()
    {
        if constexpr (MEM_PORTS < 3) {
            return;
        }

        // Scenario: three L2 memory regions are treated as one 6 MiB cyclic
        // address space. The d-cache actor writes a PRBS stream into the first
        // half while the external AXI actor verifies it; the AXI actor writes a
        // second PRBS stream into the second half while the d-cache actor verifies
        // it. The instruction-cache actor injects unrelated fills so arbitration
        // sees I, D, and external-master traffic in the same long run.
        uint32_t saved_region_size[MEM_PORTS];
        bool saved_uncached[MEM_PORTS];
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            saved_region_size[i] = region_size[i];
            saved_uncached[i] = region_uncached[i];
            region_size[i] = PRBS_REGION_SIZE;
            region_uncached[i] = false;
            slave_axi[i] = {};
        }

        read = false;
        d_read = false;
        write = false;
        cycle(false);

        uint32_t half_bytes = 4u * 1024u;
        if (const char* full = std::getenv("L2_PRBS_FULL"); full && std::strcmp(full, "0") != 0) {
            half_bytes = PRBS_TOTAL_SIZE / 2;
        }
        if (const char* override_bytes = std::getenv("L2_PRBS_HALF_BYTES")) {
            uint32_t requested = (uint32_t)std::strtoul(override_bytes, nullptr, 0);
            if (requested != 0) {
                half_bytes = requested;
            }
        }
        half_bytes = (half_bytes / 4) * 4;
        if (half_bytes == 0) {
            half_bytes = 4;
        }
        if (half_bytes > PRBS_TOTAL_SIZE / 2) {
            half_bytes = PRBS_TOTAL_SIZE / 2;
        }
        const uint32_t first_base = 0;
        const uint32_t second_base = PRBS_TOTAL_SIZE / 2;
        const uint32_t target_bytes = half_bytes * 2;
        const uint32_t ring_guard = half_bytes / 2;
        const uint32_t seed_d_to_axi = 0x13579bdfu;
        const uint32_t seed_axi_to_d = 0x2468ace0u;

        enum class DOp { Idle, Write, Read };
        enum class AxiWriteOp { Idle, Aw, W, B };
        enum class AxiReadOp { Idle, Ar, R };

        DOp d_op = DOp::Idle;
        AxiWriteOp axi_w_op = AxiWriteOp::Idle;
        AxiReadOp axi_r_op = AxiReadOp::Idle;
        uint32_t d_write_pos = 0;
        uint32_t d_read_pos = 0;
        uint32_t axi_write_pos = 0;
        uint32_t axi_read_pos = 0;
        uint32_t d_chunk_left = 0;
        uint32_t axi_w_chunk_left = 0;
        uint32_t axi_r_chunk_left = 0;
        uint32_t d_active_pos = 0;
        uint32_t axi_w_active_pos = 0;
        uint32_t axi_r_active_pos = 0;
        uint32_t axi_w_age = 0;
        uint32_t axi_r_age = 0;
        bool i_active = false;
        uint32_t rng = 0x12345678u;
        uint32_t idle_d_cycles = 0;
        uint32_t idle_axi_read_cycles = 0;
        uint32_t d_mismatch_reports = 0;

        auto next_rand = [&]() {
            rng = prbs_mix(rng + 0x9e3779b9u);
            return rng;
        };
        auto chunk_words = [&]() {
            return 1u + (next_rand() & 7u);
        };
        auto circular_addr = [&](uint32_t base, uint32_t pos) {
            return base + (pos % half_bytes);
        };

        // Three actors run every clock: instruction-cache random fills, d-cache
        // PRBS write/read traffic, and an AXI master doing the mirrored read/write.
        const uint64_t max_cycles = (uint64_t)target_bytes * 24 + 20000;
        for (uint64_t n = 0; n < max_cycles; ++n) {
            if (i_active) {
                if (!L2_VALUE(L2_I_WAIT_OUT)) {
                    read = false;
                    i_active = false;
                }
            } else if ((next_rand() & 0x1fu) == 0) {
                i_addr = (PRBS_REGION_SIZE - 0x10000u) + ((next_rand() & 0xffffu) & ~3u);
                read = true;
                i_active = true;
            }

            if (d_op == DOp::Write && !L2_VALUE(L2_D_WAIT_OUT)) {
                d_write_pos += 4;
                --d_chunk_left;
                write = false;
                d_op = DOp::Idle;
                idle_d_cycles = 0;
            } else if (d_op == DOp::Read && !L2_VALUE(L2_D_WAIT_OUT)) {
                uint32_t beat_word = ((d_addr % (PORT_BITS / 8)) / 4);
                uint32_t data = port_word(L2_VALUE(L2_D_READ_DATA_OUT), beat_word);
                uint32_t expected = prbs_word(d_active_pos, seed_axi_to_d);
                d_read = false;
                d_op = DOp::Idle;
                idle_d_cycles = 0;
                if (data == expected) {
                    d_read_pos += 4;
                    --d_chunk_left;
                } else if (d_mismatch_reports < 4) {
                    std::print("\ncycle PRBS d-read mismatch addr={:#x} pos={} data={:#x} expected={:#x}\n",
                        d_addr, d_active_pos, data, expected);
                    ++d_mismatch_reports;
                }
            } else if (d_op == DOp::Idle) {
                bool writer_has_room = (d_write_pos < target_bytes) &&
                    (d_write_pos - axi_read_pos < half_bytes - ring_guard);
                bool reader_needs_data = d_read_pos < target_bytes && axi_write_pos > d_read_pos;
                if (d_chunk_left == 0) {
                    d_chunk_left = chunk_words();
                }
                if (reader_needs_data && (!writer_has_room || (next_rand() & 1u))) {
                    d_active_pos = d_read_pos;
                    d_addr = circular_addr(second_base, d_read_pos);
                    d_read = true;
                    d_op = DOp::Read;
                } else if (writer_has_room) {
                    d_active_pos = d_write_pos;
                    d_addr = circular_addr(first_base, d_write_pos);
                    wdata = prbs_word(d_write_pos, seed_d_to_axi);
                    wmask = 0xf;
                    write = true;
                    d_op = DOp::Write;
                } else {
                    ++idle_d_cycles;
                }
            }

            if (axi_w_op == AxiWriteOp::Aw && axi_w_age == 0) {
                ++axi_w_age;
            } else if (axi_w_op == AxiWriteOp::Aw && slave_bvalid(0)) {
                cycle(false);
                slave_axi[0].aw.valid = false;
                slave_axi[0].w.valid = false;
                slave_axi[0].b.ready = true;
                axi_write_pos += PORT_BITS / 8;
                axi_w_chunk_left = (axi_w_chunk_left > PORT_BITS / 32) ? (axi_w_chunk_left - PORT_BITS / 32) : 0;
                axi_w_op = AxiWriteOp::Idle;
                continue;
            } else if (axi_w_op == AxiWriteOp::Aw && slave_awready(0) && slave_wready(0)) {
                cycle(false);
                slave_axi[0].aw.valid = false;
                slave_axi[0].w.valid = false;
                slave_axi[0].b.ready = true;
                axi_w_op = AxiWriteOp::B;
                continue;
            } else if (axi_w_op == AxiWriteOp::B && slave_bvalid(0)) {
                cycle(false);
                slave_axi[0].b.ready = false;
                axi_write_pos += PORT_BITS / 8;
                axi_w_chunk_left = (axi_w_chunk_left > PORT_BITS / 32) ? (axi_w_chunk_left - PORT_BITS / 32) : 0;
                axi_w_op = AxiWriteOp::Idle;
                continue;
            } else if (axi_w_op == AxiWriteOp::Idle) {
                if (slave_bvalid(0)) {
                    slave_axi[0].b.ready = true;
                } else if (slave_axi[0].b.ready) {
                    slave_axi[0].b.ready = false;
                } else if (axi_write_pos < target_bytes && (axi_write_pos - d_read_pos < half_bytes - ring_guard)) {
                    if (axi_w_chunk_left == 0) {
                        axi_w_chunk_left = chunk_words();
                    }
                    axi_w_active_pos = axi_write_pos;
                    uint32_t request_addr = circular_addr(second_base, axi_write_pos);
                    slave_axi[0].w.data = 0;
                    for (size_t word = 0; word < PORT_BITS / 32; ++word) {
                        uint32_t pos = axi_write_pos + (uint32_t)word * 4u;
                        slave_axi[0].w.data.bits(word * 32 + 31, word * 32) =
                            pos < target_bytes ? prbs_word(pos, seed_axi_to_d) : 0;
                    }
                    slave_axi[0].w.last = true;
                    slave_axi[0].aw.addr = request_addr;
                    slave_axi[0].aw.id = 9;
                    slave_axi[0].aw.valid = true;
                    slave_axi[0].w.valid = true;
                    axi_w_age = 0;
                    axi_w_op = AxiWriteOp::Aw;
                    (void)axi_w_active_pos;
                }
            }

            if (axi_r_op == AxiReadOp::Ar && slave_rvalid(0)) {
                logic<PORT_BITS> beat = slave_rdata(0);
                uint32_t request_addr = circular_addr(first_base, axi_r_active_pos);
                uint32_t lane = (request_addr % (PORT_BITS / 8)) / 4;
                uint32_t data = (uint32_t)beat.bits(lane * 32 + 31, lane * 32);
                uint32_t expected = prbs_word(axi_r_active_pos, seed_d_to_axi);
                slave_axi[0].ar.valid = false;
                slave_axi[0].r.ready = true;
                cycle(false);
                slave_axi[0].r.ready = false;
                axi_r_op = AxiReadOp::Idle;
                idle_axi_read_cycles = 0;
                if (data == expected) {
                    axi_read_pos += 4;
                    --axi_r_chunk_left;
                }
                continue;
            } else if (axi_r_op == AxiReadOp::Ar && axi_r_age == 0) {
                ++axi_r_age;
            } else if (axi_r_op == AxiReadOp::Ar && slave_arready(0)) {
                cycle(false);
                slave_axi[0].ar.valid = false;
                slave_axi[0].r.ready = true;
                axi_r_op = AxiReadOp::R;
                continue;
            } else if (axi_r_op == AxiReadOp::R && slave_rvalid(0)) {
                logic<PORT_BITS> beat = slave_rdata(0);
                uint32_t request_addr = circular_addr(first_base, axi_r_active_pos);
                uint32_t lane = (request_addr % (PORT_BITS / 8)) / 4;
                uint32_t data = (uint32_t)beat.bits(lane * 32 + 31, lane * 32);
                uint32_t expected = prbs_word(axi_r_active_pos, seed_d_to_axi);
                cycle(false);
                slave_axi[0].r.ready = false;
                axi_r_op = AxiReadOp::Idle;
                idle_axi_read_cycles = 0;
                if (data == expected) {
                    axi_read_pos += 4;
                    --axi_r_chunk_left;
                }
                continue;
            } else if (axi_r_op == AxiReadOp::Idle) {
                if (slave_axi[0].r.ready) {
                    slave_axi[0].r.ready = false;
                } else if (axi_read_pos < target_bytes && d_write_pos > axi_read_pos) {
                    if (axi_r_chunk_left == 0) {
                        axi_r_chunk_left = chunk_words();
                    }
                    axi_r_active_pos = axi_read_pos;
                    slave_axi[0].ar.addr = circular_addr(first_base, axi_read_pos);
                    slave_axi[0].ar.id = 10;
                    slave_axi[0].ar.valid = true;
                    axi_r_age = 0;
                    axi_r_op = AxiReadOp::Ar;
                } else {
                    ++idle_axi_read_cycles;
                }
            }

            if (d_write_pos >= target_bytes && d_read_pos >= target_bytes &&
                axi_write_pos >= target_bytes && axi_read_pos >= target_bytes &&
                d_op == DOp::Idle && axi_w_op == AxiWriteOp::Idle && axi_r_op == AxiReadOp::Idle) {
                break;
            }

            if (idle_d_cycles > WAIT_LIMIT * 32 || idle_axi_read_cycles > WAIT_LIMIT * 32) {
                break;
            }
            cycle(false);
        }

        if (d_write_pos < target_bytes || d_read_pos < target_bytes ||
            axi_write_pos < target_bytes || axi_read_pos < target_bytes) {
            std::print("\ncycle PRBS ERROR d_write={} d_read={} axi_write={} axi_read={} target={} "
                       "d_op={} axi_w_op={} axi_r_op={} i_active={} i_wait={} d_wait={} "
                       "awready={} wready={} bvalid={} arready={} rvalid={}\n",
                d_write_pos, d_read_pos, axi_write_pos, axi_read_pos, target_bytes,
                (int)d_op, (int)axi_w_op, (int)axi_r_op, i_active,
                L2_VALUE(L2_I_WAIT_OUT), L2_VALUE(L2_D_WAIT_OUT),
                slave_awready(0), slave_wready(0), slave_bvalid(0), slave_arready(0), slave_rvalid(0));
            error = true;
        }

        read = false;
        d_read = false;
        write = false;
        slave_axi[0] = {};
        cycle(false);
        for (size_t i = 0; i < MEM_PORTS; ++i) {
            region_size[i] = saved_region_size[i];
            region_uncached[i] = saved_uncached[i];
        }
    }

    void set_cpu_d_write(size_t index, bool valid, uint32_t addr, uint32_t data)
    {
        if (index == 0) {
            read = false;
            d_read = false;
            write = valid;
            d_addr = addr;
            wdata = data;
            wmask = 0xf;
        }
        else {
            extra_cpu[index].i_read = false;
            extra_cpu[index].d_read = false;
            extra_cpu[index].d_write = valid;
            extra_cpu[index].d_addr = addr;
            extra_cpu[index].write_data = data;
            extra_cpu[index].write_mask = 0xf;
        }
    }

    void set_cpu_i_read(size_t index, bool valid, uint32_t addr)
    {
        if (index == 0) {
            read = valid;
            d_read = false;
            write = false;
            i_addr = addr;
        }
        else {
            extra_cpu[index].i_read = valid;
            extra_cpu[index].d_read = false;
            extra_cpu[index].d_write = false;
            extra_cpu[index].i_addr = addr;
        }
    }

    void multi_cpu_port_pairs_check()
    {
        bool write_pending[CPU_PORTS];
        bool read_pending[CPU_PORTS];
        uint32_t addr[CPU_PORTS];
        uint32_t expected[CPU_PORTS];
        size_t index;
        size_t cycle_count;
        size_t remaining;
        size_t word;
        uint32_t data;
        bool last_cpu_done;

        if (CPU_PORTS <= 1) {
            return;
        }

        remaining = CPU_PORTS;
        for (index = 0; index < CPU_PORTS; ++index) {
            addr[index] = 0x00018004u + (uint32_t)index * 0x40u;
            expected[index] = 0x51000000u | ((uint32_t)index * 0x010101u + 0x1234u);
            write_pending[index] = true;
            read_pending[index] = true;
            set_cpu_d_write(index, true, addr[index], expected[index]);
        }
        for (cycle_count = 0; cycle_count < WAIT_LIMIT * CPU_PORTS * 4 && remaining; ++cycle_count) {
            cycle(false);
            for (index = 0; index < CPU_PORTS; ++index) {
                if (write_pending[index] && !cpu_d_wait(index)) {
                    write_pending[index] = false;
                    set_cpu_d_write(index, false, addr[index], expected[index]);
                    --remaining;
                }
            }
        }
        if (remaining) {
            std::print("\nmulti-CPU D-write arbitration ERROR pending={} CPUs={}\n", remaining, CPU_PORTS);
            error = true;
        }
        cycle(false);

        remaining = CPU_PORTS;
        for (index = 0; index < CPU_PORTS; ++index) {
            set_cpu_i_read(index, true, addr[index]);
        }
        for (cycle_count = 0; cycle_count < WAIT_LIMIT * CPU_PORTS * 4 && remaining; ++cycle_count) {
            cycle(false);
            for (index = 0; index < CPU_PORTS; ++index) {
                if (read_pending[index] && !cpu_i_wait(index)) {
                    word = (addr[index] % (PORT_BITS / 8)) / 4;
                    data = port_word(cpu_i_data(index), word);
                    if (data != expected[index]) {
                        std::print("\nmulti-CPU I-read ERROR cpu={} addr={:#x} data={:#x} expected={:#x}\n",
                            index, addr[index], data, expected[index]);
                        error = true;
                    }
                    read_pending[index] = false;
                    set_cpu_i_read(index, false, addr[index]);
                    --remaining;
                }
            }
        }
        if (remaining) {
            std::print("\nmulti-CPU I-read arbitration ERROR pending={} CPUs={}\n", remaining, CPU_PORTS);
            error = true;
        }
        cycle(false);

        // Keep CPU 0 continuously valid while the last CPU waits. The last CPU
        // must still complete, proving arbitration advances instead of using fixed priority.
        last_cpu_done = false;
        set_cpu_i_read(0, true, addr[0]);
        set_cpu_i_read(CPU_PORTS - 1, true, addr[CPU_PORTS - 1]);
        for (cycle_count = 0; cycle_count < WAIT_LIMIT * CPU_PORTS && !last_cpu_done; ++cycle_count) {
            cycle(false);
            if (!cpu_i_wait(CPU_PORTS - 1)) {
                word = (addr[CPU_PORTS - 1] % (PORT_BITS / 8)) / 4;
                data = port_word(cpu_i_data(CPU_PORTS - 1), word);
                if (data != expected[CPU_PORTS - 1]) {
                    std::print("\nmulti-CPU fairness last CPU data ERROR data={:#x} expected={:#x}\n",
                        data, expected[CPU_PORTS - 1]);
                    error = true;
                }
                last_cpu_done = true;
            }
        }
        if (!last_cpu_done) {
            std::print("\nmulti-CPU round-robin ERROR: last CPU was starved\n");
            error = true;
        }
        set_cpu_i_read(0, false, addr[0]);
        set_cpu_i_read(CPU_PORTS - 1, false, addr[CPU_PORTS - 1]);
        cycle(false);
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR Test{}<SIZE={},WAYS={},PORT_BITS={},MEM_PORTS={},CPU_PORTS={}>...",
            L2CACHE_TEST_TOP_NAME, L2_SIZE, WAYS, PORT_BITS, MEM_PORTS, CPU_PORTS);
#else
        std::print("CppHDL Test{}<SIZE={},WAYS={},PORT_BITS={},MEM_PORTS={},CPU_PORTS={}>...",
            L2CACHE_TEST_TOP_NAME, L2_SIZE, WAYS, PORT_BITS, MEM_PORTS, CPU_PORTS);
#endif
        std::print("\n  features under test:"
                   "\n    - one-clock unified request/response pipeline"
                   "\n    - independent arbitration and responses for every CPU I/D port pair"
                   "\n    - cached CPU read fill and hit"
                   "\n    - instruction-port direct read crossing a cache-line end"
                   "\n    - data-port direct read crossing an L2 beat end"
                   "\n    - immediate CPU hit from a line that was just filled"
                   "\n    - cached CPU partial writes"
                   "\n    - CPU byte stores visible to external AXI/DMA reads"
                   "\n    - dirty eviction"
                   "\n    - external AXI master read of CPU-written cache line"
                   "\n    - CPU read of external AXI-master-written cache line"
                   "\n    - full-width external AXI-master writes"
                   "\n    - same-clock AXI response retirement and next-request capture"
                   "\n    - sticky AXI request payloads are not replayed at response retirement"
                   "\n    - simultaneous CPU/private and two external masters on one line"
                   "\n    - external AXI completion does not release CPU instruction port"
                   "\n    - external AXI request does not hide CPU write completion"
                   "\n    - external AXI request does not drop CPU data-port MMIO read"
                   "\n    - external AXI local addresses with nonzero memory_base"
                   "\n    - uncached/device region CPU and AXI-master accesses"
                   "\n    - cyclic PRBS traffic from I-cache, D-cache, and external AXI actors\n");
        auto start = std::chrono::high_resolution_clock::now();
        _assign();
        preload();
        cycle(true);
        cycle(false);
        for (size_t i = 0; i < SETS + 8 && (L2_VALUE(L2_I_WAIT_OUT) || L2_VALUE(L2_D_WAIT_OUT)); ++i) {
            cycle(false);
        }
        read_check(8, 0);
        read_check(8, 0);
        instruction_cross_line_direct_check();
        data_port_unaligned_beat_end_direct_read_check();
        immediate_hit_after_fill_check();
        write_then_read_check(64 + 4, 0x12345678u, 0xf, 0x12345678u);
        write_then_read_check(64 + 4, 0xabcd0000u, 0xc, 0xabcd5678u);
        stack_alias_check();
        byte_store_check();
        cpu_byte_store_visible_to_axi_master_check(true, false);
        cpu_byte_store_visible_to_axi_master_check(false, false);
        cpu_byte_store_visible_to_axi_master_check(true, true);
        cpu_byte_store_visible_to_axi_master_check(false, true);
        dirty_eviction_check();
        slave_coherence_check();
        slave_response_turnover_check();
        slave_full_width_write_check();
        slave_write_strobe_preserves_neighbor_words_check();
        slave_completion_does_not_release_cpu_iport_check();
        slave_request_does_not_hide_cpu_write_completion_check();
        slave_request_does_not_drop_cpu_dport_read_check();
        local_slave_address_with_nonzero_memory_base_check();
        uncached_device_region_check();
        multi_cpu_port_pairs_check();
        cycle_prbs_test();

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    bool configured_cpu_ports = false;
    int only = -1;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        if (strcmp(argv[i], "--cpu-ports=configured") == 0) {
            configured_cpu_ports = true;
        }
        if (strcmp(argv[i], "--cpu-ports=1") == 0) {
            configured_cpu_ports = false;
        }
        if (argv[i][0] != '-') {
            only = atoi(argv[i]);
        }
    }

    bool ok = true;
    size_t cpu_ports = configured_cpu_ports ? CPUS_PER_L2_CACHE : 1;
#ifndef VERILATOR
    if (!noveril) {
        const auto source_root = CpphdlSourceRootFrom(__FILE__);
        const std::string verilator_source = L2CACHE_TEST_SOURCE_FILE;
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        auto compile_l2 = [&](int index, size_t cache_size, size_t port_bits, size_t mem_ports, size_t ways, size_t cpu_ports) {
            if (only != -1 && only != index) {
                return true;
            }
            return VerilatorCompile(verilator_source, L2CACHE_TEST_TOP_NAME, {},
                {(source_root / "include").string(),
                 (source_root / "tribe").string(),
                 (source_root / "tribe" / "common").string(),
                 (source_root / "tribe" / "cache").string()},
                cache_size, port_bits, LINE_SIZE, ways, 32, 32, mem_ports, cpu_ports);
        };
        ok &= compile_l2(0, 16384, 64, 4, 1, cpu_ports);
        ok &= compile_l2(1, 16384, 64, 4, 2, cpu_ports);
        ok &= compile_l2(2, 16384, 64, 4, 4, cpu_ports);
        // CppHDL currently emits one L2Cache SV module name, and flattened
        // interface widths are fixed by the first specialization. Keep the
        // Verilator sweep on 64-bit ports; the C++ model below still covers
        // the 256-bit configurations.
        ok &= compile_l2(6, 16384, 64, 8, 1, cpu_ports);
        ok &= compile_l2(7, 16384, 64, 8, 2, cpu_ports);
        ok &= compile_l2(8, 16384, 64, 8, 4, cpu_ports);
        ok &= compile_l2(12, 65536, 64, 4, 1, cpu_ports);
        ok &= compile_l2(13, 65536, 64, 4, 2, cpu_ports);
        ok &= compile_l2(14, 65536, 64, 4, 4, cpu_ports);
        ok &= compile_l2(18, 65536, 64, 8, 1, cpu_ports);
        ok &= compile_l2(19, 65536, 64, 8, 2, cpu_ports);
        ok &= compile_l2(20, 65536, 64, 8, 4, cpu_ports);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        auto run_l2 = [&](int index, size_t cache_size, size_t port_bits, size_t mem_ports, size_t ways, size_t cpu_ports) {
            if (only != -1 && only != index) {
                return true;
            }
            std::ostringstream command;
            command << L2CACHE_TEST_TOP_NAME << "_" << cache_size << "_" << port_bits << "_"
                    << LINE_SIZE << "_" << ways << "_32_32_" << mem_ports
                    << "_" << cpu_ports
                    << "/obj_dir/V" << L2CACHE_TEST_TOP_NAME << " --cpu-ports="
                    << (configured_cpu_ports ? "configured" : "1") << " " << index;
            return std::system(command.str().c_str()) == 0;
        };
        ok = ok &&
            run_l2(0, 16384, 64, 4, 1, cpu_ports) &&
            run_l2(1, 16384, 64, 4, 2, cpu_ports) &&
            run_l2(2, 16384, 64, 4, 4, cpu_ports) &&
            run_l2(6, 16384, 64, 8, 1, cpu_ports) &&
            run_l2(7, 16384, 64, 8, 2, cpu_ports) &&
            run_l2(8, 16384, 64, 8, 4, cpu_ports) &&
            run_l2(12, 65536, 64, 4, 1, cpu_ports) &&
            run_l2(13, 65536, 64, 4, 2, cpu_ports) &&
            run_l2(14, 65536, 64, 4, 4, cpu_ports) &&
            run_l2(18, 65536, 64, 8, 1, cpu_ports) &&
            run_l2(19, 65536, 64, 8, 2, cpu_ports) &&
            run_l2(20, 65536, 64, 8, 4, cpu_ports);
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

#define RUN_L2(index, cache_size, port_bits, mem_ports, ways) \
    if (only == -1 || only == index) { \
        ok = ok && (configured_cpu_ports ? \
            TestL2Cache<cache_size, port_bits, mem_ports, ways, CPUS_PER_L2_CACHE>().run() : \
            TestL2Cache<cache_size, port_bits, mem_ports, ways, 1>().run()); \
    }
    RUN_L2(0, 16384, 64, 4, 1);
    RUN_L2(1, 16384, 64, 4, 2);
    RUN_L2(2, 16384, 64, 4, 4);
    RUN_L2(3, 16384, 256, 4, 1);
    RUN_L2(4, 16384, 256, 4, 2);
    RUN_L2(5, 16384, 256, 4, 4);
    RUN_L2(6, 16384, 64, 8, 1);
    RUN_L2(7, 16384, 64, 8, 2);
    RUN_L2(8, 16384, 64, 8, 4);
    RUN_L2(9, 16384, 256, 8, 1);
    RUN_L2(10, 16384, 256, 8, 2);
    RUN_L2(11, 16384, 256, 8, 4);
    RUN_L2(12, 65536, 64, 4, 1);
    RUN_L2(13, 65536, 64, 4, 2);
    RUN_L2(14, 65536, 64, 4, 4);
    RUN_L2(15, 65536, 256, 4, 1);
    RUN_L2(16, 65536, 256, 4, 2);
    RUN_L2(17, 65536, 256, 4, 4);
    RUN_L2(18, 65536, 64, 8, 1);
    RUN_L2(19, 65536, 64, 8, 2);
    RUN_L2(20, 65536, 64, 8, 4);
    RUN_L2(21, 65536, 256, 8, 1);
    RUN_L2(22, 65536, 256, 8, 2);
    RUN_L2(23, 65536, 256, 8, 4);
#undef RUN_L2
    return !ok;
}

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
