// Native Analog++ process simulation; its own electrical comparator drives SAR decisions.
#include <basic/IHPSG13G2/mos.hpp>
#include <behavior_devices.hpp>
#include <sparse.hpp>
#include <singleton_linear.hpp>
#include <fstream>
#include <iostream>
namespace b=analog::behavior;
class host_driver : public b::device {
    std::size_t y_, high_, low_;
public:
    analog::electrical Y, HIGH, LOW;
    double resistance = 50, before = 1, after = 1, edge = 50e-12;
    double changed = -1;
    b::capabilities features() const override { return {.breakpoints=true}; }
    void bind(b::binding& binding) override {
        y_=binding.node(Y); high_=binding.node(HIGH); low_=binding.node(LOW);
        if (!(resistance > 0)) throw std::runtime_error("invalid driver resistance");
    }
    double value(double time) const {
        if (changed < 0 || time >= changed+edge) return after;
        if (time <= changed) return before;
        return before+(after-before)*(time-changed)/edge;
    }
    void set(double time, double value) {
        if (!std::isfinite(time) || time < 0 || (value != 0 && value != 1))
            throw std::runtime_error("invalid digital driver update");
        // A repeated digital value does not create an electrical edge. In
        // particular it must not insert a femtosecond timestep during hold.
        if (value == after) return;
        before=this->value(time); after=value; changed=time;
    }
    double next_breakpoint(double time) const override {
        return changed >= 0 && time < changed+edge ? changed+edge : std::numeric_limits<double>::infinity();
    }
    void evaluate(const b::evaluation& state,b::equations& out) override {
        const double high=value(state.time)/resistance, low=(1-value(state.time))/resistance;
        out.branch(y_,high_,(state.value(y_)-state.value(high_))*high,0,high,0);
        out.branch(y_,low_,(state.value(y_)-state.value(low_))*low,0,low,0);
    }
};

// Observe only accepted electrical state. Rejected Newton trials cannot drive
// the controller or leak into the waveform.
class accepted_probe : public b::stateful_device {
    std::vector<std::size_t> indices;
    std::vector<double> pending;
public:
    std::vector<analog::electrical> wires;
    std::vector<double> values;
    b::capabilities features() const override { return {.rollback=true}; }
    void bind(b::binding& binding) override {
        for (const auto& wire:wires) indices.push_back(binding.node(wire));
        pending.resize(indices.size());values.resize(indices.size());
    }
    void begin_step(double,double) override {}
    void evaluate(const b::evaluation& state,b::equations&) override {
        for (std::size_t i=0;i<indices.size();++i) pending[i]=state.value(indices[i]);
    }
    void accept() noexcept override { std::copy(pending.begin(),pending.end(),values.begin()); }
    void reject() noexcept override {}
};

class compact : public analog::osdi::device {
public:
    std::vector<analog::electrical> pins;
protected:
    std::vector<analog::electrical> terminals() const override{return pins;}
};

class reporting_backend : public b::backend_solver {
    bool first=true;
public:
    using b::backend_solver::backend_solver;
    std::vector<double> solve(std::size_t n,const std::vector<analog::matrix_entry>& a,const std::vector<double>& rhs) override {
        if(first){std::cout<<"Reduced linear unknowns: "<<n<<std::endl;first=false;}
        return b::backend_solver::solve(n,a,rhs);
    }
};

struct event {double time;int kind;unsigned sample,bit;};
class sar_steps : public b::fixed_step {
    accepted_probe& probe;
    std::array<host_driver*,11>& drivers;
    std::vector<event> events;
    std::vector<double> levels;
    std::size_t cursor=0,accepted_points=0;
    unsigned code=0,trial=128,invalid=0,current=0;
    double step,last_step=1e-12,held_min=0,held_max=0,acquisition=0;
    bool holding=false;
    std::ofstream trace,control,results,decisions;
    void drive(unsigned i,double t,bool value){
        if(value==drivers[i]->after)return;
        drivers[i]->set(t,value);control<<t<<','<<i<<','<<int(value)<<'\n';
    }
    void bits(double t){for(unsigned i=0;i<8;++i)drive(i+3,t,!(trial&(1u<<i)));}
public:
    sar_steps(accepted_probe& p,std::array<host_driver*,11>& d,const std::filesystem::path& out,
              const std::vector<std::string>& names,std::vector<double> input,double dt)
      :b::fixed_step(dt,1e-18),probe(p),drivers(d),levels(std::move(input)),step(dt),
       trace(out/"trace.csv"),control(out/"controls.csv"),results(out/"results.csv"),decisions(out/"decisions.csv"){
        trace<<std::setprecision(17)<<"time_s";for(auto& n:names)trace<<','<<n;trace<<'\n';
        control<<std::setprecision(17)<<"time_s,driver,value\n";
        for(unsigned i=0;i<11;++i)control<<0<<','<<i<<','<<drivers[i]->after<<'\n';
        results<<std::setprecision(17)<<"sample,input_V,code,ideal,code_error,acquisition_V,hold_pp_V,invalid_decisions,pass\n";
        decisions<<std::setprecision(17)<<"sample,bit,time_s,trial,decision_V,core_V,valid\n";
        for(unsigned s=0;s<levels.size();++s){
            const double origin=20e-9+50e-9*s;
            events.push_back({origin,0,s,0});events.push_back({origin+8e-9,1,s,0});
            events.push_back({origin+8e-9+25e-12,2,s,0});
            for(unsigned bit=0;bit<8;++bit){double base=origin+9e-9+4e-9*bit;
                events.push_back({base,3,s,bit});events.push_back({base+2e-9,4,s,bit});
                events.push_back({base+4e-9-25e-12,5,s,bit});}
            events.push_back({origin+41e-9,6,s,0});
        }
    }
    double proposed(double time) const override{
        double dt=time<19e-9?std::min(100e-12,step*10):step;
        // The 1 ns supply ramp is an electrical edge too. Coarse warmup
        // steps shifted the startup decision waveform by >100 mV even in
        // a nominal "5 ps" run. Resolve the ramp and its first settling ns;
        // retain larger steps only in the subsequent quiet warmup interval.
        if(time<2e-9)dt=std::min({dt,step,5e-12,2e-9-time});
        dt=std::min(dt,2*last_step);
        return cursor<events.size()?std::min(dt,events[cursor].time-time):dt;
    }
    void accepted(double time,double accepted_step) override{
        last_step=accepted_step;
        trace<<time;for(auto v:probe.values)trace<<','<<v;trace<<'\n';
        if(++accepted_points%100==0){trace.flush();std::cout<<"Accepted "<<accepted_points<<" points, t="<<time<<" s\n"<<std::flush;}
        if(holding){held_min=std::min(held_min,probe.values[0]);held_max=std::max(held_max,probe.values[0]);}
        if(cursor==events.size()||std::abs(time-events[cursor].time)>1e-18)return;
        auto e=events[cursor++];current=e.sample;
        switch(e.kind){
        case 0: code=invalid=0;trial=128;holding=false;drive(0,time,true);drive(1,time,false);drive(2,time,false);bits(time);break;
        case 1: drive(0,time,false);break;
        case 2: holding=true;held_min=held_max=probe.values[0];acquisition=std::abs(probe.values[0]-levels[current]);break;
        case 3: drive(1,time,false);trial=code|(1u<<(7-e.bit));bits(time);break;
        case 4: drive(1,time,true);break;
        case 5: {
            const double v=probe.values[8],core=probe.values[7],io=probe.values[6];
            bool valid=(v<.3*core&&probe.values[3]>.7*io&&probe.values[2]<.3*io)
                ||(v>.7*core&&probe.values[2]>.7*io&&probe.values[3]<.3*io);
            invalid+=!valid;decisions<<current<<','<<e.bit<<','<<time<<','<<trial<<','<<v<<','<<core<<','<<valid<<'\n';
            if(v>.5*core)code=trial;break;}
        case 6: {
            drive(1,time,false);holding=false;int ideal=std::clamp(int(levels[current]/3.3*256),0,255);
            auto error=std::abs(int(code)-ideal);double variation=held_max-held_min;
            bool pass=acquisition<=.020&&variation<=.020&&error<=2&&!invalid;
            results<<current<<','<<levels[current]<<','<<code<<','<<ideal<<','<<error<<','<<acquisition<<','<<variation<<','<<invalid<<','<<pass<<'\n';results.flush();break;}
        }
    }
};

int main(int argc,char** argv){
 try{
    unsigned degree=32;double stop_override=0;bool backward_euler=false;
    while(argc>1&&std::string(argv[1]).starts_with("--")){
        const std::string option=argv[1];
        if(option=="--backward-euler"){backward_euler=true;--argc;++argv;}
        else if(argc>2&&(option=="--degree"||option=="--stop")){
            if(option=="--degree")degree=std::stoul(argv[2]);else{
                stop_override=std::stod(argv[2]);if(!std::isfinite(stop_override)||stop_override<=0)throw std::runtime_error("invalid diagnostic stop");
            }
            argc-=2;argv+=2;
        }else throw std::runtime_error("unsupported native option");
    }
    if(argc<5)throw std::runtime_error("usage: native circuit.txt output-directory step_s input_V [...]");
    std::ifstream input(argv[1]);if(!input)throw std::runtime_error("cannot read process circuit");
    std::filesystem::path out=argv[2];std::filesystem::create_directories(out);double dt=std::stod(argv[3]);
    if(!(dt>0&&dt<=100e-12))throw std::runtime_error("invalid process timestep");
    std::vector<double> levels;for(int i=4;i<argc;++i)levels.push_back(std::stod(argv[i]));
    for(double level:levels)if(!std::isfinite(level)||level<0||level>3.3)throw std::runtime_error("input level outside finite 0--3.3 V range");
    analog::simulation context;analog::use_sparse_solver(context,analog::sparse_strategy::certified_lu);
    std::map<std::string,analog::electrical> nodes;
    auto wire=[&](const std::string& name)->analog::electrical&{return nodes.try_emplace(name).first->second;};
    context.reference(wire("0"));wire("VSS")=wire("0");
    std::vector<std::unique_ptr<b::device>> devices;
    auto resistor=[&](std::string a,std::string c,double value){auto p=std::make_unique<b::resistor>();p->P=wire(a);p->N=wire(c);p->resistance.value=value;devices.push_back(std::move(p));};
    auto capacitor=[&](std::string a,std::string c,double value){auto p=std::make_unique<b::capacitor>();p->P=wire(a);p->N=wire(c);p->capacitance.value=value;devices.push_back(std::move(p));};
    auto source=[&](std::string a,std::vector<std::pair<double,double>> points){auto p=std::make_unique<b::sim_pwl_voltage_source>();p->P=wire(a);p->N=wire("0");p->points=std::move(points);devices.push_back(std::move(p));};
    accepted_probe probe;std::vector<std::string> names;double temperature=27;
    std::vector<std::pair<std::string,std::string>> extra_probes;
    std::map<std::string,std::pair<std::shared_ptr<analog::osdi::model>,std::map<std::string,double>>> bundles;
    std::map<std::string,std::shared_ptr<analog::osdi::library>> libraries;
    const std::array<std::string,6> stress_names={"vgs_max","vgd_max","vgb_max","vds_max","vdb_max","vsb_max"};
    const std::array<std::pair<unsigned,unsigned>,6> stress_pairs={{{1,2},{1,0},{1,3},{0,2},{0,3},{2,3}}};
    std::map<std::string,std::array<double,6>> stress_limits;
    struct stress_device {std::string name;std::array<analog::electrical,4> pins;std::array<double,6> limits;};
    std::vector<stress_device> stress_devices;
    std::string line;
    while(std::getline(input,line)){
        std::istringstream row(line);std::string kind,name,a,c;if(!(row>>kind)||kind=="#")continue;
        if(kind=="TEMP"){row>>temperature;continue;}
        if(kind=="PROBE"){row>>name>>a;extra_probes.emplace_back(name,a);continue;}
        if(kind=="OBS"){row>>name>>a;names.push_back(name);probe.wires.push_back(wire(a));continue;}
        row>>name>>a>>c;
        if(kind=="R"||kind=="C"){double value;row>>value;if(kind=="R")resistor(a,c,value);else capacitor(a,c,value);}
        else if(kind=="M"){
            std::string s,body,path;row>>s>>body>>std::quoted(path);
            if(!bundles.contains(path)){
                std::ifstream f(path);std::string tag,binary,module,key;double value;
                f>>tag>>std::quoted(binary);if(tag!="binary")throw std::runtime_error("missing OSDI binary");
                f>>tag>>module;if(tag!="module")throw std::runtime_error("missing OSDI module");
                std::map<std::string,double> model,instance;
                while(f>>tag>>key>>value){if(tag!="model"&&tag!="instance")throw std::runtime_error("bad process bundle");
                    if(!(tag=="model"?model:instance).emplace(key,value).second)throw std::runtime_error("duplicate process parameter");}
                if(module=="psp103va"){
                    std::array<double,6> limits;
                    for(unsigned i=0;i<6;++i)limits[i]=model.at(stress_names[i]);
                    stress_limits[path]=limits;
                }
                auto id=binary+":"+module;if(!libraries.contains(id))libraries[id]=std::make_shared<analog::osdi::library>(binary,module);
                bundles[path]={std::make_shared<analog::osdi::model>(libraries[id],model),instance};
            }
            auto p=std::make_unique<compact>();p->pins={wire(a),wire(c),wire(s),wire(body)};
            p->process=bundles.at(path).first;p->parameters=bundles.at(path).second;p->temperature_K=temperature+273.15;
            if(stress_limits.contains(path))stress_devices.push_back({name,{wire(a),wire(c),wire(s),wire(body)},stress_limits.at(path)});
            devices.push_back(std::move(p));
        }else throw std::runtime_error("unknown lowered process device");
    }
    if(names!=std::vector<std::string>{"held","dac","positive","negative"})throw std::runtime_error("incorrect observation contract");
    for(auto name:{"source","IN","VDD","VCORE","DECISION","io_source","core_source"}){names.emplace_back(name);probe.wires.push_back(wire(name));}
    for(auto [name,node]:extra_probes){names.push_back(name);probe.wires.push_back(wire(node));}
    source("io_source",{{0,0},{1e-9,3.3}});resistor("io_source","VDD",.25);capacitor("VDD","0",20e-12);
    source("core_source",{{0,0},{1e-9,1.2}});resistor("core_source","VCORE",10);capacitor("VCORE","0",20e-12);
    std::vector<std::pair<double,double>> points{{0,levels[0]}};
    for(unsigned i=1;i<levels.size();++i){double t=20e-9+50e-9*i;points.emplace_back(t,levels[i-1]);points.emplace_back(t+100e-12,levels[i]);}
    source("source",points);resistor("source","IN",50);capacitor("DECISION","0",20e-15);
    std::array<host_driver*,11> drivers{};
    for(unsigned i=0;i<11;++i){auto p=std::make_unique<host_driver>();std::string name=i==0?"TRACK":i==1?"EVALUATE":i==2?"CLEAR":"BIT_N"+std::to_string(i-3);
        p->Y=wire(name);p->HIGH=wire("VDD");p->LOW=wire("0");p->before=p->after=(i==0||i==2||(i>=3&&i!=10));
        drivers[i]=p.get();devices.push_back(std::move(p));}
    for(auto& d:devices)d->elaborate();probe.elaborate();b::network net(context);net.compile();
    std::cout<<"Native process unknowns: "<<net.size()<<std::endl;
    b::coupled_solver solver;
    auto linear=std::make_shared<b::algebraic_elimination_solver>(std::make_shared<reporting_backend>(context.linear_backend),degree);
    solver.linear=linear;
    if(backward_euler)solver.integrator=std::make_shared<b::backward_euler>();
    sar_steps steps(probe,drivers,out,names,levels,dt);
    const double stop=stop_override>0?stop_override:20e-9+50e-9*levels.size();
    auto result=solver.run(net,stop,steps,std::vector<double>(net.size(),0));
    // Report the actual loaded model-card thresholds, even when SWSOA=0.
    // These are model diagnostics; pnr.md distinguishes them from the PDK's
    // device documentation and from a qualified foundry reliability limit.
    std::ofstream stress(out/"terminal_stress.csv");stress<<std::setprecision(17)<<"device,quantity,peak_V,model_limit_V,time_s,exceeded\n";
    for(const auto& d:stress_devices){
        std::array<std::size_t,4> indices;for(unsigned i=0;i<4;++i)indices[i]=net.index(d.pins[i]);
        std::array<double,6> peak{},time{};
        for(const auto& point:result){
            auto v=[&](unsigned i){return indices[i]==b::ground?0:point.solution.at(indices[i]);};
            for(unsigned i=0;i<6;++i){auto [a,b]=stress_pairs[i];double value=std::abs(v(a)-v(b));
                if(value>peak[i]){peak[i]=value;time[i]=point.time;}}
        }
        for(unsigned i=0;i<6;++i)stress<<d.name<<','<<stress_names[i]<<','<<peak[i]<<','<<d.limits[i]<<','<<time[i]<<','<<(peak[i]>d.limits[i])<<'\n';
    }
    std::ofstream stats(out/"native.json");stats<<std::setprecision(17)<<"{\"engine\":\"Analog++ OSDI\",\"method\":\""<<(backward_euler?"backward_euler":"bdf2")<<"\",\"unknowns\":"<<net.size()<<",\"points\":"<<result.size()<<",\"maximum_step_s\":"<<std::min(100e-12,dt*10)<<",\"conversion_maximum_step_s\":"<<dt<<",\"startup_edge_maximum_step_s\":"<<std::min(5e-12,dt)<<",\"startup_edge_ends_s\":2e-9,\"startup_ends_s\":19e-9,\"elimination_degree\":"<<degree<<",\"conditioning_fallbacks\":"<<linear->conditioning_fallbacks<<",\"stop_s\":"<<stop<<",\"ngspice_invoked\":false,\"complete\":true,\"conversion_complete\":"<<(stop>=20e-9+50e-9*levels.size()?"true":"false")<<"}\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
 return 0;
}
