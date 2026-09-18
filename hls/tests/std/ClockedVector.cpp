#include "../../Clocked.h"
#include <vector>
#include <algorithm>

struct VectorMethods {
    std::vector<uint32_t> data;
    static constexpr bool singleClock(uint32_t op) { return op == 3; }
    uint64_t sum(uint32_t bias) {
        uint64_t value = bias;
        for (uint32_t i = 0; i < data.size(); ++i) value += data[i];
        return value;
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 0) {
            if (index == data.size()) data.push_back(value);
            else data[index] = value;
            return data[index];
        }
        if (operation == 1) return sum(value) + sum(index);
        if (operation == 2) { std::fill(data.begin(), data.end(), value); return data.front() + data.back(); }
        if (operation == 4) { data.erase(data.begin() + index); return sum(value); }
        if (operation == 5) { data.clear(); return data.size(); }
        return data[index];
    }
#ifndef SYNTHESIS
    template<class Test> static void extraTests(Test&& test) {
        test(4, 2, 19); test(4, 0, 23); test(5, 0, 0);
        for (unsigned i = 0; i < 8; ++i) test(0, i, i + 81);
        test(1, 11, 17);
    }
#endif
};

class ClockedVectorTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<VectorMethods, 0, 16> worker;
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
int main() { return clockedTest<VectorMethods, ClockedVectorTop>(); }
#endif
