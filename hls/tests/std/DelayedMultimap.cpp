#include "../../Clocked.h"
#include "../DelayedOptions.h"
#include <map>

#ifndef _LIBCPP_VERSION
#error "HLS container examples require libc++ headers"
#endif

struct MultimapMethods {
    std::multimap<uint32_t, uint32_t> data;
    static constexpr bool singleClock(uint32_t op) { return op == 6; }
    uint64_t sum(uint32_t bias) {
        uint64_t result = bias;
        for (auto it = data.begin(); it != data.end(); ++it)
            result = result * 131 + uint64_t(it->first) * 65537 + it->second;
        return result;
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 0) { data.emplace(index, value); return data.size(); }
        if (operation == 1) return sum(value + index);
        if (operation == 2) {
            for (auto it = data.begin(); it != data.end(); ++it) it->second = value;
            return data.size();
        }
        if (operation == 5) { data.clear(); return data.size(); }
        if (operation == 6) return data.size();
        if (operation == 7) return data.count(index);
        auto it = data.find(index);
        if (it == data.end()) return operation == 4 ? 0 : UINT64_MAX;
        if (operation == 4) { data.erase(it); return 1; }
        return it->second;
    }
#ifndef SYNTHESIS
    template<class Test> static void extraTests(Test&& test) {
        test(3, 999, 0); test(7, 999, 0);
        for (unsigned value : {101u, 202u, 303u}) test(0, 3, value);
        test(7, 3, 0); test(1, 11, 17);
        for (unsigned i = 0; i < 4; ++i) {
            test(3, 3, 0); test(4, 3, 0); test(7, 3, 0);
        }
        for (unsigned key : {0u, 7u, 4u, 1u, 6u, 2u, 5u}) test(4, key, 0);
        test(5, 0, 0); test(6, 0, 0);
        for (unsigned key : {40u, 20u, 60u, 10u, 30u, 50u, 70u, 25u, 35u})
            test(0, key, key + 81);
        test(4, 40, 0); test(1, 19, 23);
    }
#endif
};

class DelayedMultimapTop : public cpphdl::Module {
public:
    cpphdl::hls::ClockedDelayer<MultimapMethods, 8, 16, 4096, true, HLS_BLOCK_RAM> worker;
    _PORT(bool) command_valid_in;
    _PORT(uint32_t) operation_in;
    _PORT(uint32_t) index_in;
    _PORT(uint32_t) value_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_ready_in;
    _PORT(bool) response_valid_out;
    _PORT(uint64_t) result_out;
    _PORT(uint32_t) fault_out;
    void _assign() {
        worker.command_valid_in = _ASSIGN(command_valid_in());
        worker.operation_in = _ASSIGN(operation_in());
        worker.index_in = _ASSIGN(index_in());
        worker.value_in = _ASSIGN(value_in());
        worker.response_ready_in = _ASSIGN(response_ready_in());
        worker._assign();
        command_ready_out = _ASSIGN(worker.command_ready_out());
        response_valid_out = _ASSIGN(worker.response_valid_out());
        result_out = _ASSIGN(worker.result_out());
        fault_out = _ASSIGN(worker.fault_out());
    }
    void _work(bool reset) { worker._work(reset); }
    void _strobe() { worker._strobe(); }
};

#ifndef SYNTHESIS
#include "../DelayedTest.h"
int main() { return delayedTest<MultimapMethods, DelayedMultimapTop>(); }
#endif
