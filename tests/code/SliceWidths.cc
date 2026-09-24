#include <cpphdl.h>
using namespace cpphdl;

struct SliceWidthWord { union { uint32_t raw; }; };

template<unsigned WIDTH = 32>
class SliceWidths : public Module {
public:
    _PORT(logic<512>) data_in;
    _PORT(logic<512>) data2_in;
    _PORT(logic<4>) lane_in;
    _PORT(SliceWidthWord) first_out;
    _PORT(SliceWidthWord) second_out;
    _PORT(logic<32>) next_out;
    _PORT(logic<16>) offset_out;
    _PORT(logic<32>) shifted_out;
    _PORT(logic<WIDTH>) parameter_out;
    _PORT(logic<512>) edited_out;
    SliceWidthWord first_comb, second_comb;
    logic<32> next_comb, shifted_comb;
    logic<16> offset_comb;
    logic<WIDTH> parameter_comb;
    logic<512> edited_comb;

    SliceWidthWord& first_comb_func() {
        u<4> word_lane = uint64_t(lane_in());
        SliceWidthWord result;
        result.raw = data_in().bits(word_lane*32+31, word_lane*32);
        first_comb = result;
        return first_comb;
    }
    SliceWidthWord& second_comb_func() {
        u<4> word_lane = uint64_t(lane_in());
        SliceWidthWord result;
        result.raw = data2_in().bits(word_lane*32+31, word_lane*32);
        second_comb = result;
        return second_comb;
    }
    logic<32>& next_comb_func() {
        u<4> word_lane = uint64_t(lane_in());
        next_comb = data_in().bits((word_lane+1)*32-1, word_lane*32);
        return next_comb;
    }
    logic<16>& offset_comb_func() {
        uint64_t base = uint64_t(lane_in())*32;
        offset_comb = data_in().bits(base+31, base+16);
        return offset_comb;
    }
    logic<32>& shifted_comb_func() {
        uint64_t word_lane = uint64_t(lane_in());
        shifted_comb = data2_in().bits(31+(word_lane<<5), word_lane<<5);
        return shifted_comb;
    }
    logic<WIDTH>& parameter_comb_func() {
        u<4> word_lane = uint64_t(lane_in());
        parameter_comb = data_in().bits(word_lane*WIDTH+WIDTH-1, word_lane*WIDTH);
        return parameter_comb;
    }
    logic<512>& edited_comb_func() {
        u<4> word_lane = uint64_t(lane_in());
        edited_comb = data_in();
        edited_comb.bits(word_lane*32+31, word_lane*32) = second_out().raw;
        return edited_comb;
    }
    void _assign() {
        first_out = _ASSIGN_COMB(first_comb_func());
        second_out = _ASSIGN_COMB(second_comb_func());
        next_out = _ASSIGN_COMB(next_comb_func());
        offset_out = _ASSIGN_COMB(offset_comb_func());
        shifted_out = _ASSIGN_COMB(shifted_comb_func());
        parameter_out = _ASSIGN_COMB(parameter_comb_func());
        edited_out = _ASSIGN_COMB(edited_comb_func());
    }
};

// Instantiate dependent port bindings while keeping WIDTH a module parameter.
template class SliceWidths<32>;
