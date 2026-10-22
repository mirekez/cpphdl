#pragma once
#include <basic/inverter.hpp>

namespace export_test {
using Inverter = analog::cmos<analog::channel_model>;
inline void configure(Inverter& component) {
    component.model.transistor_parameters.beta = analog::channel_strength(.002);
}
struct NotIC {};
} // namespace export_test
