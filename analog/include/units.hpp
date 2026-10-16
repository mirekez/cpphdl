#pragma once
#include <compare>

namespace analog {
template <class Tag> struct quantity {
    double value;
    explicit constexpr quantity(double si) : value(si) {}
    constexpr auto operator<=>(const quantity&) const = default;
};
struct voltage_tag {};
struct time_tag {};
struct resistance_tag {};
struct charge_tag {};
struct beta_tag {};
using voltage = quantity<voltage_tag>;
using duration = quantity<time_tag>;
using resistance = quantity<resistance_tag>;
using capacitance_value = quantity<charge_tag>;
using channel_strength = quantity<beta_tag>;

namespace literals {
#define ANALOG_UNIT(suffix, type, scale)                                                                               \
    constexpr type operator""##suffix(long double x) {                                                                 \
        return type(double(x) * scale);                                                                                \
    }                                                                                                                  \
    constexpr type operator""##suffix(unsigned long long x) {                                                          \
        return type(double(x) * scale);                                                                                \
    }
ANALOG_UNIT(_V, voltage, 1.0)
ANALOG_UNIT(_mV, voltage, 1e-3)
ANALOG_UNIT(_s, duration, 1.0)
ANALOG_UNIT(_ms, duration, 1e-3)
ANALOG_UNIT(_us, duration, 1e-6)
ANALOG_UNIT(_ns, duration, 1e-9)
ANALOG_UNIT(_Ohm, resistance, 1.0)
ANALOG_UNIT(_kOhm, resistance, 1e3)
ANALOG_UNIT(_F, capacitance_value, 1.0)
ANALOG_UNIT(_uF, capacitance_value, 1e-6)
ANALOG_UNIT(_nF, capacitance_value, 1e-9)
ANALOG_UNIT(_pF, capacitance_value, 1e-12)
ANALOG_UNIT(_mA_per_V2, channel_strength, 1e-3)
#undef ANALOG_UNIT
} // namespace literals
} // namespace analog
