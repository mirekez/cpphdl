#pragma once
#include "adc_feedback.hpp"
#include <basic/level_down.hpp>

namespace analog::one_g {

// The retained comparator decisions drive actual high-to-core voltage
// interfaces. Core logic must read CORE_DECISION, never the 3.3 V latch node.
template <std::size_t Count = 16, class Frontend = HighRateSARADCFeedbackFrontend>
class SARADCCoreArray : public SARADCArray<Count, Frontend> {
public:
    _PORT(electrical) Vlogic;
    std::array<port<electrical>, Count> CORE_DECISION;
    std::array<level_down, Count> core_interfaces;

    void _assign() override {
        SARADCArray<Count, Frontend>::_assign();
        for (std::size_t i = 0; i < Count; ++i) {
            core_interfaces[i].A = this->DECISION[i];
            core_interfaces[i].Y = CORE_DECISION[i];
            core_interfaces[i].Vdd = Vlogic;
            core_interfaces[i].Vss = this->Vss;
            core_interfaces[i].elaborate();
        }
    }
};
} // namespace analog::one_g
