#pragma once
#include "primitives.h"
#include "noise.h"

namespace analog::adc_sar {

// Reusable electrical SAR slice: the external sequencer supplies TRACK,
// EVALUATE, CLEAR and complemented trial bits; it reads CORE_DECISION.
// During tracking, drive the first (midscale) SAR trial and keep EVALUATE low.
// This settles DAC/preamp common-mode while the input can recharge the hold
// capacitor; retaining the previous result here causes history-dependent codes.
// Every numeric process/layout choice is a C++20 structural template argument.
// Defaults reproduce the optimized SG13G2 profile-2 SCHEMATIC slice, excluding
// the PHY's input amplifier, PGA, reference generator and digital normalizer.
template<unsigned Bits = 8,
         device_parameters Devices = ihp130_profile2,
         passive_parameters Passives = ihp130_passives,
         corner_parameters Corner = nominal_corner,
         layout_parameters Layout = schematic_layout,
         requirements Limits = requirements{},
         noise_parameters Noise = noise_parameters{}>
class adc_sar_slice : public primitive<Devices,Passives,Corner> {
    using B = primitive<Devices,Passives,Corner>;
    static_assert(Bits >= 2 && Bits <= 16, "SAR resolution must be 2..16 bits");
public:
    using B::Vdd; using B::Vss; using B::cap;
    using typename B::cap_device; using typename B::res_device;
    static constexpr unsigned bits = Bits;
    static constexpr auto devices = Devices;
    static constexpr auto passives = Passives;
    static constexpr auto corner = Corner;
    static constexpr auto layout = Layout;
    static constexpr auto limits = Limits;
    static constexpr auto noise = Noise;

    // Optional sampled input-referred noise for a simulation bench. Consuming
    // these samples at the ADC input exercises the electrical conversion path;
    // no ideal output-code perturbation or random draw inside Newton is used.
    // The physical topology/export stays unchanged. Effective sampling C can be
    // overridden after extraction; the default omits bias-dependent MOS C.
    static noise_record sample_noise(std::size_t count,double period_s,std::uint64_t seed,
                                    noise_parameters profile=Noise) {
        if (!std::isfinite(Passives.hold) || Passives.hold<=0 || !std::isfinite(Passives.cap_scale)
            || Passives.cap_scale<=0 || !std::isfinite(Layout.held_c) || Layout.held_c<0)
            throw std::invalid_argument("invalid capacitance in slice noise binding");
        if constexpr (Layout.external_routes)
            if (profile.include_ktc && profile.capacitance_F==0)
                throw std::invalid_argument("extracted routes require an effective sampling C for noise");
        const double delta=Corner.temperature-27;
        const double c=Passives.hold*Passives.cap_scale*(1+Passives.cap_tc1*delta+Passives.cap_tc2*delta*delta)
            +Layout.held_c;
        return make_noise_record(profile,Corner.temperature+273.15,c,count,period_s,seed);
    }
    _PORT(electrical) IN;             // Single-ended analog input, relative to Vss.
    _PORT(electrical) TRACK;          // High closes the transmission gate.
    _PORT(electrical) EVALUATE;       // High evaluates; low precharges the comparator.
    std::array<port<electrical>,Bits> BIT_N; // Complemented trial word, LSB first.
    _PORT(electrical) CORE_DECISION;  // Retained comparison at the core rail voltage.

    sample_hold<Devices,Passives,Corner> sampler;           // Captures the input on a physical capacitor.
    r2r_dac<Bits,Devices,Passives,Corner> ladder;            // Converts the current SAR trial to voltage.
    preamplifier<Devices,Passives,Corner> amplifier;        // Isolates the held input from latch kickback.
    comparator<Devices,Passives,Corner> comparison;         // Regenerates the differential decision.

    // Secondary and auxiliary elements follow the main signal path.
    decision_latch<Devices,Passives,Corner> decision;       // Retains the bit during comparator precharge.
    core_interface<Devices,Passives,Corner> interface;     // Safely translates the decision to the core domain.
    _PORT(electrical) Vlogic;                              // Separate low-voltage rail for the interface.
    _PORT(electrical) CLEAR;                               // Forces a known retained decision at startup.
    _PORT(electrical) AGGRESSOR;                           // Optional externally driven crosstalk source.
    electrical held_sink, dac_sink, io, core, ground;      // Distinct routed loads and internal supply terminals.
    using route_map = std::map<std::string,electrical>;
    route_map endpoints;                                  // Stable names for SPEF or other extraction adapters.
    std::function<void(const route_map&)> annotation;      // Stamps an external RC network once, at elaboration.
    std::size_t first_element=0, last_element=0;           // Exact device range for process/netlist export.
    std::string naming_prefix;                             // Optional parent instance path for multi-slice names.

    void _assign() override {
        first_element=simulation::current().elements.size();
        naming_prefix=this->__inst_name.empty() ? "" : this->__inst_name+"/";
        // Main circuit connections and functional primitives come first.
        sampler.hierarchy=naming_prefix+"sampler";
        sampler.Vdd=io; sampler.Vss=ground;
        sampler.elaborate();
        ladder.hierarchy=naming_prefix+"ladder";
        ladder.Vdd=io; ladder.Vss=ground;
        ladder.elaborate();
        amplifier.hierarchy=naming_prefix+"amplifier";
        amplifier.PLUS=held_sink; amplifier.MINUS=dac_sink;
        amplifier.Vdd=io; amplifier.Vss=ground; amplifier.elaborate();
        comparison.hierarchy=naming_prefix+"comparison";
        comparison.PLUS=amplifier.OUTP; comparison.MINUS=amplifier.OUTN;
        comparison.Vdd=io; comparison.Vss=ground; comparison.elaborate();

        // Auxiliary state retention and voltage-domain interface.
        decision.hierarchy=naming_prefix+"decision";
        decision.SET_N=comparison.OUTN; decision.RESET_N=comparison.OUTP;
        decision.Vdd=io; decision.Vss=ground; decision.elaborate();
        interface.hierarchy=naming_prefix+"interface";
        interface.Vdd=core; interface.Vss=ground; interface.elaborate();
        CORE_DECISION=interface.Y;

        // Routing is declared LAST, after all endpoint identities exist. It
        // contributes real R/C to both native C++ and exported circuit equations.
        route("input",IN,sampler.IN,Layout.input_r,Layout.input_c);
        route("held",sampler.OUT,held_sink,Layout.held_r,Layout.held_c);
        route("dac",ladder.OUT,dac_sink,Layout.dac_r,Layout.dac_c);
        route("track",TRACK,sampler.CLOCK,Layout.track_r,Layout.track_c);
        route("evaluate",EVALUATE,comparison.CLOCK,Layout.evaluate_r,Layout.evaluate_c);
        route("clear",CLEAR,decision.CLEAR,Layout.clear_r,Layout.clear_c);
        route("decision",decision.Q,interface.A,Layout.decision_r,Layout.decision_c);
        for (unsigned i=0;i<Bits;++i)
            route("bit"+std::to_string(i),BIT_N[i],ladder.BIT_N[i],Layout.bit_r,Layout.bit_c);
        route("io",Vdd,io,Layout.io_r,Layout.io_c);
        route("core",Vlogic,core,Layout.core_r,Layout.core_c);
        route("ground",Vss,ground,Layout.ground_r,0);
        cap_device C_track_held{*this,naming_prefix+"coupling/C_track_held",sampler.CLOCK,held_sink,Layout.track_held_c};
        cap_device C_evaluate_held{*this,naming_prefix+"coupling/C_evaluate_held",comparison.CLOCK,held_sink,Layout.evaluate_held_c};
        cap_device C_dac_held{*this,naming_prefix+"coupling/C_dac_held",dac_sink,held_sink,Layout.dac_held_c};
        cap_device C_aggressor_held{*this,naming_prefix+"coupling/C_aggressor_held",AGGRESSOR,held_sink,Layout.aggressor_held_c};
        if constexpr (Layout.external_routes) {
            if (!annotation) throw diagnostic("E_VALUE", "external routes require an extraction adapter");
            if (Layout.track_held_c || Layout.evaluate_held_c || Layout.dac_held_c || Layout.aggressor_held_c)
                throw diagnostic("E_VALUE", "external extraction cannot also contain lumped coupling");
            annotation(endpoints);
        } else if (annotation) throw diagnostic("E_VALUE", "annotation requires external_routes=true");
        last_element=simulation::current().elements.size();
    }

    std::vector<mos_binding> physical_bindings() const {
        std::vector<mos_binding> result;
        for (const auto* records : {&sampler.bindings,&ladder.bindings,&amplifier.bindings,
                                  &comparison.bindings,&decision.bindings,&interface.bindings})
            result.insert(result.end(),records->begin(),records->end());
        return result;
    }

    route_map all_nodes() const {
        route_map nodes;
        const auto& sim=simulation::current();
        for (const auto* block : {static_cast<const B*>(&sampler),static_cast<const B*>(&ladder),
                static_cast<const B*>(&amplifier),static_cast<const B*>(&comparison),
                static_cast<const B*>(&decision),static_cast<const B*>(&interface),static_cast<const B*>(this)})
            for (const auto& [id,wire] : block->terminals)
                nodes.emplace("node"+std::to_string(sim.root(id)),wire);
        return nodes;
    }

private:
    void route(const std::string& name, const electrical& driver, const electrical& sink, double r, double c) {
        if (!std::isfinite(r) || r < 0 || !std::isfinite(c) || c < 0)
            throw diagnostic("E_VALUE", "negative/nonfinite route R/C");
        endpoints.emplace(name+".driver",driver); endpoints.emplace(name+".sink",sink);
        if constexpr (Layout.external_routes) {
            if (r || c) throw diagnostic("E_VALUE", "external routes cannot also contain lumped R/C");
        } else {
            if (r) {
                res_device R_series{*this,naming_prefix+"routing/"+name+"/R_series",driver,sink,r,false};
            }
            else { electrical n=sink; n=driver; }
            cap_device C_sink{*this,naming_prefix+"routing/"+name+"/C_sink",sink,ground,c};
        }
    }
};

} // namespace analog::adc_sar
