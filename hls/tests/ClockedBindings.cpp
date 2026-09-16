#include "../Clocked.h"
#include <array>
#include <new>

struct BindingsOwner {
    uint32_t* destroyed;
    union { uint32_t value = 23; uint32_t alias; };
    explicit BindingsOwner(uint32_t& count) : destroyed(&count), value(23) {}
    BindingsOwner(const BindingsOwner&) = delete;
    BindingsOwner(BindingsOwner&& other) : destroyed(other.destroyed), value(other.value) { other.destroyed = nullptr; }
    ~BindingsOwner() { if (destroyed) ++*destroyed; }
    static BindingsOwner direct(uint32_t& count) { return BindingsOwner(count); }
    static BindingsOwner named(uint32_t& count) { BindingsOwner owner(count); return owner; }
    static BindingsOwner choose(uint32_t& count, bool keep) {
        BindingsOwner owner(count);
        if (keep) return owner;
        return BindingsOwner(count);
    }
};

// libc++ compressed storage also uses an anonymous struct with a reference.
struct BindingsAnonymousRef {
    struct { uint32_t& value; };
    explicit BindingsAnonymousRef(uint32_t& other) : value(other) {}
};

struct BindingsCounter {
    uint32_t value = 0;
    uint32_t add(uint32_t amount) { this->value += amount; return this->value; }
    BindingsCounter& self() { return *this; }
    uint32_t repeat(uint32_t amount) {
        for (uint32_t i = 0; i < 3; ++i) this->value += amount + i;
        return this->value;
    }
};

struct BindingsPadding { uint64_t guard = 0x12345678; };
struct BindingsOffsetCounter : BindingsPadding, BindingsCounter {
    uint32_t marker = 19;
    uint32_t addBase(uint32_t amount) { return BindingsCounter::add(amount); }
    uint32_t inspect() const { return this->value; }
};

struct BindingsMethods {
    std::array<uint32_t, 8> data{};
    BindingsCounter left, right;
    BindingsOffsetCounter shifted;
    static constexpr bool singleClock(uint32_t op) { return op != 1 && op != 2 && op != 6; }
    uint64_t sum(uint32_t bias) {
        uint64_t total = bias;
        for (uint32_t i = 0; i < data.size(); ++i) total += data[i];
        return total;
    }
    static uint32_t redirect(BindingsCounter*& selected, BindingsCounter& replacement, uint32_t value) {
        selected = &replacement;
        return value;
    }
    static uint32_t aliases(uint32_t& a, uint32_t& b) { a += 7; b *= 3; return a + b; }
    static void indirect(uint32_t* value) { *value += 11; }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 0) { data[index] = value; return data[index]; }
        if (operation == 1) return sum(value) + sum(index);
        if (operation == 2) { data.fill(value); return data[0]; }
        if (operation == 4) {
            left.self().add(value);
            right.self().add(index);
            return uint64_t(left.value) * 65537 + right.value;
        }
        if (operation == 5) {
            BindingsCounter* selected = &left;
            // C++17 evaluates the selected object before redirect changes selected.
            selected->add(redirect(selected, right, value));
            selected->add(index);
            return uint64_t(left.value) * 65537 + right.value;
        }
        if (operation == 6) {
            uint32_t saved = left.repeat(value);
            return uint64_t(saved) * 65537 + right.repeat(index);
        }
        if (operation == 7) {
            uint32_t local = value;
            uint32_t other = index;
            uint32_t first = aliases(local, other);
            uint32_t second = aliases(local, local);
            return uint64_t(first) * 65537 + second;
        }
        if (operation == 8) {
            uint32_t local = value;
            indirect(&local);
            return local;
        }
        if (operation == 9) {
            BindingsCounter& base = shifted;
            const BindingsOffsetCounter& observed = shifted;
            base.self().add(value);
            shifted.addBase(index);
            return (shifted.guard << 32) ^ (uint64_t(shifted.marker) << 1) ^ observed.inspect();
        }
        if (operation == 10) {
            BindingsOffsetCounter local{{value}, {index}, value ^ index};
            local.addBase(7);
            return (local.guard << 32) ^ (uint64_t(local.marker) << 1) ^ local.inspect();
        }
        if (operation == 11) {
            uint32_t destroyed = 0;
            uint32_t observed = 0;
            {
                BindingsOwner first = BindingsOwner::direct(destroyed);
                BindingsOwner second = BindingsOwner::named(destroyed);
                BindingsOwner third = BindingsOwner::choose(destroyed, (index & 1) != 0);
                observed = first.value + second.value + third.value + destroyed * 1000;
            }
            return uint64_t(destroyed) * 65537 + observed;
        }
        if (operation == 12) {
            void* storage = ::operator new(64, std::align_val_t(64));
            auto* pointer = new (storage) uint32_t(value);
            uint64_t result = ((reinterpret_cast<uintptr_t>(pointer) & 63) << 32) | *pointer;
            ::operator delete(pointer, std::align_val_t(64));
            return result;
        }
        if (operation == 13) {
            uint32_t local = value;
            BindingsAnonymousRef ref(local);
            ref.value += index;
            return local;
        }
        return data[index];
    }
#ifndef SYNTHESIS
    template<class Test> static void extraTests(Test&& test) {
        for (unsigned i = 0; i < 8; ++i) {
            test(4, i, i * 11 + 2);
            test(5, i, i * 13 + 7);
            test(6, i, i * 17 + 3);
            test(7, i, i * 19 + 5);
            test(8, i, i * 23 + 9);
            test(9, i, i * 29 + 11);
            test(10, i, i * 31 + 13);
            test(11, i, 0);
            test(12, i, i * 37 + 17);
            test(13, i, i * 41 + 19);
        }
    }
#endif
};

class ClockedBindingsTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<BindingsMethods> worker;
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
int main() { return clockedTest<BindingsMethods, ClockedBindingsTop>(); }
#endif
