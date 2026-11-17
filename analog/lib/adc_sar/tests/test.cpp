#include "../adc_sar_slice.h"
#include "../process_export.h"
#include "../qualification_limits.h"
#include <spef_rc.hpp>
#ifdef ADC_USE_EIGEN
#include <sparse.hpp>
#endif
#include <fstream>
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <memory>
#include <functional>
#include <set>

using namespace analog;
using namespace analog::adc_sar;

static void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
static void setup(simulation& sim) {
    sim.analytic_jacobian=true;
    sim.minimum_step=duration(.05e-12);
#ifdef ADC_USE_EIGEN
    use_sparse_solver(sim,sparse_strategy::certified_lu);
#endif
}
static bits<1> control(bool& value) {
    // A clocked bench register drives the finite electrical source. Commands
    // are committed on 100 ps bench edges, independently of the ADC clock.
    struct state { bool pending=false, committed=false; };
    auto reg=std::make_shared<state>();
    reg->pending=reg->committed=value;
    bits<1> clock;
    simulation::current().clock(clock,duration(100e-12));
    simulation::current().register_clocked(clock,reg.get(),
        [reg,&value](bool) { reg->pending=value; },
        [reg] { reg->committed=reg->pending; });
    digital_operation op{digital_law::sim_registered_output};
    op.native_identity=reg.get();
    op.read=[reg]() -> bit_value { return reg->committed; };
    bits<1> result;
    result=digital_purpose{&simulation::current(),1,std::move(op)};
    return result;
}
static void reject(const std::function<void()>& fn) {
    try { fn(); } catch (const diagnostic&) { return; }
    throw std::runtime_error("invalid configuration accepted");
}

template<class Slice> static void wire(Slice& adc, electrical& g, electrical& v, electrical& core) {
    adc.Vdd=v; adc.Vss=g; adc.Vlogic=core; adc.IN=g; adc.TRACK=g; adc.EVALUATE=g; adc.CLEAR=g;
    adc.AGGRESSOR=g;
    for (auto& bit : adc.BIT_N) bit=v;
}

static void parameters() {
    { std::ostringstream output; write_qualification_limits<adc_sar_slice<>>(output);
      require(output.str().find("\"dnl\": null")!=std::string::npos,
              "an unspecified linearity budget became a product pass");
      constexpr auto budget=[] { auto p=requirements{}; p.dnl=.25; p.average_power=.012; return p; }();
      using budget_slice=adc_sar_slice<8,ihp130_profile2,ihp130_passives,nominal_corner,schematic_layout,budget>;
      output.str(""); output.clear(); write_qualification_limits<budget_slice>(output);
      require(output.str().find("\"dnl\": 0.25")!=std::string::npos &&
              output.str().find("\"average_power\": 0.012")!=std::string::npos,
              "bound slice product budgets were not exported"); }
    simulation sim;
    electrical g,v,c;
    sim.reference(g); v=sim_voltage_source(g,voltage(3.3)); c=sim_voltage_source(g,voltage(1.2));
    adc_sar_slice<> adc; wire(adc,g,v,c); adc.elaborate();
    require(adc.physical_bindings().size()==59,"changed slice MOS inventory");
    require(adc.endpoints.size()==36,"missing route endpoints");
    std::set<std::string> model_names;
    for(std::size_t i=adc.first_element;i<adc.last_element;++i) {
        const auto& name=sim.elements.at(i).name;
        require(!name.empty(),"unnamed SAR model element");
        require(model_names.insert(name).second,"duplicate SAR model element name: "+name);
    }
    require(sim.elements.at(adc.first_element+3).name=="sampler/C0_capture",
            "sampler storage name is missing from the actual model element");
    require(sim.elements.at(adc.first_element+17).name=="ladder/bit0/M1_driver_p",
            "ladder driver name is missing from the actual model element");
    for (const auto& b : adc.physical_bindings()) {
        const auto& e=sim.elements.at(b.element);
        require(e.kind==law::nmos || e.kind==law::pmos,"MOS export identity lost");
    }
    constexpr auto bad=[] { auto p=ihp130_passives; p.hold=-1; return p; }();
    reject([] { simulation s; adc_sar_slice<8,ihp130_profile2,bad> a; a.elaborate(); });
    constexpr auto bad_route=[] { auto p=schematic_layout; p.input_r=-1; return p; }();
    reject([] { simulation s; adc_sar_slice<8,ihp130_profile2,ihp130_passives,nominal_corner,bad_route> a; a.elaborate(); });
    constexpr auto external=[] { auto p=schematic_layout; p.external_routes=true; return p; }();
    reject([] { simulation s; adc_sar_slice<8,ihp130_profile2,ihp130_passives,nominal_corner,external> a; a.elaborate(); });
    // A second resolution is a real independent electrical topology.
    { simulation s; adc_sar_slice<4> a; a.elaborate(); require(a.physical_bindings().size()==51,"resolution not propagated"); }
    constexpr auto named_routes=[] { auto p=schematic_layout; p.input_r=10; p.input_c=1e-15;
                                      p.track_held_c=1e-15; return p; }();
    { simulation s; adc_sar_slice<8,ihp130_profile2,ihp130_passives,nominal_corner,named_routes> a,b;
      a.name("rx0"); b.name("rx1"); a.elaborate(); b.elaborate();
      require(s.elements.at(a.first_element+3).name=="rx0/sampler/C0_capture","first slice hierarchy lost");
      require(s.elements.at(b.first_element+3).name=="rx1/sampler/C0_capture","second slice hierarchy lost");
      require(s.elements.at(a.last_element-1).name=="rx0/coupling/C_track_held","first slice coupling name lost");
      require(s.elements.at(b.last_element-1).name=="rx1/coupling/C_track_held","second slice coupling name lost");
      std::set<std::string> all;
      for (const auto& e:s.elements) require(all.insert(e.name).second,"slice model element names collide"); }
    { simulation s; adc_sar_slice<> a; a.elaborate(); std::ostringstream deck;
      write_process_subcircuit(deck,a); auto text=deck.str();
      require(text.find("* sampler/C0_capture\n")!=std::string::npos,
              "model element name lost in generic process export");
      require(text.find("sg13_hv_nmos w=1.5")!=std::string::npos,"sampler physical width lost");
      require(a.sampler.mim_bindings.size()==5,"series MIM decomposition changed");
      require(text.find("cap_cmim")!=std::string::npos,"missing physical MIM binding");
      reject([&] { auto m=process_models{}; m.hv_n="bad\n.include hostile"; write_process_subcircuit(deck,a,m); }); }
    constexpr auto trim=[] { auto p=ihp130_passives; p.comparator_left_trim=3e-15; return p; }();
    { simulation s; adc_sar_slice<8,ihp130_profile2,trim> a; a.elaborate();
      const auto& left=s.elements.at(a.first_element+86);
      const auto& right=s.elements.at(a.first_element+88);
      require(left.kind==law::capacitor && right.kind==law::capacitor,"comparator trim changed physical inventory");
      require(std::abs((left.c-right.c)-3e-15)<1e-20,"placed comparator MIM trim lost from C++ export"); }
    constexpr auto negative_trim=[] { auto p=ihp130_passives; p.comparator_left_trim=-1e-15; return p; }();
    reject([] { simulation s; adc_sar_slice<8,ihp130_profile2,negative_trim> a; a.elaborate(); });
}

static double acquisition(double source_r, double track_ns, double held_c=0) {
    simulation sim; setup(sim);
    electrical g,v,input;
    sim.reference(g); v=sim_voltage_source(g,voltage(3.3));
    input=sim_pwl_voltage_source(g,{{0,0},{.1e-9,1.65},{30e-9,1.65}});
    sample_hold<ihp130_profile2,ihp130_passives,nominal_corner> dut;
    dut.IN=resistor(input,resistance(source_r)); dut.Vdd=v; dut.Vss=g;
    dut.CLOCK=sim_pwl_voltage_source(g,{{0,3.3},{track_ns*1e-9,3.3},{track_ns*1e-9+.1e-9,0}});
    dut.elaborate();
    if (held_c) dut.OUT=capacitor(g,capacitance_value(held_c));
    sim.observe("held",dut.OUT,g);
    sim.run(duration((track_ns+.5)*1e-9),duration(10e-12));
    return std::abs(sim.voltage(dut.OUT,g).value-1.65);
}

static void acquisition_suite(const std::filesystem::path& dir) {
    std::ofstream out(dir/"acquisition.csv"); out << "source_ohm,track_ns,extra_hold_F,error_V\n";
    for (double r : {25.,50.,500.,5000.}) for (double t : {.1,.5,1.,2.,4.,8.}) {
        double error=acquisition(r,t);
        require(std::isfinite(error),"nonfinite acquisition");
        out << r << ',' << t << ",0," << error << '\n';
        if (r<=50 && t==8) require(error<.020,"nominal sampler acquisition");
        if (r==5000 && t==.1) require(error>.020,"underdriven sampler falsely passed");
    }
    double unloaded=acquisition(500,1), loaded=acquisition(500,1,4e-12);
    require(loaded>unloaded+.1,"hold load does not affect acquisition");
    out << "500,1,4e-12," << loaded << '\n';
}

static void dac_suite(const std::filesystem::path& dir) {
    simulation sim; setup(sim);
    electrical g,v; sim.reference(g); v=sim_voltage_source(g,voltage(3.3));
    r2r_dac<8,ihp130_profile2,ihp130_passives,nominal_corner> dut;
    dut.Vdd=v; dut.Vss=g;
    std::array<bool,8> values{};
    for (unsigned i=0;i<8;++i) dut.BIT_N[i]=sim_driver(control(values[i]),v,g,resistance(50));
    dut.elaborate();
    std::ofstream out(dir/"dac.csv"); out << "code,voltage_V,error_LSB,dnl_LSB\n";
    double previous=0, maximum=0;
    for (unsigned code=0;code<256;++code) {
        for (unsigned bit=0;bit<8;++bit) values[bit]=!(code&(1u<<bit));
        if (code==0) sim.run(duration(2e-9),duration(20e-12));
        else sim.advance_to(duration((code+1)*2e-9),duration(20e-12));
        double actual=sim.voltage(dut.OUT,g).value, error=actual*256/3.3-code;
        double dnl=code ? (actual-previous)*256/3.3-1 : 0;
        maximum=std::max(maximum,std::abs(error));
        require(actual>=previous-1e-8,"nonmonotonic DAC");
        require(std::abs(error)<1,"DAC error exceeds 1 LSB");
        out << code << ',' << actual << ',' << error << ',' << dnl << '\n';
        previous=actual; sim.discard_trace();
    }
    std::cout << "DAC max error " << maximum << " LSB\n";
}

struct conversion_config {
    double dt=10e-12, period=4e-9, track=8e-9, io=3.3, core=1.2, source_r=50, warmup=20e-9;
    double control_r=50, control_c=0, evaluate_skew=0, evaluate_jitter=0;
    double ripple=0, ripple_period=16e-9, evaluate_fraction=.5;
    double sine_frequency=0, sine_amplitude=.45, sine_center=1.45;
    bool characterize=false, physical_clocks=false;
    double clock_slew=50e-12, random_jitter=0, core_ripple=0;
    bool sinusoidal_ripple=false;
    unsigned random_seed=7;
    std::vector<double> levels{.85,1.15,1.65,2.05,1.4};
    bool gate=true;
    bool all_nodes=false;
    integration_method method=integration_method::backward_euler;
    double edge_step=0,edge_window=0;
    bool power_cycle=false;
    // Adaptive bench stimulus/observation only. The electrical slice is still
    // elaborated exactly once, and no analog state is reset between requests.
    std::function<std::optional<double>()> next_input;
    std::function<void(const std::vector<double>&)> completed;
};

static void append_stimulus(simulation& sim, std::size_t source, double begin, double value) {
    // Only append FUTURE PWL segments. Existing source history and the circuit
    // topology stay unchanged; advance_to() schedules these new breakpoints.
    auto& e=sim.elements.at(source);
    require(e.kind==law::sim_voltage_source && !e.waveform.empty(),"adaptive input needs a PWL voltage source");
    require(std::isfinite(begin) && std::isfinite(value) && begin>sim.elapsed().value
        && begin>e.waveform.back().first,"adaptive stimulus would rewrite accepted history");
    const double previous=e.waveform.back().second;
    e.waveform.emplace_back(begin,previous);
    e.waveform.emplace_back(begin+.1e-9,value);
}

static void adaptive_input_suite(const std::filesystem::path& dir) {
    // Independently integrate a piecewise-linear source through a known RC.
    // This catches lost breakpoints and accidental state resets between queries.
    simulation sim; setup(sim);
    electrical g,input,output; sim.reference(g);
    const auto source=sim.elements.size();
    input=sim_pwl_voltage_source(g,{{0,0}});
    output=resistor(input,resistance(1000));
    output=capacitor(g,capacitance_value(1e-12));
    sim.run(duration(1e-9),duration(1e-12));
    double expected=0, previous=0;
    std::ofstream out(dir/"adaptive_input.csv");
    out << "time_s,source_V,measured_V,analytic_V,error_V\n";
    for (double value : {1.,.2,1.7,.4}) {
        const double begin=sim.elapsed().value+.2e-9, tau=1e-9, ramp=.1e-9;
        append_stimulus(sim,source,begin,value);
        expected=previous+(expected-previous)*std::exp(-.2e-9/tau);
        const double slope=(value-previous)/ramp;
        expected=value-slope*tau+(expected-previous+slope*tau)*std::exp(-ramp/tau);
        expected=value+(expected-value)*std::exp(-.7e-9/tau);
        sim.advance_to(duration(begin+ramp+.7e-9),duration(1e-12));
        const double measured=sim.voltage(output,g).value;
        require(std::abs(sim.voltage(input,g).value-value)<1e-9,"future PWL segment was not applied");
        require(std::abs(measured-expected)<.5e-3,"adaptive input lost RC history or ramp breakpoints");
        out << sim.elapsed().value << ',' << value << ',' << measured << ',' << expected << ',' << measured-expected << '\n';
        previous=value; sim.discard_trace();
    }
    auto bad=[&](double begin,double value) {
        const auto size=sim.elements.at(source).waveform.size();
        bool rejected=false;
        try { append_stimulus(sim,source,begin,value); } catch (const std::runtime_error&) { rejected=true; }
        require(rejected && sim.elements.at(source).waveform.size()==size,"invalid adaptive stimulus changed history");
    };
    bad(sim.elapsed().value,1.); bad(sim.elapsed().value+1e-9,NAN); bad(NAN,1.);
}

template<class Slice=adc_sar_slice<>>
static unsigned conversion(const std::filesystem::path& dir, const std::string& name, conversion_config cfg={}) {
    const double dt=cfg.dt, spacing=cfg.track+8*cfg.period+10e-9;
    simulation sim; setup(sim); sim.method=cfg.method;
    sim.edge_step=duration(cfg.edge_step);sim.edge_window=duration(cfg.edge_window);
    electrical g,source,v,logic_source,core,input;
    sim.reference(g);
    auto rail_wave=[&](double nominal) {
        std::vector<std::pair<double,double>> wave{{0,0},{1e-9,nominal}};
        if (cfg.power_cycle) for (unsigned i=0;i+1<cfg.levels.size();++i) {
            double at=cfg.warmup+spacing*i+cfg.track+8*cfg.period+3e-9;
            wave.emplace_back(at,nominal); wave.emplace_back(at+.5e-9,0);
            wave.emplace_back(at+1.5e-9,0); wave.emplace_back(at+2.5e-9,nominal);
        }
        return wave;
    };
    auto supply_wave=rail_wave(cfg.io);
    if (cfg.ripple && !cfg.sinusoidal_ripple) for (unsigned i=0;(2+i*.5)*cfg.ripple_period<20e-9+spacing*cfg.levels.size();++i)
        supply_wave.emplace_back((2+i*.5)*cfg.ripple_period,cfg.io+(i%2 ? -cfg.ripple : cfg.ripple));
    auto ripple_wave=[&](double nominal,double amplitude) {
        if (!amplitude) return rail_wave(nominal);
        std::vector<std::pair<double,double>> wave{{0,0},{1e-9,nominal}};
        const double step=cfg.ripple_period/32, end=cfg.warmup+spacing*cfg.levels.size();
        for (unsigned i=0;2e-9+i*step<=end+step;++i) {
            double t=2e-9+i*step;
            wave.emplace_back(t,nominal+amplitude*std::sin(2*std::acos(-1.)*(t-2e-9)/cfg.ripple_period));
        }
        return wave;
    };
    if (cfg.sinusoidal_ripple) supply_wave=ripple_wave(cfg.io,cfg.ripple);
    source=sim_pwl_voltage_source(g,supply_wave);
    v=resistor(source,resistance(.25)); v=capacitor(g,capacitance_value(20e-12));
    logic_source=sim_pwl_voltage_source(g,cfg.sinusoidal_ripple ? ripple_wave(cfg.core,cfg.core_ripple) : rail_wave(cfg.core));
    core=resistor(logic_source,resistance(10)); core=capacitor(g,capacitance_value(20e-12));
    // Repeated operation preserves analog history, including the first cold start.
    const auto& levels=cfg.levels;
    std::vector<std::pair<double,double>> waveform{{0,levels[0]}};
    for (unsigned i=1;i<levels.size();++i) {
        waveform.emplace_back(cfg.warmup+spacing*i,levels[i-1]);
        waveform.emplace_back(cfg.warmup+spacing*i+.1e-9,levels[i]);
    }
    if (cfg.sine_frequency) {
        waveform.clear();
        const double step=cfg.period/16, end=cfg.warmup+spacing*levels.size();
        for (unsigned i=0;i*step<=end+step;++i) waveform.emplace_back(i*step,
            cfg.sine_center+cfg.sine_amplitude*std::sin(2*std::acos(-1.)*cfg.sine_frequency*i*step));
    }
    if (cfg.next_input) waveform={{0,1.65}};
    const auto input_source=sim.elements.size();
    input=sim_pwl_voltage_source(g,waveform);
    Slice adc;
    adc.Vdd=v; adc.Vss=g; adc.Vlogic=core; adc.IN=resistor(input,resistance(cfg.source_r)); adc.AGGRESSOR=g;
    bool track=true,evaluate=false,clear=true;
    std::array<bool,8> bits; bits.fill(true);
    std::vector<double> aperture_offset(levels.size()),evaluation_offset(8*levels.size());
    if (cfg.physical_clocks) {
        require(!cfg.next_input && !cfg.power_cycle,"finite physical-clock fixture does not support adaptive input/power cycles");
        noise_random aperture_rng(cfg.random_seed^0x4bbac518ULL), evaluation_rng(cfg.random_seed^0x98aceb27ULL);
        for (auto& x:aperture_offset) x=cfg.random_jitter*aperture_rng.normal();
        for (auto& x:evaluation_offset) x=cfg.random_jitter*evaluation_rng.normal();
        std::vector<std::pair<double,double>> tw{{0,cfg.io}},ew{{0,0}};
        auto edge=[&](auto& wave,double t,double from,double to) {
            require(t>wave.back().first,"overlapping physical clock edges");
            wave.emplace_back(t,from);wave.emplace_back(t+cfg.clock_slew,to);
        };
        for (unsigned sample=0;sample<levels.size();++sample) {
            const double origin=cfg.warmup+spacing*sample;
            if (sample)edge(tw,origin,0,cfg.io);
            edge(tw,origin+cfg.track+aperture_offset[sample],cfg.io,0);
            for (unsigned bit=0;bit<8;++bit) {
                const double base=origin+cfg.track+1e-9+bit*cfg.period;
                const double begin=base+cfg.period*(1-cfg.evaluate_fraction)+evaluation_offset[8*sample+bit];
                require(begin+cfg.clock_slew<base+cfg.period-25e-12,"jitter consumes evaluation window");
                edge(ew,begin,0,cfg.io);edge(ew,base+cfg.period,cfg.io,0);
            }
        }
        electrical ts,es;ts=sim_pwl_voltage_source(g,tw);es=sim_pwl_voltage_source(g,ew);
        adc.TRACK=resistor(ts,resistance(cfg.control_r));adc.EVALUATE=resistor(es,resistance(cfg.control_r));
    } else {
        adc.TRACK=sim_driver(control(track),v,g,resistance(cfg.control_r));
        adc.EVALUATE=sim_driver(control(evaluate),v,g,resistance(cfg.control_r));
    }
    adc.CLEAR=sim_driver(control(clear),v,g,resistance(cfg.control_r));
    for (unsigned i=0;i<8;++i) adc.BIT_N[i]=sim_driver(control(bits[i]),v,g,resistance(cfg.control_r));
    if (cfg.control_c) {
        adc.TRACK=capacitor(g,capacitance_value(cfg.control_c));
        adc.EVALUATE=capacitor(g,capacitance_value(cfg.control_c));
        adc.CLEAR=capacitor(g,capacitance_value(cfg.control_c));
        for (auto& pin : adc.BIT_N) pin=capacitor(g,capacitance_value(cfg.control_c));
    }
    adc.CORE_DECISION=capacitor(g,capacitance_value(20e-15));
    adc.elaborate();
    sim.observe("held",adc.sampler.OUT,g); sim.observe("dac",adc.ladder.OUT,g);
    sim.observe("decision",adc.CORE_DECISION,g); sim.observe("vdd",v,g); sim.observe("core",core,g);
    sim.observe("source",source,g); sim.observe("logic_source",logic_source,g);
    sim.observe("input",adc.IN,g);
    sim.observe("positive",adc.comparison.OUTP,g); sim.observe("negative",adc.comparison.OUTN,g);
    if (cfg.characterize) {
        sim.observe("ideal_input",input,g);sim.observe("track",adc.TRACK,g);sim.observe("evaluate",adc.EVALUATE,g);
    }
    if (cfg.all_nodes) for (const auto& [name,node] : adc.all_nodes()) sim.observe(name,node,g);
    sim.run(duration(std::min(cfg.warmup,80000*dt)),duration(dt));
    while(sim.elapsed().value<cfg.warmup) {
        double end=std::min(cfg.warmup,sim.elapsed().value+80000*dt);
        sim.advance_to(duration(end),duration(std::min(dt,end-sim.elapsed().value)));
    }
    clear=false;
    std::ofstream detail,edges,power;
    if (cfg.characterize) {
        detail.open(dir/(name+"_characterization.csv")); detail<<std::setprecision(17);
        detail<<"sample,aperture_s,requested_aperture_offset_s,ideal_input_V,pin_input_V,held_V,external_acquisition_V,external_code_error,io_capture_V,core_capture_V,hold_kickback_peak_V,hold_kickback_charge_C,full_cycle_s,io_energy_J,core_energy_J,io_average_A,core_average_A,io_peak_A,core_peak_A,pass\n";
        edges.open(dir/(name+"_edges.csv"));edges<<std::setprecision(17)<<"sample,kind,bit,requested_offset_s,delivered_offset_s\n";
        power.open(dir/(name+"_power.csv"));power<<std::setprecision(17)<<"phase,sample,duration_s,io_energy_J,core_energy_J,io_charge_C,core_charge_C,io_peak_A,core_peak_A\n";
    }
    auto interpolate=[&](double t,unsigned column) {
        const auto& trace=sim.trace;
        auto it=std::lower_bound(trace.begin(),trace.end(),t,[](const auto& row,double x){return row.time<x;});
        require(it!=trace.end(),"missing observation endpoint");
        if (it==trace.begin() || it->time==t)return *it->values[column];
        auto previous=it-1;double f=(t-previous->time)/(it->time-previous->time);
        return *previous->values[column]+f*(*it->values[column]-*previous->values[column]);
    };
    auto crossing=[&](unsigned column,double lo,double hi,bool rising) {
        const auto& trace=sim.trace;double level=cfg.io/2;
        for (std::size_t i=1;i<trace.size();++i) {
            const auto& a=trace[i-1];const auto& b=trace[i];
            if (b.time<lo || a.time>hi)continue;
            double x=*a.values[column],y=*b.values[column];
            if ((rising && x<level && y>=level) || (!rising && x>level && y<=level))
                return a.time+(level-x)/(y-x)*(b.time-a.time);
        }
        throw std::runtime_error("missing physical clock crossing");
    };
    // Trapezoidal source-side energy and charge on the actual nonuniform grid.
    // Returned order: elapsed, IO/core energy, IO/core charge, IO/core peak.
    auto integral=[&](double lo,double hi) {
        std::array<double,7> result{};result[0]=hi-lo;
        auto point=[&](double t) {
            double vs=interpolate(t,5),vc=interpolate(t,6);
            double i=(vs-interpolate(t,3))/.25,ic=(vc-interpolate(t,4))/10;
            return std::array<double,4>{vs*i,vc*ic,i,ic};
        };
        auto previous=point(lo);double at=lo;
        result[5]=previous[2];result[6]=previous[3];
        auto add=[&](double t) {
            auto value=point(t);double step=t-at;
            for (unsigned j=0;j<4;++j)result[j+1]+=step*(previous[j]+value[j])/2;
            result[5]=std::max(result[5],value[2]);result[6]=std::max(result[6],value[3]);
            at=t;previous=value;
        };
        for (const auto& row:sim.trace)if(row.time>lo && row.time<hi)add(row.time);
        add(hi);return result;
    };
    auto power_row=[&](const char* phase,unsigned sample,const auto& m) {
        power<<phase<<','<<sample;for(auto value:m)power<<','<<value;power<<'\n';
    };
    if (cfg.characterize)power_row("startup",0,integral(0,cfg.warmup));
    std::ofstream out(dir/(name+".csv"));
    std::ofstream bit_report(dir/(name+"_decisions.csv"));
    bit_report << "sample,bit,trial,held_V,dac_V,preamp_positive_V,preamp_negative_V,positive_V,negative_V,core_V\n";
    out << std::setprecision(12) << "sample,input_V,acquisition_V,hold_pp_V,absolute_hold_V,code,ideal,code_error,decisions,io_average_A,io_peak_A,energy_J,minimum_decision_margin_s,invalid_decisions,pass\n";
    unsigned failures=0;
    const unsigned sample_limit=cfg.next_input ? 10000 : levels.size();
    for (unsigned sample=0;sample<sample_limit;++sample) {
        double origin=cfg.warmup+spacing*sample;
        if (cfg.next_input) {
            auto value=cfg.next_input();
            if (!value) break;
            require(*value>=Slice::limits.input_min && *value<=Slice::limits.input_max,"transition input outside specified range");
            append_stimulus(sim,input_source,origin+.05e-9,*value);
        }
        track=true; evaluate=false;
        // Establish the first SAR trial while the sampling switch is CLOSED.
        // Leaving the previous result on the DAC couples its input-dependent
        // preamplifier state into the held capacitor after the next acquisition.
        for (unsigned i=0;i<8;++i) bits[i]=!(128u&(1u<<i));
        sim.advance_to(duration(origin+cfg.track),duration(dt));
        track=false;
        double target=sim.voltage(adc.IN,g).value;
        double capture_supply=sim.voltage(v,g).value;
        sim.advance_to(duration(origin+cfg.track+1e-9),duration(dt));
        double held=sim.voltage(adc.sampler.OUT,g).value, acquisition_error=std::abs(held-target);
        unsigned accepted=0,decisions=0,invalid=0;
        double minimum_margin=cfg.period;
        double minimum=held,maximum=held,absolute=acquisition_error;
        double aperture=origin+cfg.track,ideal_aperture=target,pin_aperture=target,io_aperture=capture_supply,core_aperture=cfg.core;
        decltype(sim.trace) waveform_prefix;
        std::array<double,7> cycle{};
        double kick=0,charge=0,kick_time=0,kick_previous=0;
        auto kickback=[&](double start) {
            if (!kick_time) {kick_time=start;kick_previous=interpolate(start,7)-interpolate(start,10);}
            for(const auto& row:sim.trace) if(row.time>=start) {
                double delta=*row.values[7]-*row.values[10];
                kick=std::max(kick,std::abs(delta));
                if(row.time>kick_time)charge+=(row.time-kick_time)*(kick_previous+delta)/(2*cfg.source_r);
                kick_time=row.time;kick_previous=delta;
            }
        };
        if (cfg.characterize) {
            aperture=crossing(11,origin+cfg.track-.8e-9,origin+cfg.track+.8e-9,false);
            ideal_aperture=interpolate(aperture,10);pin_aperture=interpolate(aperture,7);
            io_aperture=interpolate(aperture,3);core_aperture=interpolate(aperture,4);
            kickback(aperture);
            cycle=integral(origin,origin+cfg.track+1e-9);power_row("track_capture",sample,cycle);
            edges<<sample<<",aperture,-1,"<<aperture_offset[sample]<<','<<aperture-(origin+cfg.track+cfg.clock_slew/2)<<'\n';
        }
        if (cfg.all_nodes)waveform_prefix=sim.trace;
        if (cfg.characterize) { auto anchor=sim.trace.back();sim.discard_trace();sim.trace.push_back(anchor); }
        else sim.discard_trace();
        for (unsigned bit=0;bit<8;++bit) {
            unsigned mask=128u>>bit,trial=accepted|mask;
            for (unsigned i=0;i<8;++i) bits[i]=!(trial&(1u<<i));
            evaluate=false;
            const double base=origin+cfg.track+1e-9+bit*cfg.period;
            // Update DAC at the start of the bit period, settle for the low
            // half, then evaluate during the high half. Reserve 25 ps after
            // the sample for the decision hold-window check.
            const double begin=base+cfg.period*(1-cfg.evaluate_fraction)+cfg.evaluate_skew
                +cfg.evaluate_jitter*((bit+sample)%2 ? 1 : -1), edge=base+cfg.period-25e-12;
            sim.advance_to(duration(begin),duration(dt));
            evaluate=true;
            sim.advance_to(duration(edge),duration(dt));
            double decision=sim.voltage(adc.CORE_DECISION,g).value;
            bit_report << sample << ',' << bit << ',' << trial << ',' << sim.voltage(adc.sampler.OUT,g).value
                << ',' << sim.voltage(adc.ladder.OUT,g).value << ',' << sim.voltage(adc.amplifier.OUTP,g).value
                << ',' << sim.voltage(adc.amplifier.OUTN,g).value << ',' << sim.voltage(adc.comparison.OUTP,g).value
                << ',' << sim.voltage(adc.comparison.OUTN,g).value << ',' << decision << '\n'; bit_report.flush();
            double positive=sim.voltage(adc.comparison.OUTP,g).value, negative=sim.voltage(adc.comparison.OUTN,g).value;
            bool resolved=(decision<.36 && negative>2.31 && positive<.99)
                || (decision>.84 && positive>2.31 && negative<.99);
            if (decision>.84) accepted=trial;
            ++decisions;
            sim.advance_to(duration(base+cfg.period),duration(dt));
            if (cfg.characterize && cfg.physical_clocks) {
                const double commanded=base+cfg.period*(1-cfg.evaluate_fraction);
                double delivered=crossing(12,commanded+evaluation_offset[8*sample+bit],edge,true);
                edges<<sample<<",evaluate,"<<bit<<','<<evaluation_offset[8*sample+bit]<<','
                     <<delivered-(commanded+cfg.clock_slew/2)<<'\n';
            }
            double last_bad=begin;
            for (const auto& row : sim.trace) if (row.time>=begin && row.time<=edge+25e-12) {
                double d=*row.values[2]; bool same=decision>.84 ? d>.84 : d<.36;
                same = same && (decision>.84 ? (*row.values[8]>2.31 && *row.values[9]<.99)
                                           : (*row.values[9]>2.31 && *row.values[8]<.99));
                if (!same && row.time<=edge) last_bad=row.time;
                if (!same && row.time>=edge-25e-12) resolved=false;
            }
            minimum_margin=std::min(minimum_margin,edge-last_bad);
            invalid+=!resolved;
            evaluate=false;
        }
        double current=0,peak=0,energy=0,time=0;
        for (const auto& row : sim.trace) {
            double h=*row.values[0]; minimum=std::min(minimum,h); maximum=std::max(maximum,h);
            absolute=std::max(absolute,std::abs(h-target));
            double i=(*row.values[5]-*row.values[3])/.25;
            double ic=(*row.values[6]-*row.values[4])/10;
            if (time) { double step=row.time-time; current+=i*step; energy+=(i*(*row.values[5])+ic*(*row.values[6]))*step; }
            time=row.time; peak=std::max(peak,i);
        }
        unsigned ideal=unsigned(std::clamp(std::floor(target*256/capture_supply),0.,255.));
        unsigned error=unsigned(std::abs(int(accepted)-int(ideal)));
        bool passed=acquisition_error<=Slice::limits.acquisition && maximum-minimum<=Slice::limits.hold_variation
            && error<=Slice::limits.code_error && invalid==0;
        failures+=!passed;
        out << sample << ',' << target << ',' << acquisition_error << ',' << maximum-minimum << ',' << absolute
            << ',' << accepted << ',' << ideal << ',' << error << ',' << decisions << ','
            << current/(sim.trace.back().time-sim.trace.front().time) << ',' << peak << ',' << energy
            << ',' << minimum_margin << ',' << invalid << ',' << passed << '\n'; out.flush();
        if (cfg.gate) require(passed,"slice conversion contract failed, see "+name+".csv");
        if (cfg.characterize) {
            // Include recovery/idle through the NEXT acquisition origin.
            sim.advance_to(duration(origin+spacing),duration(dt));
            auto rest=integral(origin+cfg.track+1e-9,origin+spacing);power_row("convert_recover",sample,rest);
            for(unsigned i=0;i<5;++i)cycle[i]+=rest[i];
            for(unsigned i=5;i<7;++i)cycle[i]=std::max(cycle[i],rest[i]);
            power_row("full_cycle",sample,cycle);
            kickback(origin+cfg.track+1e-9);
            const double external=std::abs(held-ideal_aperture);
            const unsigned external_ideal=unsigned(std::clamp(std::floor(ideal_aperture*256/io_aperture),0.,255.));
            const unsigned external_error=unsigned(std::abs(int(accepted)-int(external_ideal)));
            const bool external_pass=passed && external<=Slice::limits.acquisition && external_error<=Slice::limits.code_error;
            detail<<sample<<','<<aperture<<','<<aperture_offset[sample]<<','<<ideal_aperture<<','<<pin_aperture<<','<<held
                <<','<<external<<','<<external_error<<','<<io_aperture<<','<<core_aperture<<','<<kick<<','<<charge
                <<','<<cycle[0]<<','<<cycle[1]<<','<<cycle[2]<<','<<cycle[3]/cycle[0]<<','<<cycle[4]/cycle[0]
                <<','<<cycle[5]<<','<<cycle[6]<<','<<external_pass<<'\n';
        }
        if (cfg.all_nodes) {
            std::ofstream solver_record(dir/(name+"_solver.csv"));solver_record<<std::setprecision(17);
            solver_record<<"regular_step_s,edge_step_s,edge_window_s,inserted_edge_points\n"<<cfg.dt<<','
                <<sim.edge_step.value<<','<<sim.edge_window.value<<','<<sim.edge_refinement_points<<'\n';
            auto conversion_trace=std::move(sim.trace);sim.trace=std::move(waveform_prefix);
            for(const auto& row:conversion_trace)if(sim.trace.empty() || row.time>sim.trace.back().time)sim.trace.push_back(row);
            sim.write(dir/(name+"_waveform"));sim.trace=std::move(conversion_trace);
        }
        if (cfg.characterize) { auto anchor=sim.trace.back();sim.discard_trace();sim.trace.push_back(anchor); }
        else sim.discard_trace();
        if (cfg.completed) cfg.completed({double(sample),target,capture_supply,acquisition_error,
            maximum-minimum,absolute,double(accepted),double(ideal),double(error),
            double(decisions),double(invalid),minimum_margin,double(passed)});
        if (cfg.power_cycle && sample+1<levels.size()) {
            clear=true; track=true;
            sim.advance_to(duration(origin+spacing),duration(dt));
            clear=false;
        }
    }
    return failures;
}

static void transition_server(const std::filesystem::path& dir, bool refined) {
    conversion_config cfg;
    cfg.gate=false; cfg.dt=refined ? 5e-12 : 10e-12;
    cfg.next_input=[]() -> std::optional<double> {
        double value;
        if (std::cin>>value) return value;
        require(std::cin.eof(),"invalid transition-server input");
        return std::nullopt;
    };
    cfg.completed=[](const std::vector<double>& result) {
        std::cout << std::setprecision(17);
        for (unsigned i=0;i<result.size();++i) std::cout << (i ? "," : "") << result[i];
        std::cout << '\n' << std::flush;
    };
    std::cout << "sample,input_V,reference_V,acquisition_V,hold_pp_V,absolute_hold_V,code,ideal,code_error,decisions,invalid_decisions,margin_s,pass\n" << std::flush;
    conversion(dir,refined ? "transitions_refined" : "transitions",cfg);
}

static void history_suite(const std::filesystem::path& dir) {
    conversion_config cfg;
    cfg.levels={.8572,.8572,2.05,.8572,.85,.8572,1.65,.8572};
    std::optional<unsigned> code;
    cfg.completed=[&](const std::vector<double>& row) {
        if (std::abs(row[1]-.8572)<1e-6) {
            if (code) require(unsigned(row[6])==*code,"ADC code depends on previous input at an interior voltage");
            code=unsigned(row[6]);
        }
    };
    conversion(dir,"history",cfg);
}

static void routing(const std::filesystem::path& dir) {
    constexpr auto layout=[] { auto p=schematic_layout; p.input_r=20; p.held_r=15; p.held_c=10e-15;
        p.track_r=20; p.track_c=10e-15; p.dac_held_c=.1e-15; return p; }();
    conversion<adc_sar_slice<8,ihp130_profile2,ihp130_passives,nominal_corner,layout>>(dir,"routed_synthetic");
    // External annotation keeps every endpoint distinct until the adapter
    // supplies its topology. Exercise the real SPEF parser for the input net.
    constexpr auto external=[] { auto p=schematic_layout; p.external_routes=true; return p; }();
    simulation sim;
    adc_sar_slice<8,ihp130_profile2,ihp130_passives,nominal_corner,external> adc;
    adc.annotation=[&](const auto& pins) {
        require(sim.root(pins.at("input.driver").id())!=sim.root(pins.at("input.sink").id()),"routed endpoints shorted before extraction");
        std::istringstream text("*SPEF \"IEEE 1481-1998\"\n*C_UNIT 1 PF\n*R_UNIT 1 OHM\n*DESIGN_FLOW \"PIN_CAP NONE\"\n*DELIMITER :\n"
            "*D_NET input 0.01\n*CONN\n*P input I\n*I sampler:IN I\n*CAP\n1 sampler:IN 0.01\n*RES\n1 input sampler:IN 20\n*END\n");
        spef_rc rc(text); rc.bind("input",pins.at("input.driver")); rc.bind("sampler:IN",pins.at("input.sink"));
        rc.apply(sim,adc.Vss);
        for (const auto& [name,pin] : pins) if (name.ends_with(".driver") && name!="input.driver") {
            auto sink=pins.at(name.substr(0,name.size()-7)+".sink"); sink=pin;
        }
    };
    adc.elaborate();
    require(sim.root(adc.IN.id())!=sim.root(adc.sampler.IN.id()),"extracted input resistor bypassed");
}

static void comparator_suite(const std::filesystem::path& dir) {
    std::ofstream out(dir/"comparator.csv"); out << "overdrive_V,resolved,correct,resolution_s\n";
    for (double dv : {-.1,-.01,-.001,-.0001,0.,.0001,.001,.01,.1}) {
        simulation sim; setup(sim);
        electrical g,v; sim.reference(g); v=sim_voltage_source(g,voltage(3.3));
        comparator<ihp130_profile2,ihp130_passives,nominal_corner> cmp;
        cmp.Vdd=v; cmp.Vss=g;
        cmp.PLUS=sim_pwl_voltage_source(g,{{0,0},{.5e-9,1.65+dv/2}});
        cmp.MINUS=sim_pwl_voltage_source(g,{{0,0},{.5e-9,1.65-dv/2}});
        cmp.CLOCK=sim_pwl_voltage_source(g,{{0,0},{2e-9,0},{2.1e-9,3.3}});
        cmp.elaborate(); sim.observe("positive",cmp.OUTP,g); sim.observe("negative",cmp.OUTN,g);
        sim.run(duration(5e-9),duration(5e-12));
        double last_bad=2.1e-9;
        bool resolved=false,correct=false;
        for (const auto& row : sim.trace) if (row.time>=2.1e-9) {
            double p=*row.values[0],n=*row.values[1];
            resolved=(p>2.31 && n<.99) || (n>2.31 && p<.99);
            correct=resolved && ((p>n)==(dv>0));
            if (!correct) last_bad=row.time;
        }
        if (std::abs(dv)>=.001) require(correct,"comparator polarity/resolution failure");
        if (dv==0) require(!resolved,"perfectly symmetric comparator fabricated a valid decision");
        out << dv << ',' << resolved << ',' << correct << ',' << (resolved ? last_bad-2.1e-9 : NAN) << '\n';
    }
}

static void leakage_suite(const std::filesystem::path& dir) {
    constexpr auto passive=[] { auto p=ihp130_passives; p.leakage=1e-8; p.off_coupling=0; return p; }();
    simulation sim; setup(sim); electrical g,v; sim.reference(g); v=sim_voltage_source(g,voltage(3.3));
    sample_hold<ihp130_profile2,passive,nominal_corner> sampler;
    sampler.IN=sim_voltage_source(g,voltage(1.65)); sampler.Vdd=v; sampler.Vss=g;
    sampler.CLOCK=sim_pwl_voltage_source(g,{{0,3.3},{8e-9,3.3},{8.1e-9,0}});
    sampler.elaborate(); sim.run(duration(9e-9),duration(10e-12));
    const double initial=sim.voltage(sampler.OUT,g).value;
    std::ofstream out(dir/"leakage.csv"); out << "hold_s,voltage_V,analytic_V,error_V\n";
    for (double t : {1e-6,10e-6,100e-6}) {
        sim.advance_to(duration(9e-9+t),duration(10e-9));
        double measured=sim.voltage(sampler.OUT,g).value, expected=initial*std::exp(-t*passive.leakage/passive.hold);
        require(std::abs(measured-expected)<50e-6,"hold leakage does not match independent RC solution");
        out << t << ',' << measured << ',' << expected << ',' << measured-expected << '\n';
        sim.discard_trace();
    }
}

static void timing_suite(const std::filesystem::path& dir) {
    for (double ns : {1.,2.,4.,6.}) {
        conversion_config cfg; cfg.period=ns*1e-9; cfg.levels={1.65,1.4}; cfg.gate=false;
        unsigned failed=conversion(dir,"period_"+std::to_string(int(ns)),cfg);
        if (ns>=4) require(!failed,"rated/slower timing regressed");
    }
    conversion_config cfg; cfg.period=.2e-9; cfg.levels={1.4}; cfg.gate=false;
    require(conversion(dir,"period_negative_control",cfg)>0,"unrealistically short clock escaped checks");
}

static void voltage_suite(const std::filesystem::path& dir) {
    unsigned run=0;
    for (double io : {3.,3.3,3.35}) for (double core : {1.08,1.2,1.35}) {
        conversion_config cfg; cfg.io=io; cfg.core=core; cfg.levels={1.15,1.9};
        conversion(dir,"supply_"+std::to_string(run++),cfg);
    }
    conversion_config cfg; cfg.ripple=.025; cfg.io=3.3; cfg.levels={1.15,1.9};
    conversion(dir,"ripple",cfg);
}

static void range_suite(const std::filesystem::path& dir) {
    conversion_config cfg; cfg.levels.clear(); cfg.gate=false;
    for (unsigned i=0;i<=32;++i) cfg.levels.push_back(.85+1.2*i/32);
    unsigned failures=conversion(dir,"range",cfg);
    require(failures==0,"full-range qualification failed in "+std::to_string(failures)+" cases; see range.csv");
}

static void convergence_suite(const std::filesystem::path& dir) {
    conversion_config cfg; cfg.levels={1.65}; cfg.dt=5e-12; cfg.all_nodes=true;
    conversion(dir,"refined",cfg);
    cfg.dt=10e-12; conversion(dir,"coarse",cfg);
    auto read=[](const std::filesystem::path& path) {
        std::ifstream file(path); std::string header,row,word; std::getline(file,header); std::getline(file,row);
        std::stringstream values(row); std::vector<double> result;
        while (std::getline(values,word,',')) result.push_back(std::stod(word));
        require(result.size()==15,"incomplete convergence record"); return result;
    };
    auto coarse=read(dir/"coarse.csv"), fine=read(dir/"refined.csv");
    require(coarse[5]==fine[5],"timestep changed the ADC code");
    for (unsigned metric : {2u,3u,4u}) require(std::abs(coarse[metric]-fine[metric])<.001,"analog metric timestep sensitivity exceeds 1mV");
}

template<corner_parameters Corner, passive_parameters Passive=ihp130_passives>
static void varied_case(const std::filesystem::path& dir,const std::string& name) {
    conversion_config cfg; cfg.levels={1.15,1.65,1.9};
    conversion<adc_sar_slice<8,ihp130_profile2,Passive,Corner>>(dir,name,cfg);
}
static void sensitivity_suite(const std::filesystem::path& dir) {
    // Deliberate generic sensitivities. These names must never become PSP
    // process-corner labels: the actual MOS cards contain many more parameters.
    constexpr corner_parameters slow{125,.85,.85,.025,.025};
    constexpr corner_parameters fast{-40,1.15,1.15,-.025,-.025};
    constexpr corner_parameters skew_np{27,1.15,.85,0,0};
    constexpr corner_parameters skew_pn{27,.85,1.15,0,0};
    varied_case<slow>(dir,"sensitivity_slow_hot");
    varied_case<fast>(dir,"sensitivity_fast_cold");
    varied_case<skew_np>(dir,"sensitivity_nfast_pslow");
    varied_case<skew_pn>(dir,"sensitivity_nslow_pfast");
    constexpr auto cap_hi=[] { auto p=ihp130_passives; p.cap_scale=1.1; p.resistor_scale=1.1; return p; }();
    constexpr auto cap_lo=[] { auto p=ihp130_passives; p.cap_scale=.9; p.resistor_scale=.9; return p; }();
    varied_case<nominal_corner,cap_hi>(dir,"sensitivity_passive_high");
    varied_case<nominal_corner,cap_lo>(dir,"sensitivity_passive_low");
}

static void dynamic_suite(const std::filesystem::path& dir) {
    conversion_config cfg; cfg.levels.assign(64,1.45); cfg.gate=false;
    cfg.sine_frequency=5/(64*(cfg.track+8*cfg.period+10e-9));
    unsigned failures=conversion(dir,"dynamic",cfg);
    require(failures==0,"dynamic conversion qualification failures: "+std::to_string(failures));
}
static void settling_suite(const std::filesystem::path& dir) {
    for (double fraction : {.25,.5,.75}) {
        conversion_config cfg; cfg.evaluate_fraction=fraction; cfg.levels={.85,.8875,.925,1.4}; cfg.gate=false;
        unsigned failed=conversion(dir,"dac_settle_"+std::to_string(int(4000*(1-fraction)))+"ps",cfg);
        if (fraction==.5) require(failed==0,"nominal half-cycle DAC/evaluation allocation failed");
        if (fraction==.75) require(failed>0,"insufficient DAC settling negative control not detected");
    }
}
static void clock_suite(const std::filesystem::path& dir) {
    for (double drive : {50.,500.,1000.}) {
        conversion_config cfg; cfg.control_r=drive; cfg.control_c=100e-15;
        cfg.evaluate_skew=50e-12; cfg.evaluate_jitter=25e-12; cfg.levels={1.15,1.9};
        conversion(dir,"clock_load_"+std::to_string(int(drive)),cfg);
    }
}
static void startup_suite(const std::filesystem::path& dir) {
    for (double warmup : {2e-9,5e-9,20e-9}) {
        conversion_config cfg; cfg.warmup=warmup; cfg.levels={1.15,1.9};
        conversion(dir,"startup_"+std::to_string(int(warmup*1e9))+"ns",cfg);
    }
    conversion_config cfg; cfg.power_cycle=true; cfg.levels={1.15,1.9,1.4};
    conversion(dir,"power_cycle",cfg);
}
constexpr passive_parameters mismatch_draw(unsigned seed) {
    auto p=ihp130_passives;
    auto draw=[&]() { seed=1664525u*seed+1013904223u; return .02*(double(seed)/4294967296.-.5); };
    for (unsigned i=0;i<8;++i) { p.r_mismatch[i]=draw(); p.r2_mismatch[i]=draw(); }
    p.termination_mismatch=draw(); return p;
}
template<unsigned Seed> static void mismatch_case(const std::filesystem::path& dir) {
    conversion_config cfg; cfg.levels={1.15,1.9};
    conversion<adc_sar_slice<8,ihp130_profile2,mismatch_draw(Seed)>>(dir,"mismatch_"+std::to_string(Seed),cfg);
}
static void mismatch_suite(const std::filesystem::path& dir) {
    // Eight deterministic independent draws, uniform +/-1% per R/2R element.
    // Synthetic robustness experiment, NOT foundry-calibrated mismatch/yield.
    mismatch_case<1>(dir); mismatch_case<2>(dir); mismatch_case<3>(dir); mismatch_case<4>(dir);
    mismatch_case<5>(dir); mismatch_case<6>(dir); mismatch_case<7>(dir); mismatch_case<8>(dir);
}

#include "characterization.h"

int main(int argc,char** argv) {
    try {
        require(argc==3,"usage: adc_sar_tests SUITE RESULT_DIRECTORY");
        std::string suite=argv[1]; std::filesystem::path dir=argv[2]; std::filesystem::create_directories(dir);
        if (suite=="parameters") parameters();
        else if (suite=="acquisition") acquisition_suite(dir);
        else if (suite=="dac") dac_suite(dir);
        else if (suite=="conversion") conversion(dir,"conversion");
        else if (suite=="routing") routing(dir);
        else if (suite=="comparator") comparator_suite(dir);
        else if (suite=="leakage") leakage_suite(dir);
        else if (suite=="timing") timing_suite(dir);
        else if (suite=="voltage") voltage_suite(dir);
        else if (suite=="range") range_suite(dir);
        else if (suite=="convergence") convergence_suite(dir);
        else if (suite=="sensitivity") sensitivity_suite(dir);
        else if (suite=="dynamic") dynamic_suite(dir);
        else if (suite=="settling") settling_suite(dir);
        else if (suite=="clock") clock_suite(dir);
        else if (suite=="startup") startup_suite(dir);
        else if (suite=="mismatch") mismatch_suite(dir);
        else if (suite=="adaptive_input") adaptive_input_suite(dir);
        else if (suite=="history") history_suite(dir);
        else if (suite=="transition_server" || suite=="transition_server_refined") transition_server(dir,suite=="transition_server_refined");
        else if (suite.rfind("wave_",0)==0) {
            conversion_config cfg;cfg.levels={1.65};cfg.all_nodes=true;
            cfg.method=suite.find("bdf")!=std::string::npos ? integration_method::bdf2 : integration_method::backward_euler;
            cfg.dt=std::stod(suite.substr(suite.rfind('_')+1))*1e-12;
            if(cfg.method==integration_method::bdf2){cfg.edge_step=cfg.dt/312.5;cfg.edge_window=5e-12;}
            conversion(dir,suite,cfg);
        }
        else if (suite.rfind("characterize_",0)==0) characterization_suite(dir,suite.substr(13));
        else if (suite=="probe") { conversion_config cfg; cfg.levels={.85,.8875,.925}; cfg.gate=false; conversion(dir,"probe",cfg); }
        else throw std::runtime_error("unknown suite");
        std::cout << "PASS " << suite << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
