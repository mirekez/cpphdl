#include "../../../hls/Clocked.h"

struct AutoMethods {
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        return (value + index) ^ operation;
    }
};

class AutoBase : public cpphdl::Module {
public:
    cpphdl::hls::ClockedDelayer<AutoMethods, 0, 16, 64> delayed;
    cpphdl::hls::ClockedPipeline<AutoMethods, 3> pipeline;
    _PORT(uint64_t) delayed_out;
    _PORT(uint64_t) pipeline_out;
    void _assign() {
        delayed.command_valid_in = _ASSIGN(true);
        delayed.response_ready_in = _ASSIGN(true);
        delayed.operation_in = _ASSIGN(1u);
        delayed.index_in = _ASSIGN(2u);
        delayed.value_in = _ASSIGN(3u);
        delayed._assign();
        pipeline.command_valid_in = _ASSIGN(true);
        pipeline.response_ready_in = _ASSIGN(true);
        pipeline.operation_in = _ASSIGN(4u);
        pipeline.index_in = _ASSIGN(5u);
        pipeline.value_in = _ASSIGN(6u);
        pipeline._assign();
        delayed_out = _ASSIGN(delayed.result_out());
        pipeline_out = _ASSIGN(pipeline.result_out());
    }
    void _work(bool reset) { delayed._work(reset); pipeline._work(reset); }
    void _strobe() { delayed._strobe(); pipeline._strobe(); }
};

class AutoChild : public AutoBase {};

class AutoTop : public cpphdl::Module {
    AutoChild children[2];
public:
    _PORT(uint64_t) delayed_out;
    _PORT(uint64_t) pipeline_out;
    void _assign() {
        unsigned i;
        for (i = 0; i < 2; ++i) children[i]._assign();
        delayed_out = _ASSIGN(children[0].delayed_out());
        pipeline_out = _ASSIGN(children[1].pipeline_out());
    }
    void _work(bool reset) {
        unsigned i;
        for (i = 0; i < 2; ++i) children[i]._work(reset);
    }
    void _strobe() {
        unsigned i;
        for (i = 0; i < 2; ++i) children[i]._strobe();
    }
};

extern AutoTop auto_root;

class PlainTop : public cpphdl::Module {
public:
    _PORT(uint32_t) value_out;
private:
    cpphdl::reg<cpphdl::u32> value_reg;
public:
    void _assign() { value_out = _ASSIGN(value_reg); }
    void _work(bool reset) { value_reg._next = reset ? 0u : uint32_t(value_reg) + 1u; }
    void _strobe() { value_reg.strobe(); }
};

extern PlainTop plain_root;
