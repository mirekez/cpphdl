#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#if defined(INDEX_GRAPH)
#include "model.h"
#elif defined(INDEX_RTL)
#include "VDynamicStructIndex.h"
#else
#include "generated/DynamicStructIndex.h"
long _system_clock = 0;
#endif

int main() {
#if defined(INDEX_GRAPH)
    cpphdl_native::Model model;
#elif defined(INDEX_RTL)
    VDynamicStructIndex model;
#else
    DynamicStructIndex model;
    cpphdl::logic<1> resetN, bank, valid;
    cpphdl::logic<2> index;
    cpphdl::logic<3> writeEnable;
    cpphdl::logic<13> tag;
    cpphdl::logic<32> payload;
    model.rst_n_in = _ASSIGN(resetN);
    model.bank_in = _ASSIGN(bank);
    model.index_in = _ASSIGN(index);
    model.write_enable_in = _ASSIGN(writeEnable);
    model.tag_in = _ASSIGN(tag);
    model.payload_in = _ASSIGN(payload);
    model.valid_in = _ASSIGN(valid);
    model._assign();
#endif
    std::array<uint64_t, 8> expected{};
    uint64_t random = 0x495c1e053263a785ull;
    for (unsigned sample = 0; sample < 4096; ++sample) {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        const bool reset = sample % 257 == 0;
        const unsigned selected = sample & 7;
        const unsigned enables = sample < 9 ? 7 : (random >> 48) & 7;
        const unsigned nextTag = (random >> 32) & 0x1fff;
        const unsigned nextPayload = random;
        const bool nextValid = random >> 63;
        if (reset) expected.fill(0);
        else {
            auto& row = expected[selected];
            if (enables & 1) row = (row & ~0x1fffffffeull) | (uint64_t(nextPayload) << 1);
            if (enables & 2) row = (row & 0x1ffffffffull) | (uint64_t(nextTag) << 33);
            if (enables & 4) row = (row & ~1ull) | nextValid;
        }
        uint64_t row, packedRow;
        unsigned fieldPayload, fieldTag, fieldValid, packedPayload;
#if defined(INDEX_GRAPH)
        model.rst_n[0] = !reset;
        model.bank[0] = selected >> 2;
        model.index[0] = selected & 3;
        model.write_enable[0] = enables;
        model.tag[0] = nextTag;
        model.payload[0] = nextPayload;
        model.valid[0] = nextValid;
        model.work_reset[0] = reset;
        model.step();
        row = model.row[0] | (uint64_t(model.row[1]) << 32);
        packedRow = model.packed_row[0] | (uint64_t(model.packed_row[1]) << 32);
        fieldPayload = model.field_payload[0];
        fieldTag = model.field_tag[0];
        fieldValid = model.field_valid[0];
        packedPayload = model.packed_payload[0];
#elif defined(INDEX_RTL)
        model.rst_n = !reset;
        model.bank = selected >> 2;
        model.index = selected & 3;
        model.write_enable = enables;
        model.tag = nextTag;
        model.payload = nextPayload;
        model.valid = nextValid;
        model.clk = 0; model.eval();
        model.clk = 1; model.eval();
        model.clk = 0; model.eval();
        row = model.row;
        packedRow = model.packed_row;
        fieldPayload = model.field_payload;
        fieldTag = model.field_tag;
        fieldValid = model.field_valid;
        packedPayload = model.packed_payload;
#else
        resetN = !reset;
        bank = selected >> 2;
        index = selected & 3;
        writeEnable = enables;
        tag = nextTag;
        payload = nextPayload;
        valid = nextValid;
        ++_system_clock;
        model._work(reset);
        model._strobe();
        ++_system_clock;
        row = uint64_t(model.row_out());
        packedRow = uint64_t(model.packed_row_out());
        fieldPayload = uint64_t(model.field_payload_out());
        fieldTag = uint64_t(model.field_tag_out());
        fieldValid = uint64_t(model.field_valid_out());
        packedPayload = uint64_t(model.packed_payload_out());
#endif
        if (row != expected[selected] || packedRow != row ||
            fieldPayload != unsigned(row >> 1) || fieldTag != unsigned(row >> 33) ||
            fieldValid != (row & 1) || packedPayload != fieldPayload) {
            std::fprintf(stderr, "sample=%u row=%llx packed=%llx expected=%llx fields=%x,%x,%u packed_payload=%x\n",
                         sample, static_cast<unsigned long long>(row), static_cast<unsigned long long>(packedRow),
                         static_cast<unsigned long long>(expected[selected]), fieldPayload, fieldTag, fieldValid, packedPayload);
            return 1;
        }
    }
    std::puts("dynamic nested packed/unpacked struct reads and partial writes: 4096 cycles PASS");
}
