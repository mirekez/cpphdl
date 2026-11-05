#pragma once
#include "../../1G/IHPSG13G2/core_receive_lane.hpp"
#include "../../1G/IHPSG13G2/core_flp_frontend.hpp"
#include "../../1G/IHPSG13G2/cascoded_core_transmit_driver.hpp"
#include "../../1G/IHPSG13G2/lead_receive_common_reference.hpp"
#include "../../1G/IHPSG13G2/receive_reference.hpp"
#include "../../1G/IHPSG13G2/reference_clock_generator.hpp"
#include "../../1G/IHPSG13G2/processing_clock_delivery.hpp"

namespace analog::one_g::ihp_sg13g2 {

// Electrical partition of the two-port repeater. The pad's direct MDI node
// carries TX current; its separate protected sense node feeds RX and FLP.
// The digital top supplies real control bits; this class supplies no stimuli.
// Pads, slave TX clock recovery and extracted timing are not implemented here.
class repeater_analog : public IC {
public:
    static constexpr unsigned lanes = 8, slices_per_lane = 13;
    _PORT(electrical) Vlogic;
    _PORT(electrical) REF_CLOCK;
    _PORT(electrical) CLOCK_CLEAR;
    _PORT(electrical) ADC_CLOCK;
    _PORT(electrical) PROCESSING_RAW;
    _PORT(electrical) PROCESSING_CLOCK;
    _PORT(electrical) PROCESSING_CLEAR;
    _PORT(electrical) PROCESSING_ADMIT;
    std::array<port<electrical>, 8> PROCESSING_COARSE_N, PROCESSING_FINE_N;
    std::array<port<electrical>, lanes> P, N, CLEAR, EVALUATE_ENABLE;
    std::array<port<electrical>, lanes> SENSE_P, SENSE_N;
    std::array<port<electrical>, 2> PULSE_COMMAND, PULSE_ENABLE, PULSE_DETECTED;
    std::array<std::array<port<electrical>, 5>, lanes> GAIN_N;
    std::array<std::array<port<electrical>, 6>, lanes> TRIM_N;
    std::array<std::array<port<electrical>, 4>, lanes> MAIN_P, MAIN_N, HISTORY_P, HISTORY_N;
    std::array<std::array<port<electrical>, slices_per_lane>, lanes> TRACK, DECISION;
    std::array<std::array<std::array<port<electrical>, 8>, slices_per_lane>, lanes> TRIAL;

    std::array<core_receive_lane, lanes> receivers;
    std::array<cascoded_core_emphasized_pam5_driver, lanes> transmitters;
    std::array<lead_receive_common_reference, 2> common_reference;
    std::array<adc_range_reference, 2> range_reference;
    std::array<core_flp_frontend, 2> negotiation;
    reference_clock_generator adc_clock;
    processing_clock_delivery processing_clock;

    void _assign() override {
        adc_clock.REF = REF_CLOCK;
        adc_clock.CLEAR = CLOCK_CLEAR;
        adc_clock.CORE_OUT = ADC_CLOCK;
        adc_clock.Vdd = Vdd;
        adc_clock.Vss = Vss;
        adc_clock.Vlogic = Vlogic;
        adc_clock.elaborate();
        processing_clock.RAW = PROCESSING_RAW;
        processing_clock.OUT = PROCESSING_CLOCK;
        processing_clock.CLEAR = PROCESSING_CLEAR;
        processing_clock.ADMIT = PROCESSING_ADMIT;
        processing_clock.Vdd = Vlogic;
        processing_clock.Vss = Vss;
        for (unsigned bit = 0; bit < 8; ++bit) {
            processing_clock.COARSE_N[bit] = PROCESSING_COARSE_N[bit];
            processing_clock.FINE_N[bit] = PROCESSING_FINE_N[bit];
        }
        processing_clock.elaborate();
        for (unsigned port_index = 0; port_index < 2; ++port_index) {
            bind(common_reference[port_index], Vdd, Vss);
            bind(range_reference[port_index], Vdd, Vss);
            auto& flp = negotiation[port_index];
            flp.COMMAND = PULSE_COMMAND[port_index];
            flp.ENABLE = PULSE_ENABLE[port_index];
            flp.PULSE = PULSE_DETECTED[port_index];
            // Straight MDI: pulse transmit on pair A, detect on pair B.
            flp.TX_P = P[4 * port_index];
            flp.TX_N = N[4 * port_index];
            flp.RX_P = SENSE_P[4 * port_index + 1];
            flp.RX_N = SENSE_N[4 * port_index + 1];
            flp.Vlogic = Vlogic;
            bind(flp, Vdd, Vss);
        }
        for (unsigned lane = 0; lane < lanes; ++lane) {
            auto& rx = receivers[lane];
            auto& tx = transmitters[lane];
            rx.P = SENSE_P[lane];
            rx.N = SENSE_N[lane];
            tx.P = P[lane];
            tx.N = N[lane];
            rx.Vlogic = Vlogic;
            tx.Vlogic = Vlogic;
            rx.VCM = common_reference[lane / 4].OUT;
            rx.VREF = range_reference[lane / 4].OUT;
            rx.ADC_CLOCK = ADC_CLOCK;
            rx.EVALUATE_ENABLE = EVALUATE_ENABLE[lane];
            rx.CLEAR = CLEAR[lane];
            for (unsigned bit = 0; bit < 5; ++bit) rx.GAIN_N[bit] = GAIN_N[lane][bit];
            for (unsigned bit = 0; bit < 6; ++bit) tx.TRIM_N[bit] = TRIM_N[lane][bit];
            for (unsigned bit = 0; bit < 4; ++bit) {
                tx.MAIN_P[bit] = MAIN_P[lane][bit];
                tx.MAIN_N[bit] = MAIN_N[lane][bit];
                tx.HISTORY_P[bit] = HISTORY_P[lane][bit];
                tx.HISTORY_N[bit] = HISTORY_N[lane][bit];
            }
            for (unsigned slice = 0; slice < slices_per_lane; ++slice) {
                rx.TRACK[slice] = TRACK[lane][slice];
                rx.DECISION[slice] = DECISION[lane][slice];
                for (unsigned bit = 0; bit < 8; ++bit)
                    rx.TRIAL[slice][bit] = TRIAL[lane][slice][bit];
            }
            bind(rx, Vdd, Vss);
            bind(tx, Vdd, Vss);
        }
    }
};
} // namespace analog::one_g::ihp_sg13g2
