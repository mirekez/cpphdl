#pragma once
#include <basic/cascoded_level_shifter.hpp>

namespace analog::one_g {

// The cascode supplies fast translation. These two explicit capacitors limit
// output kick from its gate/drain coupling at the clock-interface boundary.
class clock_level_shifter : public cascoded_level_shifter {
public:
    capacitance_value correction_storage{200e-15};

    void _assign() override {
        cascoded_level_shifter::_assign();
        Y = capacitor(Vss, correction_storage);
        Y_N = capacitor(Vss, correction_storage);
    }
};
} // namespace analog::one_g
