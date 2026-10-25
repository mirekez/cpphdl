#include "DimensionTop.h"
#include <array>
#include <cassert>
#include <cstdio>
#ifdef VERILATOR
#include "VMapAdd.h"
#endif
long _system_clock = 0;

class Bench : public cpphdl::Module
{
public:
    bool command = false, response_ready = false, flush_ready = false;
    uint32_t rows = 2, columns = 3, reads = 2, parameter = 0;
    cpphdl::array<2, cpphdl::u32> bases{}, row_stride{}, column_stride{}, sizes{};
    cpphdl::array<1, cpphdl::u32> output_base{}, output_row{}, output_column{}, output_size{};
    struct Memory : cpphdl::Module
    {
        cpphdl::hls::ExternalMemoryIf<> port_in;
        bool ready = false, valid = false, error = false;
        uint64_t data = 0;
        void _assign()
        {
            port_in.ready_out = _ASSIGN(ready);
            port_in.valid_out = _ASSIGN(valid);
            port_in.data_out = _ASSIGN(data);
            port_in.error_out = _ASSIGN(error);
        }
    } mem;
#ifdef VERILATOR
    VMapAdd dut;
#else
    MapAdd dut;
#endif
    struct Sample
    {
        bool accepted, response, request, write, completion, flush;
        uint32_t address, size, fault;
        uint64_t data;
    };
    Bench()
    {
        _assign();
    }
    void _assign()
    {
#ifndef VERILATOR
        dut.command_valid_in = _ASSIGN(command);
        dut.response_ready_in = _ASSIGN(response_ready);
        dut.flush_ready_in = _ASSIGN(flush_ready);
        dut.rows_in = _ASSIGN(rows);
        dut.columns_in = _ASSIGN(columns);
        dut.read_count_in = _ASSIGN(reads);
        dut.write_count_in = _ASSIGN(uint32_t(1));
        dut.operation_in = _ASSIGN(uint32_t(0));
        dut.parameter_in = _ASSIGN(parameter);
        dut.read_base_in = _ASSIGN(bases);
        dut.read_row_stride_in = _ASSIGN(row_stride);
        dut.read_column_stride_in = _ASSIGN(column_stride);
        dut.read_size_in = _ASSIGN(sizes);
        dut.write_base_in = _ASSIGN(output_base);
        dut.write_row_stride_in = _ASSIGN(output_row);
        dut.write_column_stride_in = _ASSIGN(output_column);
        dut.write_size_in = _ASSIGN(output_size);
        assignIf(dut, mem, dut.memory_out, mem.port_in);
        mem._assign();
        dut._assign();
#endif
    }
    Sample tick(bool reset = false)
    {
#ifndef VERILATOR
        if (reset)
        {
            dut._work(true);
            dut._strobe();
            ++_system_clock;
            return {};
        }
#endif
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = reset;
        dut.command_valid_in = command;
        dut.response_ready_in = response_ready;
        dut.flush_ready_in = flush_ready;
        dut.rows_in = rows;
        dut.columns_in = columns;
        dut.read_count_in = reads;
        dut.write_count_in = 1;
        dut.operation_in = 0;
        dut.parameter_in = parameter;
        dut.read_base_in = uint32_t(bases[0]) | (uint64_t(bases[1]) << 32);
        dut.read_row_stride_in = uint32_t(row_stride[0]) | (uint64_t(row_stride[1]) << 32);
        dut.read_column_stride_in = uint32_t(column_stride[0]) | (uint64_t(column_stride[1]) << 32);
        dut.read_size_in = uint32_t(sizes[0]) | (uint64_t(sizes[1]) << 32);
        dut.write_base_in = uint32_t(output_base[0]);
        dut.write_row_stride_in = uint32_t(output_row[0]);
        dut.write_column_stride_in = uint32_t(output_column[0]);
        dut.write_size_in = uint32_t(output_size[0]);
        dut.memory_out___05Fready_in = mem.ready;
        dut.memory_out___05Fvalid_in = mem.valid;
        dut.memory_out___05Fdata_in = mem.data;
        dut.memory_out___05Ferror_in = mem.error;
        dut.eval();
        Sample s{bool(command && dut.command_ready_out),
                 bool(dut.response_valid_out),
                 bool(dut.memory_out___05Fvalid_out),
                 bool(dut.memory_out___05Fwrite_out),
                 bool(mem.valid && dut.memory_out___05Fready_out),
                 bool(dut.flush_valid_out),
                 dut.memory_out___05Faddr_out,
                 dut.memory_out___05Fsize_out,
                 dut.fault_out,
                 dut.memory_out___05Fdata_out};
        dut.clk = 1;
        dut.eval();
#else
        Sample s{command && dut.command_ready_out(),
                 dut.response_valid_out(),
                 dut.memory_out.valid_in(),
                 dut.memory_out.write_in(),
                 mem.valid && dut.memory_out.ready_in(),
                 dut.flush_valid_out(),
                 uint32_t(dut.memory_out.addr_in()),
                 dut.memory_out.size_in(),
                 dut.fault_out(),
                 dut.memory_out.data_in()};
        dut._work(reset);
        dut._strobe();
#endif
        ++_system_clock;
        return s;
    }
};
int main()
{
    Bench b;
    for (int i = 0; i < 4; ++i)
        b.tick(true);
    for (unsigned scenario = 0; scenario < 16; ++scenario)
    {
        std::array<uint32_t, 128> ram{}, expected{};
        for (unsigned i = 0; i < ram.size(); ++i)
            ram[i] = expected[i] = i * 7919 + scenario;
        b.rows = 2;
        b.columns = 3;
        b.reads = scenario % 4 == 0 ? 0 : 2;
        b.parameter = scenario * 17;
        b.bases[0] = 0;
        b.bases[1] = 128;
        b.row_stride[0] = b.row_stride[1] = 16;
        b.column_stride[0] = 4;
        b.column_stride[1] = scenario % 2 ? 0 : 4;
        b.sizes[0] = b.sizes[1] = 4;
        b.output_base[0] = 256;
        b.output_row[0] = 16;
        b.output_column[0] = 4;
        b.output_size[0] = 4;
        const bool invalid = scenario == 14, inject = scenario == 15;
        if (invalid)
            b.output_base[0] = 2;
        for (unsigned r = 0; r < 2; ++r)
            for (unsigned c = 0; c < 3; ++c)
                expected[64 + 4 * r + c] =
                    (b.reads ? ram[4 * r + c] + ram[32 + 4 * r + (scenario % 2 ? 0 : c)] : 0) +
                    b.parameter;
        bool pending = false, done = false, held = false;
        unsigned due = 0, requests = 0, answers = 0;
        Bench::Sample previous{};
        b.command = true;
        b.response_ready = false;
        for (unsigned cycle = 0; cycle < 2000 && !done; ++cycle)
        {
            b.mem.ready = cycle % 7 >= 2;
            b.mem.valid = pending && cycle >= due;
            b.flush_ready = cycle % 9 == 0;
            auto s = b.tick();
            if (s.accepted)
                b.command = false;
            if (held)
                assert(s.request && s.address == previous.address && s.write == previous.write &&
                       s.data == previous.data && s.size == previous.size);
            held = s.request && !b.mem.ready;
            previous = s;
            if (s.request && b.mem.ready)
            {
                assert(!pending && s.size == 4 && s.address % 4 == 0 && s.address / 4 < ram.size());
                pending = true;
                due = cycle + 2 + cycle % 5;
                ++requests;
                b.mem.error = inject && requests == 1;
                b.mem.data = ram[s.address / 4];
                if (s.write)
                    ram[s.address / 4] = uint32_t(s.data);
            }
            if (s.completion)
            {
                pending = false;
                b.mem.error = false;
            }
            if (s.response)
            {
                assert(s.fault == (invalid ? 1u : inject ? 2u : 0u));
                assert(!pending && s.flush);
                ++answers;
                if (answers == 5)
                    b.response_ready = true;
                if (answers == 6)
                    done = true;
            }
        }
        assert(done && !b.command);
        if (invalid)
            assert(requests == 0);
        else if (!inject)
        {
            assert(requests == 6 * (b.reads + 1));
            assert(ram == expected);
        }
        b.response_ready = false;
        b.mem.valid = false;
        b.tick();
    }
    std::puts("PASS affine map: delayed memory, stalls, broadcast, constant map, faults and held "
              "completion");
}
