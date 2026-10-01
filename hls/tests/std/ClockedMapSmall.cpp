#include "../../Clocked.h"
#include "../ClockedOptions.h"
#include <map>

#ifndef _LIBCPP_VERSION
#error "HLS container examples require libc++ headers"
#endif

// A four-entry table with arbitrary 32-bit keys/values. No erase means the
// monotonic heap needs only four node allocations between resets.
struct MapSmallMethods {
    std::map<uint32_t, uint32_t> data;
    static constexpr bool singleClock(uint32_t op) { return op == 6; }
    uint64_t sum(uint32_t bias) {
        uint64_t total = bias;
        for (auto it = data.begin(); it != data.end(); ++it)
            total = total * 131 + uint64_t(it->first) * 65537 + it->second;
        return total;
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 0) {
            auto it = data.find(index);
            if (it != data.end()) { it->second = value; return value; }
            if (data.size() == 4) return UINT64_MAX;
            data.emplace(index, value);
            return value;
        }
        if (operation == 1) return sum(value + index);
        if (operation == 2) {
            for (auto it = data.begin(); it != data.end(); ++it) it->second = value;
            return data.size();
        }
        if (operation == 6) return data.size();
        auto it = data.find(index);
        return it == data.end() ? UINT64_MAX : it->second;
    }
#ifndef SYNTHESIS
    template<class Test> static void extraTests(Test&& test) {
        test(6, 0, 0);
        test(0, 99, 1); test(3, 99, 0); test(6, 0, 0);
        for (unsigned i = 0; i < 4; ++i) {
            test(0, i, 0xffff1234u + i);
            test(3, i, 0);
        }
        test(1, 23, 47);
    }
#endif
};

class ClockedMapSmallTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<MapSmallMethods, 0, 16, 192, HLS_SHARED_MEMORY, HLS_BLOCK_RAM> worker;
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
int main() { return clockedTest<MapSmallMethods, ClockedMapSmallTop>(); }
#endif
