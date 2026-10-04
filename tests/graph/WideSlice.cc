#include <cpphdl.h>
using namespace cpphdl;

#ifndef WIDE_LINE
#define WIDE_LINE 128
#endif
#ifndef WIDE_SELECT
#define WIDE_SELECT 32
#endif
#ifndef WIDE_FIXED
#define WIDE_FIXED 0
#endif

template<unsigned LineWidth = 128, unsigned SelectWidth = 32, bool Fixed = false>
class WideSlice : public Module {
public:
    _PORT(logic<LineWidth>) line_in;
    _PORT(logic<16>) first_in;
    _PORT(logic<SelectWidth>) data_out;
    _PORT(logic<SelectWidth>) const_data_out;
    _PORT(logic<LineWidth>) padded_out;
    logic<SelectWidth> data_comb, const_data_comb;
    logic<LineWidth> padded_comb;

    logic<SelectWidth>& data_comb_func() {
        unsigned first = Fixed ? 32 : unsigned(first_in());
        data_comb = logic<SelectWidth>(line_in().bits(first+SelectWidth-1, first));
        return data_comb;
    }
    logic<SelectWidth>& const_data_comb_func() {
        const logic<LineWidth> line = line_in();
        unsigned first = Fixed ? 32 : unsigned(first_in());
        const_data_comb = logic<SelectWidth>(line.bits(first+SelectWidth-1, first));
        return const_data_comb;
    }
    logic<LineWidth>& padded_comb_func() {
        unsigned first = Fixed ? 32 : unsigned(first_in());
        // Observe every upper bit, not only the narrowed destination. The read
        // must clear bits above high-low even when the source has ones there.
        padded_comb = line_in().bits(first+SelectWidth-1, first);
        return padded_comb;
    }
    void _assign() {
        data_out = _ASSIGN_COMB(data_comb_func());
        const_data_out = _ASSIGN_COMB(const_data_comb_func());
        padded_out = _ASSIGN_COMB(padded_comb_func());
    }
};

template class WideSlice<WIDE_LINE, WIDE_SELECT, WIDE_FIXED>;
extern WideSlice<WIDE_LINE, WIDE_SELECT, WIDE_FIXED> cpphdl_top;
