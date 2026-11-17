#pragma once
#include "adc_sar_slice.h"
#include <iomanip>
#include <set>

namespace analog::adc_sar {

// Caller supplies model names from its process adapter. SG13G2 names are
// defaults, but no IHP dependency is included in the generic electrical model.
struct process_models {
    std::string hv_n="sg13_hv_nmos", hv_p="sg13_hv_pmos";
    std::string lv_n="sg13_lv_nmos", lv_p="sg13_lv_pmos";
    std::string mim="cap_cmim";
};

// Export an ISOLATED slice before wiring it into a bench. Explicit bodies,
// fingers and extracted junction overrides reach the process wrapper unchanged.
// R/C remain the explicitly specified electrical values; this is a MOS process
// binding, not foundry passive/layout signoff. No beta-to-W inference is used.
template<class Slice>
void write_process_subcircuit(std::ostream& out, const Slice& slice, process_models models={}) {
    auto& sim=simulation::current();
    if (&slice.IN.owner()!=&sim || slice.last_element<=slice.first_element)
        throw diagnostic("E_VALUE","process export requires an elaborated slice in the current simulation");
    for (const auto& name : {models.hv_n,models.hv_p,models.lv_n,models.lv_p,models.mim})
        if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
            throw diagnostic("E_VALUE","invalid process model name");
    std::map<std::size_t,std::string> terminals;
    std::vector<std::pair<std::string,electrical>> ports{
        {"IN",slice.IN},{"TRACK",slice.TRACK},{"EVALUATE",slice.EVALUATE},{"CLEAR",slice.CLEAR},
        {"DECISION",slice.CORE_DECISION},{"VDD",slice.Vdd},{"VCORE",slice.Vlogic},{"VSS",slice.Vss},{"AGGRESSOR",slice.AGGRESSOR}};
    for (unsigned i=0;i<Slice::bits;++i) ports.emplace_back("BIT_N"+std::to_string(i),slice.BIT_N[i]);
    for (const auto& [name,pin] : ports)
        if (!terminals.emplace(sim.root(pin.id()),name).second)
            throw diagnostic("E_VALUE","process export ports were already shorted by a bench");
    auto node=[&](std::size_t id) {
        id=sim.root(id); auto it=terminals.find(id);
        return it==terminals.end() ? "n"+std::to_string(id) : it->second;
    };
    std::map<std::size_t,mos_binding> bindings;
    for (auto b : slice.physical_bindings())
        if (!bindings.emplace(b.element,b).second) throw diagnostic("E_VALUE","duplicate MOS binding");
    std::map<std::size_t,mim_binding> mim;
    std::set<std::size_t> accesses;
    for (auto b : slice.sampler.mim_bindings) { mim.emplace(b.capacitor,b); accesses.insert(b.resistor); }
    std::set<std::string> exported_names;
    out << std::setprecision(17) << "* Analog++ SAR: explicit MOS binding; R/C electrical values\n.subckt adc_sar_slice";
    for (const auto& [name,pin] : ports) out << ' ' << name;
    out << '\n';
    for (std::size_t i=slice.first_element;i<slice.last_element;++i) {
        const auto& e=sim.elements.at(i);
        if (accesses.contains(i)) continue;
        if (e.name.empty() || !exported_names.insert(e.name).second)
            throw diagnostic("E_VALUE","unnamed or duplicate SAR model element in process export");
        out << "* " << e.name << '\n';
        if (mim.contains(i)) {
            auto b=mim.at(i);
            out << 'X' << i << ' ' << node(b.a) << ' ' << node(b.b) << ' ' << models.mim
                << " w=" << b.width << " l=" << b.length << " ic=0\n";
        }
        else if (e.kind==law::resistor) out << 'R' << i << ' ' << node(e.a) << ' ' << node(e.b) << ' ' << e.r << '\n';
        else if (e.kind==law::capacitor) out << 'C' << i << ' ' << node(e.a) << ' ' << node(e.b) << ' ' << e.c << " IC=" << e.initial << '\n';
        else if (e.kind==law::nmos || e.kind==law::pmos) {
            if (!bindings.contains(i)) throw diagnostic("E_VALUE","unbound MOS in process export");
            auto b=bindings.at(i); auto p=b.parameters;
            bool is_p=e.kind==law::pmos;
            const auto model=p.core ? (is_p ? models.lv_p : models.lv_n) : (is_p ? models.hv_p : models.hv_n);
            if (model=="sg13_hv_nmos" || model=="sg13_hv_pmos" || model=="sg13_lv_nmos" || model=="sg13_lv_pmos") {
                const double min_width=p.core ? .15e-6 : .30e-6;
                const double min_length=p.core ? .13e-6 : (is_p ? .40e-6 : .45e-6);
                if (p.width/p.fingers<min_width-1e-18 || p.width/p.fingers>10e-6+1e-18
                    || p.length<min_length-1e-18 || p.length>10e-6+1e-18)
                    throw diagnostic("E_VALUE","MOS outside the pinned IHP per-finger model range");
            }
            out << 'X' << i << ' ' << node(e.a) << ' ' << node(e.gate) << ' ' << node(e.b) << ' ' << node(b.body)
                << ' ' << model
                << " w=" << p.width << " l=" << p.length << " ng=" << p.fingers;
            if (p.as>=0) out << " as=" << p.as;
            if (p.ad>=0) out << " ad=" << p.ad;
            if (p.ps>=0) out << " ps=" << p.ps;
            if (p.pd>=0) out << " pd=" << p.pd;
            out << '\n'; bindings.erase(i);
        } else throw diagnostic("E_VALUE","unsupported element in physical slice export");
    }
    if (!bindings.empty()) throw diagnostic("E_VALUE","MOS binding outside exported topology");
    out << ".ends adc_sar_slice\n";
}
} // namespace analog::adc_sar
