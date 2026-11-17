#pragma once
#include "parameters.h"
#include <analog.hpp>

namespace analog::adc_sar {

struct mos_binding {
    std::size_t element, body;
    mos_parameters parameters;
};
struct mim_binding {
    std::size_t capacitor, resistor, a, b;
    double width, length;
};

// Shared stamp helpers use the Analog++ equation solver. Device bindings retain
// physical geometry separately so export never guesses geometry from beta.
template<device_parameters D, passive_parameters P, corner_parameters O>
class primitive : public IC {
public:
    std::string hierarchy; // Relative instance path within a SAR slice.
    std::vector<mos_binding> bindings; // Explicit MOS/body correspondence for process export.
    std::vector<mim_binding> mim_bindings; // Replaces BOTH access R and C with one process MIM cell.
    std::map<std::size_t,electrical> terminals; // Retained wire handles for complete-node observation/export.
    void remember(std::initializer_list<electrical> nodes) {
        for (const auto& node : nodes) terminals.emplace(node.id(),node);
    }
    std::string qualified(std::string_view local) const {
        if (local.empty()) return {};
        return hierarchy.empty() ? std::string(local) : hierarchy + "/" + std::string(local);
    }
    void name_element(std::size_t id, std::string_view local) const {
        simulation::current().elements.at(id).name=qualified(local);
    }
    void mos(const electrical& drain, const electrical& gate, const electrical& source,
             const electrical& body, mos_parameters p, bool is_p = false,
             std::string_view name = {}) {
        positive(p.width, "MOS width"); positive(p.length, "MOS length");
        if (!p.fingers) throw diagnostic("E_VALUE", "zero MOS fingers");
        for (double v : {p.as,p.ad,p.ps,p.pd})
            if (!std::isfinite(v) || (v < 0 && v != -1))
                throw diagnostic("E_VALUE", "invalid extracted junction geometry");
        const bool extracted=p.as>=0 || p.ad>=0 || p.ps>=0 || p.pd>=0;
        if (extracted && !(p.as>1e-50 && p.ad>=0 && p.ps>=0 && p.pd>=0))
            throw diagnostic("E_VALUE", "extracted junctions require all four fields and positive source area");
        positive(O.temperature + 273.15, "absolute temperature");
        const double beta = p.beta * (is_p ? O.p_beta_scale : O.n_beta_scale)
            * std::pow((O.temperature + 273.15) / 300.15, -O.mobility_exponent);
        const double vt = p.threshold + (is_p ? O.p_threshold_shift : O.n_threshold_shift)
            + O.threshold_tc * (O.temperature - 27);
        positive(beta, "MOS beta"); positive(vt, "MOS threshold");
        simulation& sim = simulation::current();
        remember({drain,gate,source,body});
        sim.check(drain, gate); sim.check(source, body);
        element e{is_p ? law::pmos : law::nmos};
        e.a = drain.id(); e.b = source.id(); e.gate = gate.id(); e.beta = beta; e.threshold = vt;
        e.name=qualified(name);
        bindings.push_back({sim.elements.size(), body.id(), p});
        sim.add(e);
    }
    void inverter(const electrical& a, const electrical& y, pair_parameters p,
                  std::string_view n_name = {}, std::string_view p_name = {}) {
        mos(y,a,Vss,Vss,p.n,false,n_name); mos(y,a,Vdd,Vdd,p.p,true,p_name);
    }
    void cap(const electrical& a, const electrical& b, double c, bool mim = false,
             std::string_view name = {},
             std::function<std::string(unsigned)> segment_name = {}) {
        if (!std::isfinite(c) || c < 0) throw diagnostic("E_VALUE", "negative/nonfinite capacitance");
        if (!c) return;
        remember({a,b});
        double dt = O.temperature - 27;
        if (mim) {
            positive(P.mim_density,"MIM area density"); positive(P.mim_min_cap,"MIM minimum capacitance");
            positive(P.mim_access,"MIM access resistance");
            if (!std::isfinite(P.mim_edge) || P.mim_edge<0) throw diagnostic("E_VALUE","invalid MIM edge density");
            double count=std::ceil(P.mim_min_cap/c);
            if (!std::isfinite(count) || count>1000) throw diagnostic("E_VALUE","excessive series MIM count");
            unsigned series=unsigned(std::max(1.,count));
            const double nominal=c*series;
            // Stable positive root of density*w^2 + 4*edge*w = C.
            const double width=2*nominal/(4*P.mim_edge+std::sqrt(16*P.mim_edge*P.mim_edge+4*P.mim_density*nominal));
            const double value=nominal*P.cap_scale*(1+P.cap_tc1*dt+P.cap_tc2*dt*dt);
            positive(value,"scaled MIM capacitance");
            std::vector<electrical> nodes(series+1);
            nodes.front()=a; nodes.back()=b;
            for (unsigned i=0;i<series;++i) {
                electrical stored;
                auto& sim=simulation::current();
                std::size_t r=sim.elements.size(); stored=resistor(nodes[i],resistance(P.mim_access));
                std::size_t id=sim.elements.size(); stored=capacitor(nodes[i+1],capacitance_value(value));
                std::string capacitor_name=segment_name ? segment_name(i) : std::string(name);
                if (series>1 && !segment_name && !capacitor_name.empty())
                    capacitor_name+="_series"+std::to_string(i);
                name_element(id,capacitor_name);
                if (!capacitor_name.empty()) name_element(r,"R_access_"+capacitor_name);
                mim_bindings.push_back({id,r,nodes[i].id(),nodes[i+1].id(),width,width});
                remember({nodes[i],nodes[i+1],stored});
            }
        } else { electrical node=a; auto id=simulation::current().elements.size();
                 node=capacitor(b,capacitance_value(c)); name_element(id,name); }
    }
    void res(const electrical& a, const electrical& b, double r, bool process = true,
             std::string_view name = {}) {
        remember({a,b});
        const double dt = O.temperature - 27;
        r *= process ? P.resistor_scale * (1 + P.resistor_tc1*dt + P.resistor_tc2*dt*dt) : 1;
        positive(r, "resistance"); electrical node = a;
        auto id=simulation::current().elements.size(); node = resistor(b,resistance(r)); name_element(id,name);
    }
    // Named C++ objects stamp an element when constructed. Their identifiers
    // are visible in the model and the same names are retained by simulation.
    struct mos_device {
        mos_device(primitive& owner, std::string_view name, const electrical& drain,
                   const electrical& gate, const electrical& source, const electrical& body,
                   mos_parameters p, bool is_p=false) {
            owner.mos(drain,gate,source,body,p,is_p,name);
        }
    };
    struct cap_device {
        cap_device(primitive& owner, std::string_view name, const electrical& a,
                   const electrical& b, double c, bool mim=false,
                   std::function<std::string(unsigned)> segment_name={}) {
            owner.cap(a,b,c,mim,name,std::move(segment_name));
        }
    };
    struct res_device {
        res_device(primitive& owner, std::string_view name, const electrical& a,
                   const electrical& b, double r, bool process=true) {
            owner.res(a,b,r,process,name);
        }
    };
};

template<device_parameters D, passive_parameters P, corner_parameters O>
class sample_hold : public primitive<D,P,O> {
    using B = primitive<D,P,O>;
public:
    using B::Vdd; using B::Vss; using B::mos; using B::cap; using B::res; using B::inverter;
    using typename B::mos_device; using typename B::cap_device;
    using typename B::res_device;
    _PORT(electrical) IN; _PORT(electrical) CLOCK; _PORT(electrical) OUT;
    electrical clock_n; // Complement closes the PMOS simultaneously with the NMOS.
    void _assign() override {
        mos_device M0_track_n{*this,"M0_track_n",OUT,CLOCK,IN,Vss,D.sample.n};
        mos_device M1_track_p{*this,"M1_track_p",OUT,clock_n,IN,Vdd,D.sample.p,true};
        cap_device C0_capture{*this,"C0_capture",OUT,Vss,P.hold,true};
        // Auxiliary elements cancel switch charge and retain unavoidable coupling.
        mos_device M2_clock_inverter_n{*this,"M2_clock_inverter_n",clock_n,CLOCK,Vss,Vss,D.complement.n};
        mos_device M3_clock_inverter_p{*this,"M3_clock_inverter_p",clock_n,CLOCK,Vdd,Vdd,D.complement.p,true};
        mos_device M4_dummy_n{*this,"M4_dummy_n",OUT,clock_n,OUT,Vss,D.dummy.n};
        mos_device M5_dummy_p{*this,"M5_dummy_p",OUT,CLOCK,OUT,Vdd,D.dummy.p,true};
        cap_device C1_clock_load{*this,"C1_clock_load",clock_n,Vss,P.clock,true};
        cap_device C2_off_coupling{*this,"C2_off_coupling",IN,OUT,P.off_coupling,true,[](unsigned i) {
            return "C"+std::to_string(i+2)+"_off_coupling_series"+std::to_string(i);
        }};
        cap_device C_gate_clock_to_hold{*this,"C_gate_clock_to_hold",CLOCK,OUT,P.gate_coupling};
        cap_device C_gate_clock_n_to_hold{*this,"C_gate_clock_n_to_hold",clock_n,OUT,P.gate_coupling};
        if (P.leakage < 0 || !std::isfinite(P.leakage) || O.leakage_scale < 0 || !std::isfinite(O.leakage_scale))
            throw diagnostic("E_VALUE", "invalid hold conductance");
        if (P.leakage * O.leakage_scale > 0) {
            res_device R_hold_leakage{*this,"R_hold_leakage",OUT,Vss,
                1/(P.leakage*O.leakage_scale),false};
        }
    }
};

template<unsigned Bits, device_parameters D, passive_parameters P, corner_parameters O>
class r2r_dac : public primitive<D,P,O> {
    using B = primitive<D,P,O>;
public:
    using B::Vdd; using B::Vss; using B::cap; using B::res; using B::inverter;
    using typename B::mos_device; using typename B::cap_device; using typename B::res_device;
    std::array<port<electrical>,Bits> BIT_N; // Complemented bits, LSB first.
    _PORT(electrical) OUT;
    std::array<electrical,Bits> tap, driver; // Physical ladder and finite rail drivers.
    void _assign() override {
        tap[Bits-1] = OUT;
        for (unsigned i=0;i<Bits;++i) {
            const std::string bit="bit"+std::to_string(i)+"/";
            mos_device M0_driver_n{*this,bit+"M0_driver_n",driver[i],BIT_N[i],Vss,Vss,D.dac.n};
            mos_device M1_driver_p{*this,bit+"M1_driver_p",driver[i],BIT_N[i],Vdd,Vdd,D.dac.p,true};
            res_device R0_weight_2r{*this,bit+"R0_weight_2r",tap[i],driver[i],
                2*P.ladder*(1+P.r2_mismatch[i])};
            if (i) {
                res_device R1_tap_link_r{*this,bit+"R1_tap_link_r",tap[i],tap[i-1],
                    P.ladder*(1+P.r_mismatch[i])};
            }
        }
        res_device R0_termination_2r{*this,"R0_termination_2r",tap[0],Vss,
            2*P.ladder*(1+P.termination_mismatch)};
        for (unsigned i=0;i<Bits;++i) {
            const std::string bit="bit"+std::to_string(i)+"/";
            cap_device C0_tap_load{*this,bit+"C0_tap_load",tap[i],Vss,P.dac_cap};
            cap_device C1_driver_load{*this,bit+"C1_driver_load",driver[i],Vss,P.dac_cap};
        }
    }
};

template<device_parameters D, passive_parameters P, corner_parameters O>
class preamplifier : public primitive<D,P,O> {
    using B = primitive<D,P,O>;
public:
    using B::Vdd; using B::Vss; using B::mos; using B::cap; using B::res;
    using typename B::mos_device; using typename B::cap_device; using typename B::res_device;
    _PORT(electrical) PLUS; _PORT(electrical) MINUS; _PORT(electrical) OUTP; _PORT(electrical) OUTN;
    electrical left_source, right_source, tail; // Degeneration and shared tail set common-mode behavior.
    void _assign() override {
        mos_device M0_held_input{*this,"M0_held_input",OUTN,PLUS,left_source,Vss,D.preamp};
        mos_device M1_dac_input{*this,"M1_dac_input",OUTP,MINUS,right_source,Vss,D.preamp};
        res_device R0_negative_output_load{*this,"R0_negative_output_load",OUTN,Vdd,P.preamp_load};
        res_device R1_positive_output_load{*this,"R1_positive_output_load",OUTP,Vdd,P.preamp_load};
        res_device R2_held_degeneration{*this,"R2_held_degeneration",left_source,tail,P.degeneration};
        res_device R3_dac_degeneration{*this,"R3_dac_degeneration",right_source,tail,P.degeneration};
        res_device R4_tail_return{*this,"R4_tail_return",tail,Vss,P.tail_return};
        cap_device C0_negative_output_load{*this,"C0_negative_output_load",OUTN,Vdd,P.preamp_output};
        cap_device C1_positive_output_load{*this,"C1_positive_output_load",OUTP,Vdd,P.preamp_output};
        cap_device C2_tail_load{*this,"C2_tail_load",tail,Vss,P.preamp_tail};
        cap_device C3_held_gate_source{*this,"C3_held_gate_source",PLUS,left_source,P.preamp_gs};
        cap_device C4_dac_gate_source{*this,"C4_dac_gate_source",MINUS,right_source,P.preamp_gs};
        cap_device C5_held_gate_drain{*this,"C5_held_gate_drain",PLUS,OUTN,P.preamp_gd};
        cap_device C6_dac_gate_drain{*this,"C6_dac_gate_drain",MINUS,OUTP,P.preamp_gd};
    }
};

template<device_parameters D, passive_parameters P, corner_parameters O>
class comparator : public primitive<D,P,O> {
    using B = primitive<D,P,O>;
public:
    using B::Vdd; using B::Vss; using B::mos; using B::cap;
    using typename B::mos_device; using typename B::cap_device;
    _PORT(electrical) PLUS; _PORT(electrical) MINUS; _PORT(electrical) CLOCK;
    _PORT(electrical) OUTP; _PORT(electrical) OUTN;
    electrical left, right, tail; // Dynamic nodes retain reset/evaluate history.
    void _assign() override {
        if (!std::isfinite(P.comparator_left_trim) || P.comparator_left_trim<0)
            throw diagnostic("E_VALUE", "invalid comparator MIM trim");
        mos_device M0_plus_input{*this,"M0_plus_input",left,PLUS,tail,Vss,D.comparator_input};
        mos_device M1_minus_input{*this,"M1_minus_input",right,MINUS,tail,Vss,D.comparator_input};
        mos_device M2_evaluate_tail{*this,"M2_evaluate_tail",tail,CLOCK,Vss,Vss,D.tail};
        mos_device M3_outn_regen_n{*this,"M3_outn_regen_n",OUTN,OUTP,left,Vss,D.feedback.n};
        mos_device M4_outp_regen_n{*this,"M4_outp_regen_n",OUTP,OUTN,right,Vss,D.feedback.n};
        mos_device M5_outp_regen_p{*this,"M5_outp_regen_p",OUTP,OUTN,Vdd,Vdd,D.feedback.p,true};
        mos_device M6_outn_regen_p{*this,"M6_outn_regen_p",OUTN,OUTP,Vdd,Vdd,D.feedback.p,true};
        mos_device M7_left_precharge{*this,"M7_left_precharge",left,CLOCK,Vdd,Vdd,D.precharge,true};
        cap_device C0_left_internal{*this,"C0_left_internal",left,Vss,P.comparator_internal+P.comparator_left_trim};
        mos_device M8_right_precharge{*this,"M8_right_precharge",right,CLOCK,Vdd,Vdd,D.precharge,true};
        cap_device C1_right_internal{*this,"C1_right_internal",right,Vss,P.comparator_internal};
        mos_device M9_outp_precharge{*this,"M9_outp_precharge",OUTP,CLOCK,Vdd,Vdd,D.precharge,true};
        mos_device M10_outn_precharge{*this,"M10_outn_precharge",OUTN,CLOCK,Vdd,Vdd,D.precharge,true};
        cap_device C2_tail_internal{*this,"C2_tail_internal",tail,Vss,P.comparator_internal};
        cap_device C3_outp_load{*this,"C3_outp_load",OUTP,Vss,P.comparator_output};
        cap_device C4_outn_load{*this,"C4_outn_load",OUTN,Vss,P.comparator_output};
        cap_device C5_plus_gate_source{*this,"C5_plus_gate_source",PLUS,tail,P.comparator_gs};
        cap_device C6_minus_gate_source{*this,"C6_minus_gate_source",MINUS,tail,P.comparator_gs};
        cap_device C7_plus_gate_drain{*this,"C7_plus_gate_drain",PLUS,left,P.comparator_gd};
        cap_device C8_minus_gate_drain{*this,"C8_minus_gate_drain",MINUS,right,P.comparator_gd};
    }
};

template<device_parameters D, passive_parameters P, corner_parameters O>
class decision_latch : public primitive<D,P,O> {
    using B = primitive<D,P,O>;
public:
    using B::Vdd; using B::Vss; using B::mos; using B::cap; using B::inverter;
    using typename B::mos_device; using typename B::cap_device;
    _PORT(electrical) SET_N; _PORT(electrical) RESET_N; _PORT(electrical) CLEAR;
    _PORT(electrical) Q; _PORT(electrical) Q_N;
    std::array<electrical,2> first, second, stack; // Buffers isolate comparator kickback; stacks store charge.
    electrical clear_n; // Clear polarity for the PMOS forcing Q_N high at startup.
    void _assign() override {
        std::array<electrical,2> input{SET_N,RESET_N}, out{Q,Q_N}, feedback{Q_N,Q};
        for (unsigned i=0;i<2;++i) {
            const std::string branch=(i ? "qn/" : "q/");
            mos_device M0_input_pullup{*this,branch+"M0_input_pullup",out[i],second[i],Vdd,Vdd,D.nand.p,true};
            mos_device M1_feedback_pullup{*this,branch+"M1_feedback_pullup",out[i],feedback[i],Vdd,Vdd,D.nand.p,true};
            mos_device M2_input_pulldown{*this,branch+"M2_input_pulldown",out[i],second[i],stack[i],Vss,D.nand.n};
            mos_device M3_feedback_pulldown{*this,branch+"M3_feedback_pulldown",stack[i],feedback[i],Vss,Vss,D.nand.n};
        }
        // Auxiliary isolation buffers, clear devices and finite storage follow the latch core.
        for (unsigned i=0;i<2;++i) {
            const std::string branch=(i ? "reset/" : "set/");
            mos_device M0_first_buffer_n{*this,branch+"M0_first_buffer_n",first[i],input[i],Vss,Vss,D.buffer_input.n};
            mos_device M1_first_buffer_p{*this,branch+"M1_first_buffer_p",first[i],input[i],Vdd,Vdd,D.buffer_input.p,true};
            mos_device M2_second_buffer_n{*this,branch+"M2_second_buffer_n",second[i],first[i],Vss,Vss,D.buffer_output.n};
            mos_device M3_second_buffer_p{*this,branch+"M3_second_buffer_p",second[i],first[i],Vdd,Vdd,D.buffer_output.p,true};
            cap_device C0_input_load{*this,branch+"C0_input_load",input[i],Vss,P.latch_input};
            cap_device C1_miller{*this,branch+"C1_miller",input[i],first[i],P.latch_miller};
            cap_device C2_first_buffer_load{*this,branch+"C2_first_buffer_load",first[i],Vss,P.buffer_internal+P.buffer_input};
            cap_device C3_second_buffer_load{*this,branch+"C3_second_buffer_load",second[i],Vss,P.buffer_output+P.latch_gate};
            cap_device C4_retained_output{*this,branch+"C4_retained_output",out[i],Vss,P.latch_storage};
            cap_device C5_stack_node{*this,branch+"C5_stack_node",stack[i],Vss,P.latch_stack};
        }
        mos_device M0_inverter_n{*this,"clear/M0_inverter_n",clear_n,CLEAR,Vss,Vss,D.nand.n};
        mos_device M1_inverter_p{*this,"clear/M1_inverter_p",clear_n,CLEAR,Vdd,Vdd,D.nand.p,true};
        cap_device C0_inverter_load{*this,"clear/C0_inverter_load",clear_n,Vss,P.latch_gate};
        mos_device M2_force_q_low{*this,"clear/M2_force_q_low",Q,CLEAR,Vss,Vss,D.clear.n};
        mos_device M3_force_qn_high{*this,"clear/M3_force_qn_high",Q_N,clear_n,Vdd,Vdd,D.clear.p,true};
    }
};

template<device_parameters D, passive_parameters P, corner_parameters O>
class core_interface : public primitive<D,P,O> {
    using B = primitive<D,P,O>;
public:
    using B::Vdd; using B::Vss; using B::mos; using B::cap; using B::inverter;
    using typename B::mos_device; using typename B::cap_device;
    _PORT(electrical) A; _PORT(electrical) Y;
    electrical inverted; // Ratioed HV-input/core-output node, including static current.
    void _assign() override {
        mos_device M0_core_pullup{*this,"M0_core_pullup",inverted,Vss,Vdd,Vdd,D.interface_pullup,true};
        mos_device M1_hv_input{*this,"M1_hv_input",inverted,A,Vss,Vss,D.interface_input};
        mos_device M2_core_output_n{*this,"M2_core_output_n",Y,inverted,Vss,Vss,D.interface_output.n};
        mos_device M3_core_output_p{*this,"M3_core_output_p",Y,inverted,Vdd,Vdd,D.interface_output.p,true};
        cap_device C0_hv_input_ground{*this,"C0_hv_input_ground",A,Vss,P.interface_input};
        cap_device C1_hv_input_core{*this,"C1_hv_input_core",A,Vdd,P.interface_input};
        cap_device C2_input_feedback{*this,"C2_input_feedback",A,inverted,P.interface_feedback};
        cap_device C3_internal_ground{*this,"C3_internal_ground",inverted,Vss,P.interface_internal};
        cap_device C4_internal_core{*this,"C4_internal_core",inverted,Vdd,P.interface_input};
        cap_device C5_output_feedback{*this,"C5_output_feedback",inverted,Y,P.interface_feedback};
        cap_device C6_decision_output{*this,"C6_decision_output",Y,Vss,P.interface_output};
    }
};

} // namespace analog::adc_sar
