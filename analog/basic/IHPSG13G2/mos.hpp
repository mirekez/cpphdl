#pragma once
#include <osdi.hpp>

namespace analog::ihp {

// The process object selects the PDK's NMOS/PMOS card and corner. This class
// contributes its actual compact-model equations; it does not call ngspice.
class mos : public osdi::device {
public:
    _PORT(electrical) D;
    _PORT(electrical) G;
    _PORT(electrical) S;
    _PORT(electrical) B;
protected:
    std::vector<electrical> terminals() const override { return {D, G, S, B}; }
};

class varactor : public osdi::device {
public:
    _PORT(electrical) G;
    _PORT(electrical) BI;
    _PORT(electrical) B;
protected:
    std::vector<electrical> terminals() const override { return {G, BI, B}; }
};

} // namespace analog::ihp

namespace analog::behavior {
template <> struct traits<ihp::mos> : traits<osdi::device> {
    static constexpr std::string_view name = "IHP PSP MOS";
};
template <> struct traits<ihp::varactor> : traits<osdi::device> {
    static constexpr std::string_view name = "IHP MOSVAR core";
};
}
