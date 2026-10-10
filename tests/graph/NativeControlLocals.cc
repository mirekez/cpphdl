#include "cpphdl.h"
#include <cstdio>
#include <cstdlib>

class ControlLocals : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<8>) data_in;
    _PORT(cpphdl::logic<32>) result_out = _ASSIGN(convert(uint64_t(data_in())));
    _PORT(cpphdl::logic<32>) loop_out = _ASSIGN(loop_convert(uint64_t(data_in())));
    _PORT(cpphdl::logic<32>) checked_out = _ASSIGN(checked);
    _PORT(cpphdl::logic<16>) mask_out = _ASSIGN(mask);
    cpphdl::reg<cpphdl::logic<32>> checked;
    cpphdl::reg<cpphdl::logic<16>> mask;
    void _work(bool) {
        uint16_t count, bytes;
        unsigned index;
        checked._next = checked_convert(uint64_t(data_in()));
        if (uint64_t(data_in()) & 128) {
            bytes = uint64_t(data_in()) & 31;
            if (uint64_t(data_in()) & 64) bytes = bytes << 1;
            if (bytes > 16) { count = 16; }
            else { count = bytes; }
            mask._next = 0;
            for (index = 0; index < 16; ++index) {
                if (index < count) mask._next[index] = 1;
            }
        }
    }
    void _strobe() { checked.strobe(); mask.strobe(); }
    uint32_t convert(uint32_t value) {
        uint32_t target;
#ifdef CONTROL_LOCALS_BAD
        if (value < 16) target = value + 1;
#else
        if (value == 0) return 42;
        if (value >= 200) return 99;
        target = value - 1;
        if (target & 1) ++target;
#endif
        return target;
    }
    uint32_t loop_convert(uint32_t value) {
        uint32_t sum, index;
        if (value == 0) return 7;
        sum = 0;
        for (index = 0; index < 8; ++index) {
            if (index >= value) break;
            if ((index & 1) == 0) continue;
            sum += index;
        }
        return sum + index * 100;
    }
    uint32_t checked_convert(uint32_t value) {
        uint32_t target;
        if (value == 255) { printf("bad input\n"); exit(1); }
        target = value + 1;
        return target;
    }
};
extern ControlLocals cpphdl_top;

#ifdef CHECK_CONTROL_LOCALS
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    cpphdl_native::Model model;
    ControlLocals original;
    unsigned expected_mask = 0;
    for (unsigned value = 0; value < 256; ++value) {
        model.data[0] = value;
        model.eval(false);
        if (model.result[0] != original.convert(value) || model.loop[0] != original.loop_convert(value)) return 1;
        if (value == 255) {
            try { model.eval(true); return 3; } catch (const std::runtime_error&) {}
        }
        else {
            model.eval(true); model.eval(false);
            if (model.checked[0] != original.checked_convert(value)) return 2;
            if (value & 128) expected_mask = (1U << std::min((value & 31) << ((value & 64) != 0), 16U)) - 1;
            if (model.mask[0] != expected_mask) return 4;
        }
    }
    std::puts("early returns: continuation-only locals; loop break/continue preserved PASS");
}
#endif
