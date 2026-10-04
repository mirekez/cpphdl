#include "cpphdl.h"
using namespace cpphdl;

// The same source is executed as ordinary C++, native graph and Verilated RTL.
class SwitchControl : public Module {
public:
    _PORT(logic<4>) mode_in;
    _PORT(logic<4>) selector_in;
    _PORT(logic<3>) flags_in;
    _PORT(logic<8>) seed_in;
    _PORT(logic<32>) result_out = _ASSIGN_COMB(result_comb_func());
    _PORT(logic<32>) stored_out = _ASSIGN_REG(stored);
    reg<logic<32>> stored;
    logic<32> result_comb;
    logic<32>& result_comb_func() { result_comb = result(); return result_comb; }

    unsigned all_return(unsigned s, unsigned f) {
        switch (s) {
        case 0: return 11;
        case 1:
            switch (f) {
            case 0: return 21;
            case 1: return 22;
            default: return 29;
            }
        default: return 99;
        }
    }
    unsigned mixed(unsigned s, unsigned f, unsigned v) {
        unsigned x = v;
        switch (s) {
        case 0:
        case 1:
            x += 3;
            if (f & 1) return x + 100;
            if (f & 2) break;
            x += 5;
            [[fallthrough]];
        case 2:
            x *= 3;
            if (f & 4) { x += 7; break; }
            return x + 200;
        default: // Default deliberately precedes a later case.
            x += 11;
            if (f & 1) break;
            [[fallthrough]];
        case 3: {
            x += 13;
            if (f & 2) return x + 300;
            break;
            x += 10000; // Unreachable after break.
        }
        case 4:
            return x + 400;
            x += 20000; // Unreachable after return.
        }
        return x + 1000;
    }
    unsigned no_default(unsigned s, unsigned f, unsigned v) {
        unsigned x = v;
        switch (s) {
        case 0: if (f & 1) return x + 1; x += 2; break;
        case 2: x += 4; [[fallthrough]];
        case 3: x += 8; // End-of-switch fall-through is legal too.
        }
        x += 16;
        if (f & 2) return x + 32;
        return x + 64;
    }
    unsigned nested(unsigned s, unsigned f, unsigned v) {
        unsigned x = v;
        switch (s) {
        case 0: {
            switch (f) {
            case 0: x += 1; break;
            case 1: return x + 2;
            default: if (f & 2) break; x += 4;
            }
            x += 8; // Inner break must still reach this.
            if (f & 4) break;
            return x + 16;
        }
        case 1:
            if (f & 1) { if (f & 2) return x + 32; else break; }
            else { x += 64; }
            [[fallthrough]];
        default: x += 128; break;
        }
        return x + 256;
    }
    unsigned loops(unsigned s, unsigned f, unsigned v) {
        unsigned i, j;
        unsigned x = v;
        for (i = 0; i < 4; ++i) {
            switch (s) {
            case 0:
                x += 1;
                if (i == f) break; // Switch break, not loop break.
                x += 2;
                break;
            case 1:
                if (i == f) return x + 100;
                x += 4;
                break;
            case 2:
                if (i == f) continue; // Continue the enclosing loop.
                x += 8;
                break;
            default:
                for (j = 0; j < 3; ++j) {
                    if (j == f) break;
                    x += 16;
                }
                x += 32;
                break;
            }
            x += 64;
            if (i == f && (s & 4)) break; // Actual loop break.
        }
        return x + 1000;
    }
    void early_void(unsigned s, unsigned f) {
        switch (s) {
        case 0: stored._next = uint64_t(stored._next) + 1; return;
        case 1:
            if (f & 1) { stored._next = uint64_t(stored._next) + 2; return; }
            stored._next = uint64_t(stored._next) + 4;
            break;
        default: stored._next = uint64_t(stored._next) + 8; break;
        }
        stored._next = uint64_t(stored._next) + 16;
    }
    unsigned fixed(unsigned f, unsigned v) {
        unsigned x = v;
        switch (2) {
        case 0: return 999;
        case 2: if (f & 1) return x + 7; x += 9; break;
        default: x += 10000;
        }
        return x + 11;
    }
    unsigned selector_changes(unsigned s, unsigned f, unsigned v) {
        unsigned x = v;
        switch (s) {
        case 0: s = 3; x += 1; [[fallthrough]];
        case 1: x += 2; break;
        case 3: return x + 4;
        default: x += 8; break;
        }
        // The selector must not be re-read after s changed inside the switch.
        return x + s + f;
    }
    unsigned sequential(unsigned s, unsigned f, unsigned v) {
        unsigned x = v;
        if (f & 4) return x + 1;
        switch (s) {
        case 0: if (f & 1) return x + 2; x += 4; break;
        case 1: x += 8; break;
        }
        x += 16;
        switch (f) {
        case 0: return x + 32;
        default: if (s & 1) break; return x + 64;
        }
        return x + 128;
    }
    unsigned loop_in_case(unsigned s, unsigned f, unsigned v) {
        unsigned i, j;
        unsigned x = v;
        switch (s) {
        case 0:
            for (i = 0; i < 3; ++i) {
                for (j = 0; j < 3; ++j) {
                    if (f == i + j) return x + 1;
                    if (f == j + 4) break;
                    x += 2;
                }
                if (f == i + 5) break;
                x += 4;
            }
            x += 8;
            break;
        default: x += 16; break;
        }
        return x + 32;
    }
    unsigned empty_default(unsigned s, unsigned f, unsigned v) {
        unsigned x = v;
        switch (s) {} // No matching label: continue unchanged.
        switch (f) {
        default: if (s & 1) return x + 1; x += 2; break;
        }
        return x + 4;
    }
    unsigned visible_counter(unsigned s, unsigned f, unsigned v) {
        unsigned i;
        unsigned x = v;
        for (i = 0; i < 5; i = i + 1) {
            if (i == f) break;
            switch (s) {
            case 0: if (i == 2) break; x += 1; break;
            case 1: if (i == 2) continue; x += 2; break;
            default: if (i == 3) return x + i; x += 4; break;
            }
            x += 8;
        }
        return x + 100 * i;
    }
    unsigned nested_mixed_returns(unsigned s, unsigned f, unsigned v) {
        unsigned x = v;
        switch (s) {
        case 0:
            switch (f & 3) {
            case 0: { if (f & 4) break; return x + 1; }
            default: return x + 2;
            }
            // Not unreachable: the inner conditional break reaches here.
            x += 8;
            break;
        default: x += 16; break;
        }
        return x + 32;
    }
    unsigned result() {
        unsigned s = uint64_t(selector_in());
        unsigned f = uint64_t(flags_in());
        unsigned v = uint64_t(seed_in());
        switch (uint64_t(mode_in())) {
        case 0: return all_return(s, f);
        case 1: return mixed(s, f, v);
        case 2: return no_default(s, f, v);
        case 3: return nested(s, f, v);
        case 4: return loops(s, f, v);
        case 5: return mixed(f, s, v);
        case 6: return fixed(f, v);
        case 8: return selector_changes(s, f, v);
        case 9: return sequential(s, f, v);
        case 10: return loop_in_case(s, f, v);
        case 11: return empty_default(s, f, v);
        case 12: return visible_counter(s, f, v);
        case 13: return nested_mixed_returns(s, f, v);
        default: break;
        }
        return v + 12345;
    }
    void _work(bool reset) {
        stored._next = result();
        early_void(uint64_t(selector_in()), uint64_t(flags_in()));
        // A callee's return must not exit its caller or skip another call.
        stored._next = uint64_t(stored._next) + 32;
        early_void(uint64_t(flags_in()), uint64_t(selector_in()));
        if (reset) stored._next = 0;
    }
    void _strobe() { stored.strobe(); }
};

SwitchControl cpphdl_top;
