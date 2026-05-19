#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include "../basic/Buffer.cpp"
#if !defined(SYNTHESIS)
#include <print>
#endif

using namespace cpphdl;

template<size_t DATA_WIDTH = 512, size_t AVALON_WIDTH = 512, size_t AVALON_BITS = clog2(AVALON_WIDTH / 8)>
class AvalonEndpoint : public Module
{
    static constexpr size_t DATA_BYTES = (DATA_WIDTH + 7) / 8;
    static constexpr size_t AV_BYTES = AVALON_WIDTH / 8;
    static constexpr size_t DATA_BITS = AV_BYTES * 8;
    static constexpr size_t OUTPUT_BITS = DATA_BITS + 64 + AV_BYTES;

    Buffer<OUTPUT_BITS,2> output_buffer;

public:
    bool debugen_in = false;

    _PORT(array<u8,DATA_BYTES>)  data_in;
    _PORT(bool)                  valid_in;
    _PORT(uint64_t)              addr_in;
    _PORT(uint8_t)               nbytes_in;

    _PORT(bool)                wait_out = _ASSIGN( !output_buffer.ready_out() || hole_delayed );
    _PORT(uint64_t)            avmm_address_out    = _ASSIGN((uint64_t)(output_buffer.data_out() >> DATA_BITS));
    _PORT(array<u8,AV_BYTES>)  avmm_writedata_out  = _ASSIGN((array<u8,AV_BYTES>)output_buffer.data_out());
    _PORT(logic<AV_BYTES>)     avmm_byteenable_out = _ASSIGN((logic<AV_BYTES>)(output_buffer.data_out() >> (DATA_BITS + 64)));
    _PORT(bool)                avmm_write_out      = output_buffer.valid_out;
    _PORT(bool)                avmm_read_out       = _ASSIGN(false);
    _PORT(bool)                avmm_waitrequest_in;
    _PORT(array<u8,AV_BYTES>)  avmm_readdata_in;
    _PORT(bool)                avmm_readdatavalid_in;

    reg<u1> valid_delayed;
    reg<array<u8,DATA_BYTES>> data_delayed;
    reg<u64> addr_delayed;
    reg<u8> nbytes_delayed;
    reg<u1> hole_delayed;
    reg<logic<DATA_BITS>> buffer1_precalc;
    reg<logic<DATA_BITS>> buffer2_precalc;

    reg<u1> buffer1_valid;
    reg<logic<DATA_BITS>> buffer1;
    reg<logic<AV_BYTES>> buffer1_byteenable;
    reg<u64> buffer1_address;

    reg<u1> buffer2_valid;
    reg<logic<DATA_BITS>> buffer2;
    reg<logic<AV_BYTES>> buffer2_byteenable;
    reg<u64> buffer2_address;
    reg<u8> buffer2_pos;

public:
    void _assign()
    {
        output_buffer.valid_in = _ASSIGN_REG(buffer1_valid);
        output_buffer.data_in = _ASSIGN(Cat{buffer1_byteenable,buffer1_address,buffer1});
        output_buffer.ready_in = _ASSIGN(!avmm_waitrequest_in());
        output_buffer._assign();
    }

    void _work(bool reset)
    {
        size_t i;
        uint64_t addr_lo;
        uint64_t addr_hi;
        size_t addr_sub;
        size_t in_addr_sub;
        bool delayed_glue;
        bool buffer1_has_bytes;
        bool tail_valid;
        uint64_t tail_address;
        size_t tail_pos;
        logic<DATA_BITS> wider_bus;

        addr_lo = 0;
        addr_hi = 0;
        addr_sub = 0;
        in_addr_sub = 0;
        delayed_glue = false;
        buffer1_has_bytes = false;
        tail_valid = false;
        tail_address = 0;
        tail_pos = 0;

        if (output_buffer.ready_out()) {
            if (!hole_delayed) {
                valid_delayed._next = valid_in();
                data_delayed._next = data_in();
                addr_delayed._next = addr_in();
                nbytes_delayed._next = nbytes_in();

                wider_bus = (logic<DATA_BITS>)data_in();
                in_addr_sub = addr_in() & ((1ULL << AVALON_BITS) - 1);
                buffer1_precalc._next = wider_bus << (in_addr_sub * 8);
                if (in_addr_sub != 0) {
                    buffer2_precalc._next = wider_bus >> (AVALON_WIDTH - in_addr_sub * 8);
                }
                else {
                    buffer2_precalc._next = 0;
                }
            }
            else if (hole_delayed) {
                valid_delayed._next = valid_delayed;
                data_delayed._next = data_delayed;
                addr_delayed._next = addr_delayed;
                nbytes_delayed._next = nbytes_delayed;
                buffer1_precalc._next = buffer1_precalc;
                buffer2_precalc._next = buffer2_precalc;
                hole_delayed._next = false;
            }
            else {
                valid_delayed._next = false;
                hole_delayed._next = false;
            }

            addr_lo = addr_delayed & ~((1ULL << AVALON_BITS) - 1);
            addr_hi = addr_lo + (1ULL << AVALON_BITS);
            addr_sub = addr_delayed & ((1ULL << AVALON_BITS) - 1);
            delayed_glue = !hole_delayed && ((addr_lo == buffer2_address && addr_sub == buffer2_pos) || !buffer2_valid);

            buffer1._next = 0;
            buffer1_byteenable._next = 0;
            buffer1_has_bytes = false;
            if (buffer2_valid) {
                buffer1._next = buffer2;
                buffer1_has_bytes = buffer2_pos != 0;
                for (i = 0; i < AV_BYTES; ++i) {
                    if (i < buffer2_pos) {
                        buffer1_byteenable._next[i] = buffer2_byteenable[i];
                    }
                }
            }
            buffer1_address._next = buffer2_address;
            buffer1_valid._next = buffer2_valid;

            buffer2._next = 0;
            buffer2_valid._next = false;
            buffer2_byteenable._next = 0;

            if (valid_delayed) {
                if (delayed_glue) {
                    buffer1._next |= buffer1_precalc;
                }
                buffer2._next = buffer2_precalc;
            }

            for (i = 0; i < AV_BYTES; ++i) {
                if (valid_delayed && i < nbytes_delayed) {
                    if (addr_sub + i < AV_BYTES) {
                        if (delayed_glue) {
                            buffer1_byteenable._next[addr_sub + i] = 1;
                            buffer1_valid._next = true;
                            buffer1_has_bytes = true;
                        }
                    }
                    else {
                        buffer2_byteenable._next[addr_sub + i - AV_BYTES] = 1;
                        buffer2_valid._next = true;
                    }
                }
            }

            if (hole_delayed) {
                buffer2_valid._next = false;
            }

            buffer2_address._next = buffer2_address;
            buffer2_pos._next = buffer2_pos;
            if (valid_delayed) {
                buffer1_address._next = addr_lo;
                buffer2_address._next = addr_hi;
                buffer2_pos._next = addr_sub + nbytes_delayed - (1 << AVALON_BITS);
            }
            if (buffer2_valid) {
                buffer1_address._next = buffer2_address;
            }

            if (!buffer1_has_bytes) {
                buffer1_valid._next = false;
            }

            tail_valid = valid_delayed && !hole_delayed && (addr_sub + nbytes_delayed > AV_BYTES);
            tail_address = addr_hi;
            tail_pos = 0;
            if (tail_valid) {
                tail_pos = addr_sub + nbytes_delayed - AV_BYTES;
            }
            if (!hole_delayed) {
                in_addr_sub = addr_in() & ((1ULL << AVALON_BITS) - 1);
                hole_delayed._next = valid_in() && tail_valid && ((addr_in() & ~((1ULL << AVALON_BITS) - 1)) != tail_address || in_addr_sub != tail_pos);
            }
        }

        output_buffer._work(reset);

#if !defined(SYNTHESIS)
        if (debugen_in) {
            std::print("MM_ENDPOINT({:d}), ({:d}){}@{:016x}/{}({:d}) => ({:d}){}@{}/{}({:d}) => ({:d}){}@{:08x}/{}({:d}), buffer2: ({:d}){}@{}/{}, buffer2_pos: {}, addr_sub: {}, addr_lo: {}, buffer1_precalc: {}, buffer2_precalc: {}\n",
                reset, (bool)valid_in(), data_in(), addr_in(), nbytes_in(), (bool)!output_buffer.ready_out(),
                (bool)valid_delayed, data_delayed, addr_delayed, nbytes_delayed, (bool)hole_delayed,
                (bool)avmm_write_out(), avmm_writedata_out(), avmm_address_out(), avmm_byteenable_out(), (bool)avmm_waitrequest_in(),
                (bool)buffer2_valid, buffer2, buffer2_address, buffer2_byteenable, buffer2_pos, addr_sub, addr_lo, buffer1_precalc, buffer2_precalc);
        }
#endif

        if (reset) {
            valid_delayed.clr();
            data_delayed.clr();
            addr_delayed.clr();
            nbytes_delayed.clr();
            hole_delayed.clr();

            buffer1_precalc.clr();
            buffer2_precalc.clr();
            buffer1_valid.clr();
            buffer1.clr();
            buffer1_byteenable.clr();
            buffer1_address.clr();
            buffer2_valid.clr();
            buffer2.clr();
            buffer2_byteenable.clr();
            buffer2_address.clr();
            buffer2_pos.clr();
        }
    }

    void _strobe()
    {
        valid_delayed.strobe();
        data_delayed.strobe();
        addr_delayed.strobe();
        nbytes_delayed.strobe();
        hole_delayed.strobe();

        buffer1_precalc.strobe();
        buffer2_precalc.strobe();
        buffer1_valid.strobe();
        buffer1.strobe();
        buffer1_byteenable.strobe();
        buffer1_address.strobe();
        buffer2_valid.strobe();
        buffer2.strobe();
        buffer2_byteenable.strobe();
        buffer2_address.strobe();
        buffer2_pos.strobe();
        output_buffer._strobe();
    }
};

template class AvalonEndpoint<512, 512, 6>;
template class AvalonEndpoint<256, 512, 6>;
template class AvalonEndpoint<256, 1024, 7>;

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <print>
#include <string>
#include <vector>
#include "../tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

template<size_t DATA_WIDTH = 512, size_t AVALON_WIDTH = 512, size_t AVALON_BITS = clog2(AVALON_WIDTH / 8)>
class TestAvalonEndpoint : public Module
{
    static constexpr size_t BUS_BYTES = (DATA_WIDTH + 7) / 8;
    static constexpr size_t AV_BYTES = AVALON_WIDTH / 8;
    static constexpr size_t MEM_SIZE = 645;
    static constexpr size_t MEM_CNT = 100;

#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    AvalonEndpoint<DATA_WIDTH, AVALON_WIDTH, AVALON_BITS> dut;
#endif

    reg<array<u8, BUS_BYTES>> data_in_reg;
    reg<u1> valid_in_reg;
    reg<u64> addr_in_reg;
    reg<u8> nbytes_in_reg;
    reg<u1> waitrequest_reg;

    uint8_t mem[MEM_CNT][MEM_SIZE] = {};
    uint8_t mem1[MEM_CNT][MEM_SIZE] = {};
    size_t offsets[MEM_CNT] = {};
    size_t sizes[MEM_CNT] = {};

    size_t sent_bytes = 0;
    size_t loop = 0;
    size_t read_loop = 0;
    bool sending = false;
    size_t tsize = 0;
    uint32_t prbs = 0x8b51cafeu ^ AVALON_WIDTH;
    bool error = false;
    bool debug = false;
    bool handshake_regression = false;

    bool wait_out_value = false;
    bool avmm_write_value = false;
    uint64_t avmm_address_value = 0;
    array<u8, AV_BYTES> avmm_writedata_value;
    logic<AV_BYTES> avmm_byteenable_value;

    uint32_t next_prbs()
    {
        uint32_t bit = ((prbs >> 31) ^ (prbs >> 21) ^ (prbs >> 1) ^ prbs) & 1u;
        prbs = (prbs << 1) | bit;
        return prbs;
    }

    bool rnd(unsigned mod)
    {
        return (next_prbs() % mod) == 0;
    }

#ifdef VERILATOR
    template<size_t N, typename Words>
    static void pack_sv_byte_array(Words& dst, const array<u8, N>& src)
    {
        std::memset(&dst, 0, sizeof(dst));
        uint32_t* words = reinterpret_cast<uint32_t*>(&dst);
        for (size_t i = 0; i < N; ++i) {
            size_t bit = i * 8;
            words[bit / 32] |= uint32_t(uint8_t(src[i])) << (bit % 32);
        }
    }

    template<size_t N, typename Words>
    static void unpack_sv_byte_array(array<u8, N>& dst, const Words& src)
    {
        const uint32_t* words = reinterpret_cast<const uint32_t*>(&src);
        for (size_t i = 0; i < N; ++i) {
            size_t bit = i * 8;
            dst[i] = u8((words[bit / 32] >> (bit % 32)) & 0xff);
        }
    }
#endif

    void reset_reference()
    {
        std::memset(mem, 0, sizeof(mem));
        std::memset(mem1, 0, sizeof(mem1));
        sent_bytes = 0;
        loop = 0;
        read_loop = 0;
        sending = false;
        tsize = 0;
        prbs = 0x8b51cafeu ^ AVALON_WIDTH;

        size_t offset = 3;
        for (size_t j = 0; j < MEM_CNT; ++j) {
            sizes[j] = (next_prbs() & 1) ? MEM_SIZE : (next_prbs() % (MEM_SIZE - 10)) + 10;
            offsets[j] = offset;
            offset += sizes[j] + (next_prbs() % 11);
            for (size_t i = 0; i < sizes[j]; ++i) {
                mem[j][i] = next_prbs() & 0xff;
            }
        }
    }

    void drive_dut(bool reset)
    {
#ifdef VERILATOR
        pack_sv_byte_array(dut.data_in, data_in_reg);
        dut.valid_in = valid_in_reg;
        dut.addr_in = addr_in_reg;
        dut.nbytes_in = nbytes_in_reg;
        dut.avmm_waitrequest_in = waitrequest_reg;
        dut.avmm_readdatavalid_in = 0;
        dut.reset = reset;
        dut.eval();

        wait_out_value = dut.wait_out;
        avmm_write_value = dut.avmm_write_out;
        avmm_address_value = dut.avmm_address_out;
        unpack_sv_byte_array(avmm_writedata_value, dut.avmm_writedata_out);
        std::memcpy(&avmm_byteenable_value, &dut.avmm_byteenable_out, sizeof(avmm_byteenable_value));
#else
        dut.debugen_in = debug;
        dut._work(reset);
        wait_out_value = dut.wait_out();
        avmm_write_value = dut.avmm_write_out();
        avmm_address_value = dut.avmm_address_out();
        avmm_writedata_value = dut.avmm_writedata_out();
        avmm_byteenable_value = dut.avmm_byteenable_out();
#endif
    }

    void check_avalon_write()
    {
        if (!avmm_write_value || waitrequest_reg) {
            return;
        }
        if (read_loop >= MEM_CNT) {
            std::print("\nERROR: write after all reference buffers were completed\n");
            error = true;
            return;
        }

        bool seen_byte = false;
        bool seen_gap = false;
        bool non_contiguous = false;
        for (size_t i = 0; i < AV_BYTES; ++i) {
            if (avmm_byteenable_value[i]) {
                if (seen_gap) {
                    non_contiguous = true;
                }
                seen_byte = true;
            }
            else if (seen_byte) {
                seen_gap = true;
            }
        }
        if (!seen_byte) {
            std::print("\nERROR: Avalon write with empty byteenable at cycle {}\n", sys_clock);
            error = true;
            return;
        }
        if (non_contiguous) {
            std::print("\nERROR: non-contiguous byteenable {} at cycle {}\n", avmm_byteenable_value, sys_clock);
            error = true;
            return;
        }

        for (size_t i = 0; i < AV_BYTES; ++i) {
            if (avmm_byteenable_value[i]) {
                uint64_t absolute = avmm_address_value + i;
                if (absolute < offsets[read_loop] || absolute - offsets[read_loop] >= sizes[read_loop]) {
                    std::print("\nERROR: write address 0x{:x} outside buffer {} offset={} size={} at cycle {}\n",
                        absolute, read_loop, offsets[read_loop], sizes[read_loop], sys_clock);
                    error = true;
                    return;
                }
                size_t pos = absolute - offsets[read_loop];
                mem1[read_loop][pos] = avmm_writedata_value[i];
                if (pos == sizes[read_loop] - 1) {
                    ++read_loop;
                }
            }
        }
    }

    void update_source()
    {
        if (!wait_out_value) {
            if (valid_in_reg) {
                sent_bytes += nbytes_in_reg;
                sending = false;
            }
            valid_in_reg._next = false;
        }

        if (loop >= MEM_CNT) {
            return;
        }

        if (!sending && sent_bytes != sizes[loop] && rnd(2)) {
            tsize = next_prbs() % BUS_BYTES + 1;
            if (sent_bytes + tsize > sizes[loop]) {
                tsize = sizes[loop] - sent_bytes;
            }
            sending = true;
        }

        if (sending && !wait_out_value) {
            data_in_reg._next = 0;
            for (size_t i = 0; i < tsize; ++i) {
                data_in_reg._next[i] = mem[loop][sent_bytes + i];
            }
            valid_in_reg._next = true;
            addr_in_reg._next = offsets[loop] + sent_bytes;
            nbytes_in_reg._next = tsize;
        }

        if (sent_bytes == sizes[loop] && !valid_in_reg && !sending) {
            ++loop;
            sent_bytes = 0;
        }
    }

public:
    explicit TestAvalonEndpoint(bool debug_en)
    {
        debug = debug_en;
    }

    void _assign()
    {
#ifndef VERILATOR
        dut.data_in = _ASSIGN_REG(data_in_reg);
        dut.valid_in = _ASSIGN_REG(valid_in_reg);
        dut.addr_in = _ASSIGN_REG(addr_in_reg);
        dut.nbytes_in = _ASSIGN_REG(nbytes_in_reg);
        dut.avmm_waitrequest_in = _ASSIGN_REG(waitrequest_reg);
        dut.avmm_readdata_in = _ASSIGN(array<u8, AV_BYTES>{});
        dut.avmm_readdatavalid_in = _ASSIGN(false);
        dut.__inst_name = __inst_name + "/endpoint";
        dut._assign();
#endif
    }

    void _work(bool reset)
    {
        if (reset) {
            reset_reference();
            data_in_reg.clr();
            valid_in_reg.clr();
            addr_in_reg.clr();
            nbytes_in_reg.clr();
            waitrequest_reg.clr();
        }

#ifdef VERILATOR
        dut.clk = 0;
#endif
        drive_dut(reset);
        if (reset) {
            return;
        }

        if (!handshake_regression) {
            check_avalon_write();
            waitrequest_reg._next = rnd(3);
            update_source();

            if (debug && (sys_clock % 1000) == 0) {
                std::print("cycle={} loop={} read_loop={} sent={} valid={} wait={} av_write={} av_addr=0x{:x} be={}\n",
                    sys_clock, loop, read_loop, sent_bytes, (bool)valid_in_reg, wait_out_value,
                    avmm_write_value, avmm_address_value, avmm_byteenable_value);
            }
        }
    }

    void _strobe()
    {
#ifndef VERILATOR
        dut._strobe();
#endif
        data_in_reg.strobe();
        valid_in_reg.strobe();
        addr_in_reg.strobe();
        nbytes_in_reg.strobe();
        waitrequest_reg.strobe();
    }

    void _work_neg(bool reset)
    {
#ifdef VERILATOR
        dut.clk = 1;
        drive_dut(reset);
#else
        (void)reset;
#endif
    }

    void set_input(uint64_t addr, uint8_t nbytes, uint8_t seed)
    {
        data_in_reg._next = 0;
        for (size_t i = 0; i < BUS_BYTES; ++i) {
            data_in_reg._next[i] = seed + i;
        }
        valid_in_reg._next = true;
        addr_in_reg._next = addr;
        nbytes_in_reg._next = nbytes;
        waitrequest_reg._next = false;
    }

    void tick()
    {
        _strobe();
        ++sys_clock;
        _work(false);
        _work_neg(false);
    }

    void collect_write(std::map<uint64_t, uint8_t>& captured)
    {
        if (!avmm_write_value || waitrequest_reg) {
            return;
        }
        for (size_t i = 0; i < AV_BYTES; ++i) {
            if (avmm_byteenable_value[i]) {
                captured[avmm_address_value + i] = avmm_writedata_value[i];
            }
        }
    }

    bool run_hole_backpressure_regression()
    {
        struct Beat {
            uint64_t addr;
            uint8_t nbytes;
            uint8_t seed;
        };

        const size_t cross_sub = AV_BYTES - BUS_BYTES + 2;
        const Beat beats[] = {
            {0x1000, uint8_t(BUS_BYTES), 0x10},
            {0x1000 + AV_BYTES + cross_sub, uint8_t(BUS_BYTES), 0x20},
            {0x1000 + AV_BYTES * 3 + cross_sub * 2, uint8_t(BUS_BYTES), 0x30},
            {0x1000 + AV_BYTES * 5 + cross_sub * 3, uint8_t(BUS_BYTES), 0x40},
        };
        constexpr size_t BEAT_COUNT = sizeof(beats) / sizeof(beats[0]);
        std::map<uint64_t, uint8_t> expected;
        std::map<uint64_t, uint8_t> captured;

        for (const auto& beat : beats) {
            for (size_t i = 0; i < beat.nbytes; ++i) {
                expected[beat.addr + i] = beat.seed + i;
            }
        }

        handshake_regression = true;
        reset_reference();
        data_in_reg.clr();
        valid_in_reg.clr();
        addr_in_reg.clr();
        nbytes_in_reg.clr();
        waitrequest_reg.clr();
        _work(true);
        _work_neg(true);

        size_t beat_index = 0;
        size_t drain_cycles = 0;
        set_input(beats[beat_index].addr, beats[beat_index].nbytes, beats[beat_index].seed);
        for (size_t cycle = 0; cycle < 200 && (beat_index < BEAT_COUNT || drain_cycles < 20); ++cycle) {
            _strobe();
            ++sys_clock;
            _work(false);
            collect_write(captured);

            if (beat_index < BEAT_COUNT) {
                if (!wait_out_value) {
                    ++beat_index;
                }
                if (beat_index < BEAT_COUNT) {
                    set_input(beats[beat_index].addr, beats[beat_index].nbytes, beats[beat_index].seed);
                }
                else {
                    valid_in_reg._next = false;
                    ++drain_cycles;
                }
            }
            else {
                valid_in_reg._next = false;
                ++drain_cycles;
            }

            _work_neg(false);
        }

        bool ok = true;
        for (const auto& [addr, value] : expected) {
            auto it = captured.find(addr);
            if (it == captured.end()) {
                std::print("\nERROR: hole regression missing byte at 0x{:x}, expected 0x{:02x}\n", addr, value);
                ok = false;
                break;
            }
            if (it->second != value) {
                std::print("\nERROR: hole regression data mismatch at 0x{:x}: expected 0x{:02x}, got 0x{:02x}\n",
                    addr, value, it->second);
                ok = false;
                break;
            }
        }

        handshake_regression = false;
        return ok;
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestAvalonEndpoint<DATA={},AVALON={}>...", DATA_WIDTH, AVALON_WIDTH);
#else
        std::print("CppHDL TestAvalonEndpoint<DATA={},AVALON={}>...", DATA_WIDTH, AVALON_WIDTH);
#endif
        if (debug) {
            std::print("\n");
        }

        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "avalon_endpoint_test";
        _assign();
        if (!run_hole_backpressure_regression()) {
            std::print(" FAILED ({} us)\n",
                (std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock::now() - start)).count());
            return false;
        }
        _work(true);
        _work_neg(true);

        int cycles = 200000;
        while (--cycles && !error) {
            _strobe();
            ++sys_clock;
            _work(false);
            _work_neg(false);
            if (loop >= MEM_CNT && read_loop >= MEM_CNT) {
                break;
            }
        }

        if (loop < MEM_CNT || read_loop < MEM_CNT) {
            std::print("\nERROR: test did not finish: loop={} read_loop={} sent={} cycles_left={}\n",
                loop, read_loop, sent_bytes, cycles);
            error = true;
        }

        for (size_t j = 0; j < MEM_CNT && !error; ++j) {
            for (size_t i = 0; i < sizes[j]; ++i) {
                if (mem[j][i] != mem1[j][i]) {
                    std::print("\nERROR: data mismatch buffer={} pos={}: ref=0x{:02x} got=0x{:02x}\n",
                        j, i, mem[j][i], mem1[j][i]);
                    error = true;
                    break;
                }
            }
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool debug = false;
    bool noveril = false;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        else if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        else {
            positional.emplace_back(argv[i]);
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "AvalonEndpoint", {"Predef_pkg", "Buffer"}, {"../../../../include"}, 512, 512, 6);
        ok &= VerilatorCompile(__FILE__, "AvalonEndpoint", {"Predef_pkg", "Buffer"}, {"../../../../include"}, 256, 512, 6);
        ok &= VerilatorCompile(__FILE__, "AvalonEndpoint", {"Predef_pkg", "Buffer"}, {"../../../../include"}, 256, 1024, 7);
        auto compile_us = (std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count();
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system((std::string("AvalonEndpoint_512_512_6/obj_dir/VAvalonEndpoint 512 512 6") + (debug ? " --debug" : "")).c_str()) == 0;
        ok = ok && std::system((std::string("AvalonEndpoint_256_512_6/obj_dir/VAvalonEndpoint 256 512 6") + (debug ? " --debug" : "")).c_str()) == 0;
        ok = ok && std::system((std::string("AvalonEndpoint_256_1024_7/obj_dir/VAvalonEndpoint 256 1024 7") + (debug ? " --debug" : "")).c_str()) == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    if (positional.size() >= 3) {
        size_t data_width = std::stoull(positional[0]);
        size_t avalon_width = std::stoull(positional[1]);
        size_t avalon_bits = std::stoull(positional[2]);
        if (data_width == 512 && avalon_width == 512 && avalon_bits == 6) {
            return !(ok && TestAvalonEndpoint<512, 512, 6>(debug).run());
        }
        if (data_width == 256 && avalon_width == 512 && avalon_bits == 6) {
            return !(ok && TestAvalonEndpoint<256, 512, 6>(debug).run());
        }
        if (data_width == 256 && avalon_width == 1024 && avalon_bits == 7) {
            return !(ok && TestAvalonEndpoint<256, 1024, 7>(debug).run());
        }
        std::print("Unsupported AvalonEndpoint test parameters: DATA={} AVALON={} BITS={}\n", data_width, avalon_width, avalon_bits);
        return 1;
    }

    ok = ok && TestAvalonEndpoint<512, 512, 6>(debug).run();
    ok = ok && TestAvalonEndpoint<256, 512, 6>(debug).run();
    ok = ok && TestAvalonEndpoint<256, 1024, 7>(debug).run();
    return !ok;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
