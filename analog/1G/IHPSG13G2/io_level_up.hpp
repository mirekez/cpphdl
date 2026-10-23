#pragma once
#include <analog.hpp>

namespace analog::one_g::ihp_sg13g2 {

// Eight-MOS topology of the open PDK's sg13g2_LevelUp cell. The square-law
// parameters are a fast functional model; separate PSP tests use its actual
// LV/HV devices and dimensions. The PDK also provides its physical GDS view.
class io_level_up : public IC {
public:
    _PORT(electrical) Vlogic;
    _PORT(electrical) A;
    _PORT(electrical) Y;
    electrical complement, latch, latch_n;
    bool inverting{false};
    mos_model logic_n{channel_strength(.004), voltage(.35)};
    mos_model logic_p{channel_strength(.002), voltage(.35)};
    mos_model high_n{channel_strength(.001), voltage(.5)};
    mos_model latch_p{channel_strength(.00003), voltage(.7)};
    mos_model output_p{channel_strength(.00039), voltage(.7)};
    capacitance_value internal_storage{5e-15}, output_storage{20e-15};

    void _assign() override {
        complement = pmos(A, Vlogic, logic_p);
        Vss = nmos(A, complement, logic_n);
        latch = pmos(latch_n, Vdd, latch_p);
        latch_n = pmos(latch, Vdd, latch_p);
        Vss = nmos(inverting ? complement : electrical(A), latch_n, high_n);
        Vss = nmos(inverting ? electrical(A) : complement, latch, high_n);
        Y = pmos(latch_n, Vdd, output_p);
        Vss = nmos(latch_n, Y, high_n);
        complement = capacitor(Vss, internal_storage);
        latch = capacitor(Vss, internal_storage);
        latch_n = capacitor(Vss, internal_storage);
        Y = capacitor(Vss, output_storage);
    }
};

class io_level_up_inverting : public io_level_up {
public:
    void _assign() override {
        inverting = true;
        io_level_up::_assign();
    }
};
} // namespace analog::one_g::ihp_sg13g2
