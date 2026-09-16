#include "../Clocked.h"
#include <array>

struct ReuseNode {
    ReuseNode* left = nullptr;
    uint32_t value = 0;
    ReuseNode* _M_node_ptr() { return this; }
    static ReuseNode* _S_left(ReuseNode* node) {
        return node->left ? node->left->_M_node_ptr() : nullptr;
    }
    static uint32_t inspect(ReuseNode* node, uint32_t bias) {
        ReuseNode* next = _S_left(node);
        if (!next) return bias;
        if (next->value & 1) return next->value + bias;
        return next->value ^ bias;
    }
};

struct ReuseMethods {
    std::array<uint32_t, 8> data{};
    ReuseNode root, leaf;
    static constexpr bool singleClock(uint32_t op) { return op != 1 && op != 2 && op != 5; }

    uint32_t mix(uint32_t& left, uint32_t& right, uint32_t bias) {
        left += bias;
        right ^= left + 3;
        return left + right;
    }
    uint64_t fold(uint32_t bias) {
        uint64_t total = bias;
        for (uint32_t i = 0; i < data.size(); ++i) total += data[i] * (i + 1);
        return total;
    }
    template<class T> uint64_t widen(T value) { return uint64_t(value) + sizeof(T) * 1000; }
    static void bump(uint32_t& value) { value += 13; }
    static uint64_t copy_paths(uint32_t value, uint32_t index) {
        uint32_t saved = value;
        value = index;
        if (index & 1) value += 7;
        else saved += 11;
        uint32_t second = value;
        bump(value);
        return (uint64_t(saved) << 32) | (second * 127 + value);
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 0) { data[index] = value; return data[index]; }
        if (operation == 1) return fold(value) + fold(index);
        if (operation == 2) { data.fill(value); return data[0]; }
        if (operation == 4) {
            uint32_t first = mix(data[index], data[(index + 1) % 8], value);
            uint32_t second = mix(data[index], data[index], value + 7);
            return uint64_t(first) * 65537 + second;
        }
        if (operation == 5) {
            uint32_t saved = mix(data[index], data[(index + 1) % 8], value);
            uint64_t first = fold(saved);
            return first + fold(value) + saved;
        }
        if (operation == 6)
            return widen(uint8_t(value)) + widen(value) + widen(uint8_t(index)) + widen(index);
        if (operation == 7) {
            uint16_t half = uint16_t(value);
            uint64_t whole = (uint64_t(value) << 32) | index;
            return whole ^ half;
        }
        if (operation == 8) {
            struct Pair { uint64_t low, high; };
            Pair original{(uint64_t(value) << 32) | index, (uint64_t(index) << 32) | value};
            Pair copy = original;
            return copy.low + copy.high;
        }
        if (operation == 9) {
            struct Widths { uint8_t byte; uint16_t half; uint64_t whole; };
            Widths item{uint8_t(index), uint16_t(value), (uint64_t(value) << 32) | index};
            return item.byte + item.half + item.whole;
        }
        if (operation == 10) {
            root.left = (index & 1) ? &leaf : nullptr;
            leaf.value = value;
            uint32_t first = ReuseNode::inspect(&root, index);
            leaf.value += 1;
            return uint64_t(first) * 65537 + ReuseNode::inspect(&root, value);
        }
        if (operation == 11) return copy_paths(value, index) ^ copy_paths(index, value);
        return data[index];
    }
#ifndef SYNTHESIS
    template<class Test> static void extraTests(Test&& test) {
        for (unsigned i = 0; i < 8; ++i) {
            test(4, i, i * 13 + 9);
            test(5, i, i * 31 + 17);
            test(6, i + 256, i * 1000 + 771);
            test(7, i + 256, i * 1000 + 0x12340001u);
            test(8, i + 256, i * 1000 + 0x87654321u);
            test(9, i + 256, i * 1000 + 0x43218765u);
            test(10, i, i * 1000 + 17);
            test(11, i, i * 1000 + 17);
            test(1, i, 33);
        }
    }
#endif
};

class ClockedReuseTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<ReuseMethods> worker;
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
int main() { return clockedTest<ReuseMethods, ClockedReuseTop>(); }
#endif
