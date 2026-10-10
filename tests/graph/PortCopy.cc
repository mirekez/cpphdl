#include "cpphdl.h"
using namespace cpphdl;
class CopySource : public Module {
public:
    _PORT(uint32_t) data_in;
    uint32_t offset;
    _PORT(uint32_t) initial_out = _ASSIGN(data_in() + offset);
    _PORT(uint32_t) current_out;
    void _assign() { offset = 7u; current_out = _ASSIGN(data_in() + 11u); }
};
class PortCopy : public Module {
public:
    CopySource source;
    _PORT(uint32_t) data_in;
    _PORT(uint32_t) initialized_out = source.initial_out;
    _PORT(uint32_t) copied_out;
    _PORT(uint32_t) rebound_out;
    _PORT(uint32_t) copies[2];
    _PORT(uint32_t) array_sum_out = _ASSIGN(copies[0]() + copies[1]());
    void _assign() {
        source.__inst_name = __inst_name + "/source";
        source.data_in = data_in;
        source._assign();
        copied_out = source.current_out;
        copies[0] = source.current_out;
        source.current_out = _ASSIGN(data_in() + 13u);
        rebound_out = source.current_out;
        copies[1] = source.current_out;
    }
};
extern PortCopy cpphdl_top;
#ifdef CHECK_PORT_COPY
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    PortCopy dut;
    cpphdl_native::Model model;
    uint32_t data = 0;
    dut.data_in = _ASSIGN(data);
    dut._assign();
    for (unsigned i = 0; i < 4096; ++i) {
        data = i * 1234567u;
        model.data[0] = data;
        ++_system_clock;
        model.eval();
        if (dut.initialized_out() != data + 7u || model.initialized[0] != data + 7u ||
            dut.copied_out() != data + 11u || model.copied[0] != data + 11u ||
            dut.rebound_out() != data + 13u || model.rebound[0] != data + 13u ||
            dut.array_sum_out() != 2u * data + 24u || model.array_sum[0] != 2u * data + 24u)
            return 1;
    }
    std::puts("port binding copies and rebinding: 4096 samples PASS");
}
#endif
