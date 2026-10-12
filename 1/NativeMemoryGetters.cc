#include "cpphdl.h"

class MemoryGetters : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<5>) address_in;
    _PORT(cpphdl::logic<64>) data_in;
    _PORT(cpphdl::logic<64>) first_out = _ASSIGN(first);
    _PORT(cpphdl::logic<64>) second_out = _ASSIGN(second);
    _PORT(cpphdl::logic<64>) forwarded_out = _ASSIGN(forwarded);
    _PORT(cpphdl::logic<1>) held_out = _ASSIGN(held);
    cpphdl::reg<cpphdl::logic<1>> held;
    cpphdl::reg<cpphdl::logic<5>> cursor;
    cpphdl::reg<cpphdl::logic<64>> first, second, forwarded;
    cpphdl::memory<cpphdl::logic<64>, 1, 32> rows;
    cpphdl::logic<64> committed_comb, pending_comb;
    cpphdl::logic<64>& committed_func() {
        committed_comb = rows[uint64_t(cursor._next)];
        return committed_comb;
    }
    cpphdl::logic<64>& pending_func() {
        pending_comb = rows.pending(uint64_t(cursor._next));
        return pending_comb;
    }
    void _work(bool reset) {
        if (reset) { cursor.clr(); first.clr(); second.clr(); forwarded.clr(); return; }
        cursor._next = address_in();
        first._next = committed_func();
        rows[uint64_t(cursor._next)] = data_in();
        cursor._next = cursor._next + 1;
        second._next = committed_func();
        cursor._next = address_in();
        forwarded._next = pending_func();
    }
    void _strobe() { held.strobe(); cursor.strobe(); first.strobe(); second.strobe(); forwarded.strobe(); rows.apply(); }
};
extern MemoryGetters cpphdl_top;

#ifdef CHECK_MEMORY_GETTERS
#include "model.h"
#include <array>
#include <cstdio>
int main() {
    cpphdl_native::Model model;
    std::array<uint64_t, 32> rows{};
    for (unsigned sample = 0; sample < 4096; ++sample) {
        unsigned address = sample % 32;
        uint64_t data = uint64_t(sample + 1) * 0x123456789abcdefULL;
        uint64_t first = rows[address], second = rows[(address + 1) % 32];
        model.address[0] = address; model.data[0] = data; model.data[1] = data >> 32;
        model.work_reset[0] = 0;
        model.eval(false); model.eval(true); model.eval(false);
        auto word = [](const auto& value) { return uint64_t(value[0]) | (uint64_t(value[1]) << 32); };
        if (model.held[0] || word(model.first) != first || word(model.second) != second || word(model.forwarded) != data) {
            std::fprintf(stderr, "memory getter mismatch sample=%u first=%llx/%llx second=%llx/%llx forwarded=%llx/%llx\n",
                sample, (unsigned long long)word(model.first), (unsigned long long)first,
                (unsigned long long)word(model.second), (unsigned long long)second,
                (unsigned long long)word(model.forwarded), (unsigned long long)data);
            return 1;
        }
        rows[address] = data;
    }
    std::puts("bounded memory getters: next-state address changes and pending writes PASS");
}
#endif
