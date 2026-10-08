#include <cpphdl.h>
using namespace cpphdl;
#ifndef COPY_LANES
#define COPY_LANES 1
#endif
#ifndef COPY_CONTROL
#define COPY_CONTROL 0
#endif

class LocalRegCopy : public Module {
public:
    using payload_t = array<COPY_LANES, logic<8>>;
    _PORT(logic<8>) data_in;
    _PORT(logic<8>) result_out, last_out, committed_copy_out;
    _PORT(logic<64>) audit_out;
    reg<payload_t> state;
    reg<logic<64>> audit;
    reg<logic<8>> committed_copy;
    logic<8> result_comb;

    logic<8>& result_comb_func() {
#if COPY_CONTROL == 1
        payload_t snapshot = state;
#elif COPY_CONTROL == 2
        const auto& snapshot = state;
#else
        auto snapshot = state;
#endif
#if COPY_CONTROL == 3
        auto read_copy = [snapshot]() { return pack_value<8>(snapshot[0]); };
        result_comb = read_copy();
#else
        result_comb = pack_value<8>(snapshot[0]);
#endif
        return result_comb;
    }
    reg<payload_t> roundtrip(reg<payload_t> copy) {
        copy[0] = uint64_t(copy[0]) + 3;
        if (data_in()[0]) {
            copy._next[0] = uint64_t(copy._next[0]) + 4;
            return copy;
        }
        copy._next[0] = uint64_t(copy._next[0]) + 7;
        return copy;
    }
    void _assign() {
        result_out = _ASSIGN_COMB(result_comb_func());
        last_out = _ASSIGN(state[COPY_LANES-1]);
        audit_out = _ASSIGN_REG(audit);
        committed_copy_out = _ASSIGN_REG(committed_copy);
    }
    void _work(bool reset) {
        // Deliberately make current and next different before copying.
        for (unsigned i = 0; i < COPY_LANES; ++i) state._next[i] = uint64_t(data_in()) + i;
        {
            auto snapshot = state;
            auto other = snapshot;
            auto& alias = other;
            auto assigned = state;
            payload_t payload = state;
            snapshot[0] = uint64_t(data_in()) ^ 0xa5;
            snapshot._next[0] = uint64_t(data_in()) ^ 0x5a;
            alias[0] = uint64_t(alias[0]) + 1;
            alias._next[0] = uint64_t(alias._next[0]) + 2;
            assigned = snapshot;
            {
                auto returned = roundtrip(assigned);
                // Assignment of a payload changes current, not the local _next.
                assigned = payload;
                // Later source mutation must not change any snapshots.
                state._next[0] = uint64_t(data_in()) ^ 0x3c;
                audit._next = uint64_t(snapshot[0]) | (uint64_t(snapshot._next[0]) << 8) |
                    (uint64_t(other[0]) << 16) | (uint64_t(other._next[0]) << 24) |
                    (uint64_t(assigned[0]) << 32) | (uint64_t(assigned._next[0]) << 40) |
                    (uint64_t(returned[0]) << 48) | (uint64_t(returned._next[0]) << 56);
            }
        }
        if (reset) {
            for (unsigned i = 0; i < COPY_LANES; ++i) state._next[i] = 0;
            audit._next = 0;
        }
    }
    void _strobe() {
        auto local = state;
        local._next[0] = uint64_t(local._next[0]) ^ 0xe7;
        local.strobe();
        committed_copy._next = local[0];
        state.strobe();
        audit.strobe();
        committed_copy.strobe();
    }
};
extern LocalRegCopy cpphdl_top;
