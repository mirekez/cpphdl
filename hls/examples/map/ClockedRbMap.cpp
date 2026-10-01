#include "../../Clocked.h"
#include "RbMap.h"
#include "../../tests/ClockedOptions.h"

struct RbMapMethods {
    RbMap<uint32_t, uint32_t> data;
    uint64_t sum(uint32_t bias) {
        uint64_t result = bias;
        for (auto* n = data.first(); n; n = data.next(n))
            result = result * 131 + uint64_t(n->key) * 65537 + n->value;
        return result;
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 0) return data.insert_or_assign(index, value)->value;
        if (operation == 1) return sum(value + index);
        if (operation == 2) {
            for (auto* n = data.first(); n; n = data.next(n)) n->value = value;
            return data.size();
        }
        if (operation == 4) return data.erase(index);
        if (operation == 5) { data.clear(); return data.size(); }
        if (operation == 6) return data.size();
        auto* n = data.find(index);
        return n ? n->value : UINT64_MAX;
    }
};

class ClockedRbMapTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<RbMapMethods, 0, 16, 4096, true, HLS_BLOCK_RAM> worker;
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
#include "../../tests/std/MapMethods.h"
#include "../../tests/ClockedTest.h"
int main() { return clockedTest<MapMethods, ClockedRbMapTop>(); }
#endif
