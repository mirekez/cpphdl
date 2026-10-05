#include "../Clocked.h"
#include <cstdint>

struct ExternalPointerMethods {
    static uint64_t read_alias(cpphdl::hls::external_ptr<const uint64_t> p, uint32_t index) {
        return p[index];
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        auto wide = cpphdl::hls::external_memory<uint64_t>(0x1000);
        auto words = cpphdl::hls::external_memory<uint32_t>(0x1000);
        uint64_t sum = 0;
        uint32_t i;
        if (operation == 0) return read_alias(wide, index) + value;
        if (operation == 1) { words[index] = value; return words[index] + uint64_t(value); }
        if (operation == 2) {
            for (i = 0; i < index; ++i) sum += read_alias(wide, i);
            return sum;
        }
        if (operation == 3) return *cpphdl::hls::external_memory<uint64_t>(index);
        if (operation == 5) {
            auto p = cpphdl::hls::external_memory<uint8_t>(0x1000);
            p[index] = uint8_t(value);
            return p[index];
        }
        if (operation == 6) {
            auto p = cpphdl::hls::external_memory<int16_t>(0x1000);
            p[index] = int16_t(value);
            return uint64_t(p[index]);
        }
        auto alias = wide + index;
        *alias = *alias + value;
        return *alias;
    }
};

class ExternalPointerTop : public cpphdl::Module {
public:
    cpphdl::hls::ClockedMemory<ExternalPointerMethods> worker;
    cpphdl::hls::ExternalMemoryIf<> memory_out;
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
        assignIf(*this, worker, memory_out, worker.memory_out);
        command_ready_out = _ASSIGN(worker.command_ready_out());
        response_valid_out = _ASSIGN(worker.response_valid_out());
        result_out = _ASSIGN(worker.result_out());
        fault_out = _ASSIGN(worker.fault_out());
    }
    void _work(bool reset) { worker._work(reset); }
    void _strobe() { worker._strobe(); }
};

#ifndef SYNTHESIS
#include <cstdio>
long _system_clock = 0;
int main() {
    uint64_t memory[8] = {10,20,30,40};
    cpphdl::hls::bind_external_memory(memory, sizeof(memory), 0x1000);
    ExternalPointerMethods methods;
    if (methods.command(0,2,7) != 37 || methods.command(2,4,0) != 100 ||
        methods.command(1,4,11) != 22 || methods.command(4,3,2) != 42 ||
        methods.command(5,33,0xab) != 0xab || methods.command(6,18,0xfffe) != UINT64_MAX-1) return 1;
    std::puts("PASS: native external pointer aliases, loads, stores and loops");
}
#endif
