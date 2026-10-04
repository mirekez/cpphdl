#include "SwitchCompleteWrite.cc"

// Unlike SwitchControl, these destinations have no pre-switch assignment.
class SwitchCompleteCases : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<2>) selector_in;
    _PORT(cpphdl::logic<1>) flag_in;
    _PORT(cpphdl::logic<8>) seed_in;
    _PORT(cpphdl::logic<64>) result_out;
    cpphdl::logic<64> result_comb;
    SwitchProbe probe;

    void _assign() {
        probe.increment_in = _ASSIGN(cpphdl::logic<1>(uint64_t(selector_in()) >> 1));
        probe.decrement_in = _ASSIGN(cpphdl::logic<1>(uint64_t(selector_in())));
        probe.current_in = _ASSIGN(cpphdl::logic<2>(seed_in()));
        probe._assign();
        result_out = _ASSIGN_COMB(result_comb_func());
    }
    cpphdl::logic<64>& result_comb_func() { result_comb = result(); return result_comb; }

    uint64_t result() {
        unsigned first, middle, nested, ordered, returned, retained;
        cpphdl::logic<8> bits;
        unsigned s = selector_in();
        unsigned seed = seed_in();
        bool f = flag_in();
        unsigned cursor = s;
        // Default first; stacked labels and conditional break/fallthrough.
        switch (s) {
        default: first = seed + 3; break;
        case 0:
        case 1:
            first = seed + 1;
            if (f) break;
            [[fallthrough]];
        case 2: first = seed + 2; break;
        }
        // Default in the middle, falling through a later case.
        switch (s) {
        case 0: middle = seed; break;
        default: middle = seed + 4; [[fallthrough]];
        case 1: middle = seed + 5; break;
        case 2: middle = seed + 6; break;
        }
        switch (s) {
        case 0:
            if (f) nested = seed + 1;
            else nested = seed + 2;
            break;
        case 1:
            switch (unsigned(f)) {
            case 0: nested = seed + 3; break;
            default: nested = seed + 4; break;
            }
            break;
        default: nested = seed + 5; break;
        }
        // Evaluate selector once; preserve read/write order on fallthrough.
        switch (cursor) {
        case 0:
            ordered = seed + cursor;
            cursor = 10;
            [[fallthrough]];
        case 1:
            ordered = seed + cursor;
            break;
        default: ordered = seed + cursor; break;
        }
        ordered = uint64_t(ordered) + 1;
        returned = fill_returned(s, f, seed);
        // Completeness is per bit, including assignments after the switch.
        switch (s) {
        case 0: bits.bits(3, 0) = seed; break;
        case 1: bits.bits(3, 0) = seed + 1; break;
        default: bits.bits(3, 0) = seed + 2; break;
        }
        bits.bits(7, 4) = seed >> 4;
        retained = seed;
        switch (s) {
        case 0: retained = seed + 1; break;
        case 1: retained = seed + 2; break;
        }
        return uint64_t(first & 255) | (uint64_t(middle & 255) << 8) |
            (uint64_t(nested & 255) << 16) | (uint64_t(ordered & 255) << 24) |
            (uint64_t(returned & 255) << 32) | (uint64_t(bits) << 40) |
            (uint64_t(retained & 255) << 48) | (uint64_t(probe.result_out()) << 56);
    }
    unsigned fill_returned(unsigned s, bool f, unsigned seed) {
        unsigned returned;
        switch (s) {
        case 0: returned = seed + 1; return returned;
        default: returned = seed + 2; break;
        case 1:
            returned = seed + 4;
            if (f) return seed + 3;
            break;
        }
        return returned + 8;
    }
};
extern SwitchCompleteCases complete_top;
