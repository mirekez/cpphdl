#include "../../Clocked.h"
#include <array>

struct ClockedTag {
    uint32_t value;
    constexpr explicit ClockedTag(uint32_t n) : value(__builtin_is_constant_evaluated() ? n : n + 100) {}
};
inline constexpr ClockedTag clockedTag{17};
void clockedStore(uint32_t& declarationName, uint32_t declaredValue);
inline void clockedStore(uint32_t& target, uint32_t value) { target = value + 3; }
struct ClockedPadding { uint64_t padding = 91; };
struct ClockedBase { uint32_t number; };
struct ClockedNode : ClockedPadding, ClockedBase {
    uint32_t extra;
    ClockedNode(uint32_t n, uint32_t e) : ClockedBase{n}, extra(e) {}
    ClockedNode(uint32_t n) : ClockedNode(n, 29) {}
};

struct ArrayMethods {
    static constexpr bool singleClock(uint32_t op) { return op == 0 || op == 3; }
    std::array<uint32_t, 8> data;
    uint64_t sum(uint32_t bias) {
        uint64_t value = bias;
        for (uint32_t i = 0; i < data.size(); ++i) value += data[i];
        return value;
    }
    struct ScopeCount {
        uint32_t& count;
        ~ScopeCount() { ++count; }
    };
    uint32_t scoped() {
        uint32_t count = 0;
        for (uint32_t i = 0; i < 6; ++i) {
            ScopeCount scope{count};
            if (i == 1) continue;
            if (i == 3) break;
        }
        return count;
    }
    uint64_t combine(uint32_t before, uint64_t after) { return before + after; }
    uint32_t& select(uint32_t i) { return data[i]; }
    void set(uint32_t i, uint32_t value) { return clockedStore(data[i], value); }
    const ClockedTag& tag() { return clockedTag; }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 0) { data[index % data.size()] = value; return data[index % data.size()]; }
        if (operation == 1) return sum(value) + sum(index);
        if (operation == 2) { data.fill(value); return data.front() + data.back(); }
        if (operation == 4) { uint32_t& selected = select(index); selected += sum(value); return selected; }
        if (operation == 5) return combine(data[index], sum(value));
        if (operation == 6) return scoped();
        if (operation == 7) {
            uint32_t i = 0, total = 0;
            while (i < index) {
                uint32_t j = 0;
                do { total += value + i + j; ++j; } while (j < 3);
                ++i;
            }
            return total;
        }
        if (operation == 8) {
            ClockedNode node(value);
            ClockedBase* base = &node;
            ClockedNode* back = static_cast<ClockedNode*>(base);
            ClockedBase* nullBase = nullptr;
            ClockedNode* nullNode = static_cast<ClockedNode*>(nullBase);
            set(index, back->number + back->extra);
            return data[index] + (nullNode == nullptr ? 1000 : 0);
        }
        if (operation == 9) {
            const ClockedTag* selected;
            if (index) selected = &tag();
            else selected = &clockedTag;
            struct Local { uint32_t value; } local{value};
            return selected->value + local.value + (&tag() == selected ? 1000 : 0);
        }
        return data[index % data.size()];
    }
#ifndef SYNTHESIS
    template<class Test> static void extraTests(Test&& test) {
        test(4, 3, 11); test(5, 3, 29); test(6, 0, 0);
        test(7, 0, 3); test(7, 4, 7);
        test(8, 2, 73); test(8, 5, 11);
        test(9, 0, 43); test(9, 1, 73);
    }
#endif
};

class ClockedArrayTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<ArrayMethods> worker;
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
int main() { return clockedTest<ArrayMethods, ClockedArrayTop>(); }
#endif
