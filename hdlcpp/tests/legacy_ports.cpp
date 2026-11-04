#include <cassert>
#include <cstdio>
#include <unistd.h> // declares sync(), which may hide an unqualified class name
#ifdef USE_VERILATOR
#include "Vlegacy_ports.h"
using Model = Vlegacy_ports;
#else
#include "generated/legacy_ports.h"
using Model = legacy_ports<>;
long _system_clock = 0;
#endif
int main() {
    Model model;
#ifndef USE_VERILATOR
    cpphdl::logic<8> data = 0, enable = 0;
    cpphdl::u32 log_input = 0;
    model.data_in = _ASSIGN(data);
    model.enable_in = _ASSIGN(enable);
    model.log_input_in = _ASSIGN(log_input);
    model._assign();
#endif
    for (unsigned step=0; step<256; ++step) {
        const unsigned value = (step * 37 + 11) & 255, mask = step;
#ifdef USE_VERILATOR
        model.data = value; model.enable = mask; model.log_input = value; model.eval();
        unsigned wide=model.wide, narrow=model.narrow, array=model.array_value, pad=model.pad_value;
        unsigned implicit_value=model.implicit_value, sync_value=model.sync_value;
        unsigned log_value=model.log_value;
        unsigned case_value=model.case_value;
#else
        data=value; enable=mask; log_input=value; ++_system_clock;
        unsigned wide=uint64_t(model.wide_out()), narrow=uint64_t(model.narrow_out());
        unsigned array=uint64_t(model.array_value_out()), pad=uint64_t(model.pad_value_out());
        unsigned implicit_value=uint64_t(model.implicit_value_out()), sync_value=uint64_t(model.sync_value_out());
        unsigned log_value=uint64_t(model.log_value_out());
        unsigned case_value=uint64_t(model.case_value_out());
#endif
        assert(wide == ((value << 1) | 1));
        assert(narrow == (value ^ 0xa5));
        assert(array == value);
        assert(pad == (value & mask));
        assert(implicit_value == ((mask & 1) ? value : 1));
        assert(sync_value == value);
        unsigned expected_log=0;
        for (unsigned remaining=value; remaining; remaining >>= 1) ++expected_log;
        assert(log_value == expected_log);
        const unsigned select=value & 7;
        assert(case_value == (0xa0u | (select==4 ? 2 : select==5 ? 4 : select==6 ? 8 : 1)));
        std::printf("%u %u %u %u\n", wide, narrow, array, pad);
    }
}
