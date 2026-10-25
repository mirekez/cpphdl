#include "cpphdl.h"
#include <cstdint>
using namespace cpphdl;

struct Fields {
    uint32_t low : 5;
    int32_t signed_value : 7;
    uint32_t high : 20;
};
union Word {
    uint32_t raw;
    Fields fields;
};
struct Envelope { Word word; };
class NativeRecords : public Module {
public:
    _PORT(uint32_t) data_in;
    _PORT(uint32_t) result_out = _ASSIGN(state.word.raw);
    _PORT(int32_t) sign_value_out = _ASSIGN(state.word.fields.signed_value);
    _PORT(array<16, Word>) operands_out = _ASSIGN_COMB(operands_comb_func());
    reg<Envelope> state;
    array<16, Word> operands_comb;
    array<16, Word>& operands_comb_func() {
        unsigned lane;
        for (lane = 0; lane < 16; ++lane) {
            operands_comb[lane].raw = data_in() + lane;
            if (data_in() & 0x80000000U) {
                operands_comb[lane].raw ^= 0x80000000U;
            }
        }
        return operands_comb;
    }
    void update(Word& word, bool& reset);
    void _work(bool reset) {
        Word word{};
        word.raw = data_in();
        update(word, reset);
        state._next.word = word;
    }
    void _strobe() { state.strobe(); }
};
void NativeRecords::update(Word& word, bool& reset) {
    word.fields.low = word.fields.low + 3;
    word.fields.signed_value = word.fields.signed_value - 1;
    if (reset) word = Word{.fields = {3, -2, 0x12345}};
}
extern NativeRecords cpphdl_top;
#ifdef CHECK_NATIVE_RECORDS
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    NativeRecords dut{};
    cpphdl_native::Model model;
    uint32_t data = 0;
    dut.data_in = _ASSIGN(data);
    for (unsigned i = 0; i < 4096; ++i) {
        data = i * 1234567u;
        bool reset = i % 19 == 0;
        model.data[0] = data;
        model.work_reset[0] = reset;
        ++_system_clock;
        dut._work(reset); dut._strobe();
        model.step();
        if (model.result[0] != dut.result_out() ||
            int32_t(model.sign_value[0]) != dut.sign_value_out()) {
            std::printf("sample=%u data=%08x reset=%u raw=%08x/%08x signed=%d/%d\n",
                i, data, reset, model.result[0], dut.result_out(),
                int32_t(model.sign_value[0]), dut.sign_value_out());
            return 1;
        }
        for (unsigned lane = 0; lane < 16; ++lane)
            if (model.operands[lane] != dut.operands_out()[lane].raw) return 2;
    }
    std::puts("native record bitfields and union aliases: 4096 samples PASS");
}
#endif
