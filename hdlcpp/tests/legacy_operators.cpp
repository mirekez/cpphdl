#include <cassert>
#include <cstdio>
#ifdef USE_VERILATOR
#include "Vlegacy_operators.h"
using Model = Vlegacy_operators;
#elif defined(USE_NATIVE)
#include "model.h"
using Model = cpphdl_native::Model;
#else
#include "generated/legacy_operators.h"
using Model = legacy_operators;
long _system_clock = 0;
#endif
int main() {
    Model model;
#if !defined(USE_VERILATOR) && !defined(USE_NATIVE)
    cpphdl::logic<1> reset_n = 0;
    cpphdl::logic<2> address = 0;
    cpphdl::logic<13> value = 0;
    model.rst_n_in = _ASSIGN(reset_n);
    model.addr_in = _ASSIGN(address);
    model.data_in = _ASSIGN(value);
    model._assign();
#endif
    for (unsigned step = 0; step < 260; ++step) {
        unsigned data = (step * 217 + 31) & 8191;
#ifdef USE_VERILATOR
        model.rst_n = step >= 4;
        model.addr = step % 4;
        model.data = data;
        model.clk = 0; model.eval();
        model.clk = 1; model.eval();
        model.clk = 0; model.eval();
        unsigned high=model.high, low=model.low, word=model.xnor_word, bit=model.xnor_bit;
        unsigned count=model.count, repeated_high=model.repeated_high;
        unsigned long long repeated_low=model.repeated_low;
        unsigned other1=model.other1, other2=model.other2, selected=model.selected;
        unsigned loop_bits=model.loop_bits, loop_index=model.loop_index;
#elif defined(USE_NATIVE)
        model.rst_n[0] = step >= 4;
        model.addr[0] = step % 4;
        model.data[0] = data;
        model.step(); model.eval(false);
        unsigned high=model.high[0], low=model.low[0], word=model.xnor_word[0], bit=model.xnor_bit[0];
        unsigned count=model.count[0], repeated_high=model.repeated_high[0];
        unsigned long long repeated_low=uint64_t(model.repeated_low[0]) | (uint64_t(model.repeated_low[1]) << 32);
        unsigned other1=model.other1[0], other2=model.other2[0], selected=model.selected[0];
        unsigned loop_bits=model.loop_bits[0], loop_index=model.loop_index[0];
#else
        reset_n = step >= 4;
        address = step % 4;
        value = data;
        ++_system_clock;
        model._work(false); model._strobe();
        ++_system_clock;
        unsigned high=uint64_t(model.high_out()), low=uint64_t(model.low_out());
        unsigned word=uint64_t(model.xnor_word_out()), bit=uint64_t(model.xnor_bit_out());
        unsigned count=uint64_t(model.count_out()), repeated_high=uint64_t(model.repeated_high_out());
        unsigned long long repeated_low=uint64_t(model.repeated_low_out());
        unsigned other1=uint64_t(model.other1_out()), other2=uint64_t(model.other2_out()), selected=uint64_t(model.selected_out());
        unsigned loop_bits=uint64_t(model.loop_bits_out()), loop_index=uint64_t(model.loop_index_out());
#endif
        assert(high == (step < 4 ? 0x123456 >> 13 : (data ^ 0x155)));
        assert(low == (step < 4 ? 0x123456 & 8191 : data));
        assert(word == ((~(data ^ 0x156)) & 8191));
        assert(bit == ((~(data ^ (data >> 1))) & 1));
        assert(count == (step < 4 ? 0 : (step - 3) & 255));
        const __uint128_t repeated = (__uint128_t(0x2aaaaaaaaaaaaaaaull) << 13) | 0x355;
        assert(repeated_low == uint64_t(repeated));
        assert(repeated_high == (repeated >> 64));
        assert(other1 == (((data >> 1) & 126) | (data & 1)));
        assert(other2 == (((data >> 1) & 124) | (data & 3)));
        assert(selected == ((data >> 1) & 3));
        assert(loop_bits == (step < 4 ? 0 : data & 255));
        assert(loop_index == 8);
        std::printf("%u %u %u %u\n", high, low, word, bit);
    }
}
