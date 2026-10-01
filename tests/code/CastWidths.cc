#include <cpphdl.h>
using namespace cpphdl;

template<unsigned WIDTH = 9>
class CastWidths : public Module {
public:
    using lane_t = logic<WIDTH>;
    using unused_lane_t = logic<WIDTH + 1>;
    _PORT(logic<64>) value_in;
    _PORT(logic<64>) result_out;
    _PORT(logic<64>) signed_static_out;
    _PORT(logic<64>) signed_cstyle_out;
    _PORT(logic<64>) signed_functional_out;
    _PORT(logic<64>) strobe_out;
    _PORT(logic<64>) inverted_out;
    logic<64> result_comb;
    logic<64> signed_static_comb, signed_cstyle_comb, signed_functional_comb;
    logic<64> byte_mask_comb;
    logic<64> inverted_comb;
    logic<64>& result_comb_func() {
        uint64_t x;
        x = value_in();
        result_comb = static_cast<uint64_t>(static_cast<lane_t>(x))
            ^ (static_cast<uint64_t>((lane_t)(x >> 7)) << 1)
            ^ (static_cast<uint64_t>(lane_t(x >> 13)) << 2);
        return result_comb;
    }
    logic<64>& signed_static_comb_func() {
        signed_static_comb = static_cast<lane_t>(static_cast<int8_t>(value_in()));
        return signed_static_comb;
    }
    logic<64>& signed_cstyle_comb_func() {
        signed_cstyle_comb = (lane_t)(int8_t)(uint64_t)value_in();
        return signed_cstyle_comb;
    }
    logic<64>& signed_functional_comb_func() {
        signed_functional_comb = lane_t(int8_t(uint64_t(value_in())));
        return signed_functional_comb;
    }
    logic<64>& byte_mask_comb_func() {
        // Compound cast sizes need parentheses in SV. This is the same
        // all-byte-enables expression used by DMA writers.
        byte_mask_comb = ~logic<(WIDTH + 7) / 8>(0);
        return byte_mask_comb;
    }
    logic<64>& inverted_comb_func() {
        lane_t lane;
        lane = value_in();
        inverted_comb = ~lane;
        return inverted_comb;
    }
    void _assign() {
        result_out = _ASSIGN_COMB(result_comb_func());
        signed_static_out = _ASSIGN_COMB(signed_static_comb_func());
        signed_cstyle_out = _ASSIGN_COMB(signed_cstyle_comb_func());
        signed_functional_out = _ASSIGN_COMB(signed_functional_comb_func());
        strobe_out = _ASSIGN_COMB(byte_mask_comb_func());
        inverted_out = _ASSIGN_COMB(inverted_comb_func());
    }
    void _work(bool reset) {}
    void _strobe() {}
};

// Convert from an actual specialization, but retain WIDTH in emitted casts.
template class CastWidths<9>;
