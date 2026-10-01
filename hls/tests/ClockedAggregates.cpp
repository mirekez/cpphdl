#include "../Clocked.h"
#include <utility>

struct AggregateFields {
    uint16_t low = 3;
    uint16_t high = 17;
    uint32_t guard = 0x12345678;
    void add(uint32_t value) { low += value; }
    AggregateFields& self() { return *this; }
    uint16_t& selected(bool branch) {
        if (branch) { high += 1; return low; }
        low += 2;
        return low;
    }
};

struct AggregatePadding { uint64_t guard = 0x1122334455667788ull; };
struct AggregateDerived : AggregatePadding, AggregateFields {
    uint32_t tail = 29;
};

struct AggregatesMethods {
    AggregateFields saved;
    static constexpr bool singleClock(uint32_t op) { return op != 1; }
    static void alias(uint16_t& a, uint16_t& b) { a += 7; b *= 3; }
    static void throughPointer(AggregateFields* fields, uint16_t* low) {
        fields->add(11);
        *low += fields->high;
    }
    static AggregateFields copy(AggregateFields value) {
        value.high += 5;
        return value;
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        AggregateFields temporary = saved;
        AggregateFields copied = copy(temporary);
        std::pair<uint32_t, uint32_t> pair(value, index);
        temporary.self().add(pair.first);
        temporary.selected((index & 1) != 0) += 3;
        throughPointer(&temporary, &temporary.low);
        alias(temporary.low, temporary.low);
        temporary.high += pair.second;
        if (operation == 0) saved = temporary;
        if (operation == 1) {
            for (uint32_t i = 0; i < 5; ++i) temporary.add(value + i);
        }
        if (operation == 4) {
            AggregateDerived derived;
            AggregateFields& base = derived;
            base.add(value);
            derived.tail += index;
            return derived.AggregatePadding::guard ^ uint64_t(base.guard) ^
                (uint64_t(base.low) << 32) ^ derived.tail;
        }
        if (operation == 5) {
            // A partial update must retain the other fields across commands.
            saved.low += value;
            return (uint64_t(saved.guard) << 32) | (uint32_t(saved.high) << 16) | saved.low;
        }
        return (uint64_t(temporary.guard) << 32) ^ (uint64_t(copied.low) << 16) ^
            temporary.low ^ (uint64_t(temporary.high) << 48) ^ copied.high;
    }
#ifndef SYNTHESIS
    template<class Test> static void extraTests(Test&& test) {
        for (uint32_t i = 0; i < 8; ++i) {
            test(4, i * 19, 0xfffffff0u + i);
            test(5, i, 0xffffu + i);
            test(3, i, 7);
            test(1, i, 0xfff0u + i);
        }
    }
#endif
};

class ClockedAggregatesTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<AggregatesMethods, 0, 16> worker;
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
#include "ClockedTest.h"
int main() { return clockedTest<AggregatesMethods, ClockedAggregatesTop>(); }
#endif
