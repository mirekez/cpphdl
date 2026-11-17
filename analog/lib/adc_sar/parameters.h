#pragma once
#include <array>
#include <cmath>
#include <stdexcept>

namespace analog::adc_sar {

// SI units throughout. These are engineering choices from the existing SG13G2
// receiver, not a substitute for the foundry PSP model card. W is TOTAL width.
struct mos_parameters {
    double width, length, beta, threshold;
    unsigned fingers = 1;
    bool core = false;
    // Optional extracted diffusion geometry, consumed by the process exporter.
    // Negative means let the PDK wrapper calculate it from W/L/fingers.
    double as = -1, ad = -1, ps = -1, pd = -1;
};
struct pair_parameters { mos_parameters n, p; };
struct device_parameters {
    pair_parameters sample{{15e-6,.45e-6,.024,.7,6}, {30e-6,.45e-6,.012,.7,4}};
    pair_parameters dummy{{8e-6,.45e-6,.0064,.7,4}, {16.8e-6,.45e-6,.00336,.7,2}};
    pair_parameters complement{{40e-6,.45e-6,.016,.7,4}, {80e-6,.45e-6,.016,.7,8}};
    pair_parameters dac{{200e-6,.45e-6,.08,.7,20}, {400e-6,.45e-6,.08,.7,40}};
    mos_parameters preamp{10e-6,.45e-6,.003,.7,2};
    mos_parameters comparator_input{50e-6,.45e-6,.008,.7,5};
    pair_parameters feedback{{20e-6,.45e-6,.004,.7,2}, {40e-6,.45e-6,.002,.7,4}};
    mos_parameters precharge{10e-6,.45e-6,.002,.7};
    mos_parameters tail{50e-6,.45e-6,.008,.7,5};
    pair_parameters buffer_input{{2e-6,.45e-6,.001,.7}, {4e-6,.45e-6,.001,.7}};
    pair_parameters buffer_output{{10e-6,.45e-6,.004,.7}, {20e-6,.45e-6,.004,.7,2}};
    pair_parameters nand{{30e-6,.45e-6,.01,.7,3}, {60e-6,.45e-6,.01,.7,6}};
    pair_parameters clear{{120e-6,.45e-6,.05,.7,12}, {240e-6,.45e-6,.05,.7,24}};
    mos_parameters interface_input{5e-6,.45e-6,.004,.7};
    mos_parameters interface_pullup{2e-6,.13e-6,.0005,.35,1,true};
    pair_parameters interface_output{{10e-6,.13e-6,.01,.35,1,true}, {20e-6,.13e-6,.005,.35,2,true}};
};
struct passive_parameters {
    double hold = 1e-12;                 // Stored input charge.
    double clock = 20e-15;               // Complement driver load.
    double off_coupling = 1e-15;         // Input feedthrough while holding.
    double gate_coupling = 0;            // Explicit additional switch-clock coupling.
    double leakage = 0;                 // S to ground; zero disables a fictitious return.
    double ladder = 1000;               // R in the R/2R DAC.
    double dac_cap = 3e-15;             // Per driver and ladder tap storage.
    std::array<double,16> r_mismatch{};  // Fractional R error per bit, deterministic draw.
    std::array<double,16> r2_mismatch{}; // Independent 2R error per bit.
    double termination_mismatch = 0;    // Last 2R termination error.
    double preamp_load = 1000, degeneration = 100, tail_return = 300;
    double preamp_output = .3e-12, preamp_tail = .04e-12;
    double preamp_gs = .02e-12, preamp_gd = .003e-12;
    double comparator_internal = .03e-12, comparator_output = .05e-12;
    double comparator_gs = .02e-12, comparator_gd = .003e-12;
    double latch_input = 2e-15, latch_miller = 1e-15;
    double buffer_internal = 5e-15, buffer_input = 8e-15, buffer_output = 10e-15;
    double latch_storage = 50e-15, latch_gate = 3e-15, latch_stack = 10e-15;
    double interface_internal = 80e-15, interface_output = 10e-15;
    double interface_input = 5e-15, interface_feedback = 2e-15;
    // SG13G2 cap_cmim temperature coefficients. Resistive TC defaults to zero:
    // the old design binds ideal resistors, not a selected foundry resistor PCell.
    double cap_tc1 = 3.6e-6, cap_tc2 = 2e-9, resistor_tc1 = 0, resistor_tc2 = 0;
    double cap_scale = 1, resistor_scale = 1;
    double mim_density = 1.5e-3, mim_edge = 40e-12, mim_access = .055;
    double mim_min_cap = 3e-15; // Smaller target values use equal MIM cells in series.
    double comparator_left_trim = 0; // Optional placed MIM trim on the left regenerative node; default is symmetric.
};
struct corner_parameters {
    double temperature = 27;
    double n_beta_scale = 1, p_beta_scale = 1, n_threshold_shift = 0, p_threshold_shift = 0;
    // Explicit generic sensitivity knobs, NOT invented PSP TT/SS/FF corners.
    double mobility_exponent = 0, threshold_tc = 0, leakage_scale = 1;
};
struct layout_parameters {
    double input_r = 0, held_r = 0, dac_r = 0, track_r = 0, evaluate_r = 0;
    double clear_r = 0, bit_r = 0, decision_r = 0, io_r = 0, core_r = 0, ground_r = 0;
    double input_c = 0, held_c = 0, dac_c = 0, track_c = 0, evaluate_c = 0;
    double clear_c = 0, bit_c = 0, decision_c = 0, io_c = 0, core_c = 0;
    double track_held_c = 0, evaluate_held_c = 0, dac_held_c = 0, aggressor_held_c = 0;
    // Set true only when supplying an external distributed RC network. The
    // endpoints remain DISTINCT and every route must be supplied by that network.
    bool external_routes = false;
};
struct requirements {
    double io_min = 3, io_max = 3.35, core_min = 1.08, core_max = 1.35;
    double input_min = .85, input_max = 2.05;
    double acquisition = .020, hold_variation = .020;
    unsigned code_error = 2;
    double clock = 4e-9, acquisition_time = 8e-9, decision_setup = 25e-12;
    // NaN explicitly means no product budget; a report must never turn it PASS.
    double absolute_hold = NAN, average_power = NAN, peak_current = NAN;
    double dnl = NAN, inl = NAN, sndr = NAN, enob = NAN, sfdr = NAN;
    double jitter = NAN, minimum_yield = NAN;
    double input_noise_rms = NAN;       // Input-referred V RMS; unspecified is never a product PASS.
};
inline constexpr device_parameters ihp130_profile2{};
inline constexpr passive_parameters ihp130_passives{};
inline constexpr corner_parameters nominal_corner{};
inline constexpr layout_parameters schematic_layout{};

} // namespace analog::adc_sar
