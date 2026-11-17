#pragma once
#include "parameters.h"
#include <iomanip>
#include <ostream>
#include <sstream>

namespace analog::adc_sar {

// Export the product budgets bound to an adc_sar_slice template specialization.
// NaN is an explicit absent budget. Positive infinity is invalid, not a pass.
inline void write_qualification_limits(std::ostream& out, const requirements& r) {
    std::ostringstream buffer;
    buffer << std::setprecision(17);
    buffer << "{\n";
    const auto field = [&](const char* name, double value, bool last = false) {
        if (!std::isfinite(value) && !std::isnan(value))
            throw std::invalid_argument("non-finite ADC qualification limit");
        buffer << "  \"" << name << "\": ";
        if (std::isnan(value)) buffer << "null";
        else buffer << value;
        buffer << (last ? "\n" : ",\n");
    };
    field("dnl", r.dnl);
    field("inl", r.inl);
    field("average_power", r.average_power);
    field("peak_current", r.peak_current);
    field("sndr", r.sndr);
    field("enob", r.enob);
    field("sfdr", r.sfdr);
    field("absolute_hold", r.absolute_hold, true);
    buffer << "}\n";
    out << buffer.str();
}

template<class Slice>
inline void write_qualification_limits(std::ostream& out) {
    write_qualification_limits(out, Slice::limits);
}

} // namespace analog::adc_sar
