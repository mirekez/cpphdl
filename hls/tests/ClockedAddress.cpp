#include "../Clocked.h"
#include <array>

struct AddressMethods {
    std::array<uint64_t, 8> data{};
    uint64_t* saved = nullptr;
    static constexpr bool singleClock(uint32_t op) { return op != 1 && op != 2; }
    uint64_t sum(uint32_t bias) {
        uint64_t total = bias;
        for (uint64_t* p = data.data(); p != data.data() + data.size(); ++p) total += *p;
        return total;
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 0) {
            saved = data.data() + index;
            *saved = (uint64_t(value) << 40) | value;
            return *saved;
        }
        if (operation == 1) return sum(value) + sum(index);
        if (operation == 2) { data.fill((uint64_t(value) << 48) | value); return data[0]; }
        if (operation == 4) return uint64_t(data.data() - (index + data.data()));
        if (operation == 5) return uint64_t((data.data() + index) - data.data());
        if (operation == 6) return *saved;
        return data[index];
    }
#ifndef SYNTHESIS
    template<class Test> static void extraTests(Test&& test) {
        for (unsigned i = 0; i < 8; ++i) {
            test(0, i, 0xabc123u + i);
            test(6, 0, 0);
            test(4, i, 0);
            test(5, i, 0);
        }
        test(1, 123, 456);
    }
#endif
};

class ClockedAddressTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<AddressMethods, 0, 16> narrow_worker;
    cpphdl::hls::Clocked<AddressMethods, 0, 32> wide_worker;
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
        narrow_worker.command_valid_in = _ASSIGN(command_valid_in());
        narrow_worker.operation_in = _ASSIGN(operation_in());
        narrow_worker.index_in = _ASSIGN(index_in());
        narrow_worker.value_in = _ASSIGN(value_in());
        narrow_worker.response_ready_in = _ASSIGN(response_ready_in());
        narrow_worker._assign();
        wide_worker.command_valid_in = _ASSIGN(command_valid_in());
        wide_worker.operation_in = _ASSIGN(operation_in());
        wide_worker.index_in = _ASSIGN(index_in());
        wide_worker.value_in = _ASSIGN(value_in());
        wide_worker.response_ready_in = _ASSIGN(response_ready_in());
        wide_worker._assign();
        command_ready_out = _ASSIGN(narrow_worker.command_ready_out() && wide_worker.command_ready_out());
        response_valid_out = _ASSIGN(narrow_worker.response_valid_out() && wide_worker.response_valid_out());
        result_out = _ASSIGN(narrow_worker.result_out());
        fault_out = _ASSIGN(narrow_worker.fault_out() | wide_worker.fault_out() |
            ((narrow_worker.response_valid_out() && wide_worker.response_valid_out() && narrow_worker.result_out() != wide_worker.result_out()) ? 128u : 0u));
    }
    void _work(bool reset) { narrow_worker._work(reset); wide_worker._work(reset); }
    void _strobe() { narrow_worker._strobe(); wide_worker._strobe(); }
};

#ifndef SYNTHESIS
#include "ClockedTest.h"
int main() { return clockedTest<AddressMethods, ClockedAddressTop>(); }
#endif
