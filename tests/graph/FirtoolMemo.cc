#include "cpphdl.h"
class FirtoolMemo : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<32>) data_in;
    _PORT(cpphdl::logic<32>) result_out;
    cpphdl::logic<32> calculate_cache;
    long calculate_clock = -1;
    cpphdl::logic<32>& calculate() {
        if (calculate_clock == _system_clock) return calculate_cache;
        calculate_clock = _system_clock;
        return calculate_cache = cpphdl::logic<32>(uint64_t(data_in()) * 3 + 7);
    }
    void _assign() { result_out = _ASSIGN(cpphdl::logic<32>(uint64_t(calculate()) + uint64_t(calculate()))); }
};
extern FirtoolMemo cpphdl_top;
#ifdef CHECK_FIRTOOL_MEMO
#include "model.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    FirtoolMemo top;
    cpphdl_native::Model model;
    cpphdl::logic<32> data;
    top.data_in = _ASSIGN(data); top._assign();
    for (unsigned i = 0; i < 4096; ++i) {
        data = uint32_t(i * 987654321u);
        uint32_t expected = (uint32_t(uint64_t(data)) * 3u + 7u) * 2u;
        model.data[0] = uint64_t(data);
        ++_system_clock; model.eval();
        if (model.result[0] != expected || uint64_t(top.result_out()) != expected) return 1;
    }
    std::puts("firtool memoized getters: 4096 samples PASS");
}
#endif
