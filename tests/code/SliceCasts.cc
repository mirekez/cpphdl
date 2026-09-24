#include <cpphdl.h>
using namespace cpphdl;

struct SliceCastWord { union { uint32_t raw; }; };

class SliceCasts : public Module {
public:
    _PORT(logic<128>) data_in;
    _PORT(logic<3>) lane_in;
    _PORT(logic<4>) mode_in;
    _PORT(logic<16>) replacement_in;
    _PORT(SliceCastWord) result_out;
    _PORT(logic<128>) edited_out;
    SliceCastWord result_comb;
    logic<128> edited_comb;

    SliceCastWord read_int(int lane) {
        SliceCastWord result;
        result.raw = data_in().bits(lane*32+31, lane*32);
        return result;
    }
    SliceCastWord read_short(short lane) {
        SliceCastWord result;
        result.raw = data_in().bits(lane*16+15, lane*16);
        return result;
    }
    SliceCastWord read_unsigned(unsigned lane) {
        SliceCastWord result;
        result.raw = data_in().bits(lane*16+15, lane*16);
        return result;
    }
    SliceCastWord read_offset(short lane) {
        SliceCastWord result;
        // Negative intermediates are valid; only the final select is in range.
        result.raw = data_in().bits((lane+4)*16+15, (lane+4)*16);
        return result;
    }
    SliceCastWord read_wrap(uint32_t base) {
        SliceCastWord result;
        // base == UINT32_MAX must wrap to zero BEFORE selecting the data.
        result.raw = data_in().bits((base+1u)*16u+15u, (base+1u)*16u);
        return result;
    }
    SliceCastWord read_narrow(uint32_t base) {
        SliceCastWord result;
        result.raw = data_in().bits(uint8_t(base)*16+15, uint8_t(base)*16);
        return result;
    }
    SliceCastWord read_div(int8_t lane) {
        SliceCastWord result;
        int quotient = lane/2;
        result.raw = data_in().bits((quotient+4)*16+15, (quotient+4)*16);
        return result;
    }
    SliceCastWord read_shift(int8_t lane) {
        SliceCastWord result;
        int shifted = lane >> 1;
        result.raw = data_in().bits((shifted+4)*16+15, (shifted+4)*16);
        return result;
    }
    SliceCastWord read_explicit_wide(uint32_t base) {
        SliceCastWord result;
        // The explicit widening must retain the inner 32-bit wrap boundary.
        result.raw = data_in().bits(uint64_t(base+1u)*16+15, uint64_t(base+1u)*16);
        return result;
    }
    SliceCastWord& result_comb_func() {
        unsigned lane = lane_in();
        switch (mode_in()) {
        case 0: result_comb = read_int(lane & 3u); break;
        case 1: result_comb = read_short(lane); break;
        case 2: result_comb = read_unsigned(lane); break;
        case 3: result_comb = read_offset(int(lane)-4); break;
        case 4: result_comb = read_wrap(lane-1u); break;
        case 5: result_comb = read_narrow(lane+256u); break;
        case 6: result_comb = read_div(int(lane)*2-8); break;
        case 7: result_comb = read_shift(int(lane)*2-8); break;
        default: result_comb = read_explicit_wide(lane-1u); break;
        }
        return result_comb;
    }
    logic<128>& edited_comb_func() {
        int lane = lane_in();
        edited_comb = data_in();
        edited_comb.bits(lane*16+15, lane*16) = replacement_in();
        return edited_comb;
    }
    void _assign() {
        result_out = _ASSIGN_COMB(result_comb_func());
        edited_out = _ASSIGN_COMB(edited_comb_func());
    }
};
