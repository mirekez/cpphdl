#include "cpphdl.h"

#ifndef GRAPH_MEMORY_WORDS
#define GRAPH_MEMORY_WORDS 1
#endif
#ifndef GRAPH_MEMORY_DEPTH
#define GRAPH_MEMORY_DEPTH 17
#endif

class GraphMemory : public cpphdl::Module {
public:
    static constexpr unsigned Words = GRAPH_MEMORY_WORDS, Bits = Words * 64, Depth = GRAPH_MEMORY_DEPTH;
    using Row = cpphdl::logic<Bits>;
    _PORT(Row) data_in, other_in;
    _PORT(cpphdl::logic<5>) address_in, second_in;
    _PORT(cpphdl::logic<Bits / 8>) mask_in;
    _PORT(cpphdl::logic<1>) full_in, overwrite_in;
    _PORT(Row) read_out = _ASSIGN_COMB(committed_func());
    _PORT(Row) auxiliary_out = _ASSIGN(cpphdl::pack_value<Bits>(other[uint64_t(address_in())]));
    _PORT(Row) old_out = _ASSIGN(old.pack());
    _PORT(Row) pending_out = _ASSIGN(forwarded.pack());
    cpphdl::memory<cpphdl::logic<64>, Words, Depth> sram, other;
    cpphdl::reg<Row> old, forwarded;
    Row latest, committed;
    Row& committed_func() {
        committed = cpphdl::pack_value<Bits>(sram[uint64_t(address_in())]);
        return committed;
    }
    Row& pending_func() {
        latest = cpphdl::pack_value<Bits>(sram.pending(uint64_t(address_in())));
        return latest;
    }
    void _work(bool reset) {
        old._next = 0; forwarded._next = 0;
        if (!reset) {
            old._next = committed_func();
            forwarded._next = pending_func();
            if (full_in()) {
                auto snapshot = sram.pending(uint64_t(address_in()));
                snapshot = data_in();
                snapshot.bits(7, 0) = 0xff;
                other[uint64_t(address_in())] = ~data_in();
            }
            for (unsigned byte = 0; byte < Bits / 8; ++byte) {
                if (mask_in()[byte]) {
                    auto row = sram.pending(uint64_t(address_in()));
                    row.bits(byte * 8 + 7, byte * 8) = other_in().bits(byte * 8 + 7, byte * 8);
                    sram[uint64_t(address_in())] = row;
                }
            }
            if (overwrite_in()) sram[uint64_t(second_in())] = other_in();
            old._next = committed_func();
            forwarded._next = pending_func();
        }
    }
    void _strobe() { old.strobe(); forwarded.strobe(); sram.apply(); other.apply(); }
};
GraphMemory cpphdl_top;

#ifdef CPP_GRAPH_MEMORY_RUN
#include "model.h"
#include <array>
#include <cstdio>
long _system_clock = 0;

int main() {
    cpphdl_native::Model model;
    GraphMemory::Row data, other;
    cpphdl::logic<5> address, second;
    cpphdl::logic<GraphMemory::Bits / 8> mask;
    cpphdl::logic<1> full, overwrite;
    cpphdl_top.data_in = _ASSIGN(data); cpphdl_top.other_in = _ASSIGN(other);
    cpphdl_top.address_in = _ASSIGN(address); cpphdl_top.second_in = _ASSIGN(second);
    cpphdl_top.mask_in = _ASSIGN(mask); cpphdl_top.full_in = _ASSIGN(full); cpphdl_top.overwrite_in = _ASSIGN(overwrite);
    using Row = std::array<uint8_t, GraphMemory::Bits / 8>;
    std::array<Row, GraphMemory::Depth> expected{}, alternate{};
    for (unsigned row = 0; row < GraphMemory::Depth; ++row) {
        cpphdl_top.sram[row] = 0; cpphdl_top.other[row] = 0;
    }
    cpphdl_top.sram.apply(); cpphdl_top.other.apply();
    uint64_t random = 0x1234567812345678ull;
    for (unsigned sample = 0; sample < 4000; ++sample) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        unsigned selected = random % GraphMemory::Depth;
        unsigned secondAddress = sample % 2 ? selected : (random >> 16) % GraphMemory::Depth;
        bool reset = sample % 79 == 0;
        address = selected; second = secondAddress;
        full = (random >> 7) & 1; overwrite = (random >> 8) & 1; mask = random >> 32;
        model.address[0] = selected; model.second[0] = secondAddress;
        model.full[0] = uint64_t(full); model.overwrite[0] = uint64_t(overwrite); model.mask[0] = uint64_t(mask);
        model.work_reset[0] = reset;
        for (unsigned byte = 0; byte < GraphMemory::Bits / 8; ++byte) {
            data.bytes[byte] = (random >> ((byte % 8) * 8)) ^ byte;
            other.bytes[byte] = ~data.bytes[byte];
        }
        for (unsigned word = 0; word < GraphMemory::Bits / 32; ++word) {
            model.data[word] = 0; model.other[word] = 0;
            for (unsigned byte = 0; byte < 4; ++byte) {
                model.data[word] |= uint32_t(data.bytes[word * 4 + byte]) << (byte * 8);
                model.other[word] |= uint32_t(other.bytes[word * 4 + byte]) << (byte * 8);
            }
        }
        Row previous = expected[selected];
        ++_system_clock;
        cpphdl_top._work(reset);
        if (cpphdl::pack_value<GraphMemory::Bits>(cpphdl_top.sram[selected]) != cpphdl::pack_value<GraphMemory::Bits>(cpphdl_top.old._next) && !reset) return 1;
        cpphdl_top._strobe();
        ++_system_clock;
        if (!reset) {
            for (unsigned byte = 0; byte < GraphMemory::Bits / 8; ++byte) {
                if (uint64_t(full)) { expected[selected][byte] = data.bytes[byte]; alternate[selected][byte] = other.bytes[byte]; }
                if ((uint64_t(mask) >> byte) & 1) expected[selected][byte] = other.bytes[byte];
                if (uint64_t(overwrite)) expected[secondAddress][byte] = other.bytes[byte];
            }
        }
        model.eval(); model.eval(); model.step(); model.eval();
        auto actual = cpphdl_top.read_out(), otherActual = cpphdl_top.auxiliary_out();
        auto oldActual = cpphdl_top.old_out(), pendingActual = cpphdl_top.pending_out();
        for (unsigned byte = 0; byte < GraphMemory::Bits / 8; ++byte) {
            auto graphByte = [&](const auto& value) { return uint8_t(value[byte / 4] >> ((byte % 4) * 8)); };
            uint8_t oldByte = reset ? 0 : previous[byte], pendingByte = reset ? 0 : expected[selected][byte];
            if (actual.bytes[byte] != expected[selected][byte] || graphByte(model.read) != expected[selected][byte] ||
                otherActual.bytes[byte] != alternate[selected][byte] || graphByte(model.auxiliary) != alternate[selected][byte] ||
                oldActual.bytes[byte] != oldByte || graphByte(model.old) != oldByte ||
                pendingActual.bytes[byte] != pendingByte || graphByte(model.pending) != pendingByte) {
                std::printf("memory mismatch sample=%u byte=%u\n", sample, byte); return 2;
            }
        }
    }
    std::printf("ordinary C++ graph: 4000 %u-bit memory transactions match old/pending reads, masks, collisions and settling\n", GraphMemory::Bits);
}
#endif
