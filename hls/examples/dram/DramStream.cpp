#include "../../Clocked.h"

// Ordinary C++: the pointer dereference becomes a variable-latency read.
struct DramLoad {
    uint64_t command(uint32_t base, uint32_t index, uint32_t count) {
        auto words = cpphdl::hls::external_memory<const uint64_t>(base);
        uint64_t sum = 0;
        uint32_t i;
        for (i = 0; i < count; ++i) sum += words[index + i];
        return sum;
    }
};

struct DramCalculate {
    __uint128_t command(uint64_t sum, uint64_t metadata, uint64_t scale) const {
        uint64_t value = uint64_t(uint32_t(sum)) * uint32_t(scale);
        value += uint32_t(sum >> 32);
        value = (value ^ (value >> 17)) + 0x12345678u;
        if (metadata >> 32) value = 0;
        return (__uint128_t(metadata) << 64) | value;
    }
};

#include "ReadArbiter.h"

struct DramCompletion {
    uint64_t sum;
    uint32_t tag, scale, error;
    bool valid;
};

#ifndef DRAM_PIPELINE_STAGES
#define DRAM_PIPELINE_STAGES 3
#endif

class DramStream : public cpphdl::Module {
public:
    cpphdl::hls::ClockedMemory<DramLoad> loaders[4];
    ReadArbiter arbiter;
    cpphdl::hls::ClockedPipeline<DramCalculate, DRAM_PIPELINE_STAGES, uint64_t, __uint128_t> calculate;
    cpphdl::hls::ExternalMemoryIf<> memory_out;
    _PORT(bool) valid_in;
    _PORT(bool) ready_out;
    _PORT(uint32_t) index_in;
    _PORT(uint32_t) count_in;
    _PORT(uint32_t) scale_in;
    _PORT(uint32_t) tag_in;
    _PORT(bool) ready_in;
    _PORT(bool) valid_out;
    _PORT(uint64_t) data_out;
    _PORT(uint32_t) tag_out;
    _PORT(uint32_t) error_out;
    _PORT(uint32_t) fault_out;
private:
    cpphdl::reg<cpphdl::u<2>> head_reg, tail_reg;
    cpphdl::reg<cpphdl::u<3>> count_reg;
    cpphdl::reg<cpphdl::array<4, cpphdl::u32>> tags_reg, scales_reg;
    DramCompletion completion_comb;
    const DramCompletion& completion_comb_func() {
        unsigned i;
        completion_comb = DramCompletion{};
        for (i = 0; i < 4; ++i) {
            if (head_reg == i) {
                completion_comb.sum = loaders[i].result_out();
                completion_comb.error = loaders[i].fault_out();
                completion_comb.valid = count_reg != 0 && loaders[i].response_valid_out();
                completion_comb.tag = uint32_t(tags_reg[i]);
                completion_comb.scale = uint32_t(scales_reg[i]);
            }
        }
        return completion_comb;
    }
    bool available_comb;
    const bool& available_comb_func() {
        unsigned i;
        available_comb = false;
        for (i = 0; i < 4; ++i)
            if (tail_reg == i) available_comb = loaders[i].command_ready_out();
        available_comb = available_comb && count_reg < 4 && fault_out() == 0;
        return available_comb;
    }
    uint32_t fault_comb;
    const uint32_t& fault_comb_func() {
        unsigned i;
        fault_comb = calculate.fault_out();
        for (i = 0; i < 4; ++i) fault_comb |= loaders[i].fault_out();
        return fault_comb;
    }
public:
    void _assign() {
        unsigned i;
        for (i = 0; i < 4; ++i) {
            loaders[i].command_valid_in = _ASSIGN_I(valid_in() && ready_out() && tail_reg == i);
            loaders[i].operation_in = _ASSIGN(0x1000u);
            loaders[i].index_in = _ASSIGN(index_in());
            loaders[i].value_in = _ASSIGN(count_in());
            loaders[i].response_ready_in = _ASSIGN_I(count_reg != 0 && head_reg == i && calculate.command_ready_out());
            assignIf(arbiter, loaders[i], arbiter.clients_in[i], loaders[i].memory_out);
        }
        assignIf(*this, arbiter, memory_out, arbiter.memory_out);
        calculate.command_valid_in = _ASSIGN(completion_comb_func().valid);
        calculate.operation_in = _ASSIGN(completion_comb_func().sum);
        calculate.index_in = _ASSIGN(uint64_t(completion_comb_func().tag) |
            (uint64_t(completion_comb_func().error) << 32));
        calculate.value_in = _ASSIGN(uint64_t(completion_comb_func().scale));
        calculate.response_ready_in = _ASSIGN(ready_in());
        calculate._assign();
        ready_out = _ASSIGN(available_comb_func());
        valid_out = _ASSIGN(calculate.response_valid_out());
        data_out = _ASSIGN(uint64_t(calculate.result_out()));
        tag_out = _ASSIGN(uint32_t(calculate.result_out() >> 64));
        error_out = _ASSIGN(uint32_t(calculate.result_out() >> 96));
        fault_out = _ASSIGN(fault_comb_func());
    }
    void _work(bool reset) {
        unsigned i;
        bool push, pop;
        for (i = 0; i < 4; ++i) loaders[i]._work(reset);
        arbiter._work(reset); calculate._work(reset);
        push = valid_in() && ready_out();
        pop = calculate.command_valid_in() && calculate.command_ready_out();
        if (reset) {
            head_reg.clr(); tail_reg.clr(); count_reg.clr(); tags_reg.clr(); scales_reg.clr();
        } else {
            if (push) {
                for (i = 0; i < 4; ++i) if (tail_reg == i) {
                    tags_reg._next[i] = tag_in();
                    scales_reg._next[i] = scale_in();
                }
                tail_reg._next = uint32_t(tail_reg) + 1u;
            }
            if (pop) head_reg._next = uint32_t(head_reg) + 1u;
            if (push != pop) count_reg._next = uint32_t(count_reg) + (push ? 1u : uint32_t(-1));
        }
    }
    void _strobe() {
        unsigned i;
        for (i = 0; i < 4; ++i) loaders[i]._strobe();
        arbiter._strobe(); calculate._strobe();
        head_reg.strobe(); tail_reg.strobe(); count_reg.strobe(); tags_reg.strobe(); scales_reg.strobe();
    }
};

#ifndef SYNTHESIS
#include "DramStreamTest.h"
#endif
