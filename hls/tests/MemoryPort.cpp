#include "../Clocked.h"
#include "ClockedOptions.h"
#include <cstdint>
#include <cstdio>

struct MemoryPortMethods {
    struct Pair { uint64_t low, high; };
    Pair* item = new Pair{};
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 0) { *item = Pair{index, value}; return item->low + item->high; }
        if (operation == 1) return item->low + item->high;
        if (operation == 2) {
            *reinterpret_cast<__uint128_t*>(uintptr_t(index)) = (__uint128_t(value) << 64) | value;
            item->low = 999; // Must not execute after the invalid store.
            return 999;
        }
        return *reinterpret_cast<uint64_t*>(uintptr_t(index));
    }
};

class MemoryPortTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<MemoryPortMethods, 0, 16, 64, true, HLS_BLOCK_RAM> worker;
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
long _system_clock = 0;
int main() {
    MemoryPortTop top;
    bool valid = false, ready = false;
    uint32_t op = 0, index = 17, value = 29;
    top.command_valid_in = _ASSIGN(valid);
    top.response_ready_in = _ASSIGN(ready);
    top.operation_in = _ASSIGN(op);
    top.index_in = _ASSIGN(index);
    top.value_in = _ASSIGN(value);
    top._assign();
    auto tick = [&](bool reset = false) { top._work(reset); top._strobe(); ++_system_clock; };
    tick(true);
    valid = true; tick(); valid = false;
    if (!top.response_valid_out() || top.result_out() != 46) return 1;
    ready = true; tick(); ready = false;
    op = 1; valid = true; tick();
    if (!top.response_valid_out() || top.result_out() != 46) return 1;
    std::puts("native wide memory transactions passed");
}
#endif
