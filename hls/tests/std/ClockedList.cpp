#include "../../Clocked.h"
#include "../ClockedOptions.h"
#include <list>

#ifndef _LIBCPP_VERSION
#error "HLS container examples require libc++ headers"
#endif

struct ListMethods {
    std::list<uint32_t> data;
    static constexpr bool singleClock(uint32_t op) { return op == 6; }
    uint64_t sum(uint32_t bias) {
        uint64_t result = bias;
        for (auto it = data.begin(); it != data.end(); ++it) result = result * 131 + *it;
        return result;
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 1) return sum(value + index);
        if (operation == 2) {
            for (auto it = data.begin(); it != data.end(); ++it) *it = value;
            return data.size();
        }
        if (operation == 5) { data.clear(); return data.size(); }
        if (operation == 6) return data.size();
        if (operation == 0 && index == data.size()) { data.push_back(value); return data.back(); }
        if (index >= data.size()) return UINT64_MAX;
        auto it = data.begin();
        for (uint32_t i = 0; i < index; ++i) ++it;
        if (operation == 0) { *it = value; return *it; }
        if (operation == 4) { data.erase(it); return data.size(); }
        return *it;
    }
#ifndef SYNTHESIS
    template<class Test> static void extraTests(Test&& test) {
        test(6, 0, 0); test(3, 100, 0);
        test(0, 3, 919);
        test(4, 0, 0); test(4, 3, 0); test(4, 5, 0);
        test(1, 11, 17);
        test(5, 0, 0); test(6, 0, 0);
        for (unsigned i = 0; i < 8; ++i) test(0, i, i + 81);
        test(1, 19, 23);
    }
#endif
};

class ClockedListTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<ListMethods, 8, 16, 4096, true, HLS_BLOCK_RAM> worker;
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
#include "../ClockedTest.h"
int main() { return clockedTest<ListMethods, ClockedListTop>(); }
#endif
