#include "adc_sar_slice.h"
#include "../process_export.h"
#include <fstream>
#include <iostream>

int main(int argc,char** argv) {
 try {
    if(argc!=2)throw std::runtime_error("usage: adc_sar_ihp_export output-directory");
    std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    analog::simulation sim;analog::adc_sar::ihpsg13g2::adc_sar_slice adc;adc.elaborate();
    std::ofstream spice(out/"generic_binding.spice");analog::adc_sar::write_process_subcircuit(spice,adc);
    std::map<std::size_t,std::string> names;
    auto pin=[&](const std::string& name,const analog::electrical& wire){
        if(!names.emplace(sim.root(wire.id()),name).second)throw std::runtime_error("aliased physical ports");};
    pin("IN",adc.IN);pin("TRACK",adc.TRACK);pin("EVALUATE",adc.EVALUATE);pin("CLEAR",adc.CLEAR);
    pin("DECISION",adc.CORE_DECISION);pin("VDD",adc.Vdd);pin("VCORE",adc.Vlogic);pin("VSS",adc.Vss);
    for(unsigned i=0;i<8;++i)pin("BIT_N"+std::to_string(i),adc.BIT_N[i]);
    auto node=[&](std::size_t id){id=sim.root(id);return names.contains(id)?names.at(id):"n"+std::to_string(id);};
    std::map<std::size_t,analog::adc_sar::mos_binding> mos;
    for(auto b:adc.physical_bindings())mos.emplace(b.element,b);
    std::map<std::size_t,analog::adc_sar::mim_binding> mim;std::set<std::size_t> access;
    for(auto b:adc.sampler.mim_bindings){mim.emplace(b.capacitor,b);access.insert(b.resistor);}
    std::ofstream json(out/"circuit.json");json<<std::setprecision(17);
    json<<"{\"schema\":1,\"top\":\"adc_sar_slice\",\"bits\":8,\"ports\":[";
    bool comma=false;for(const auto& [id,name]:names){if(comma)json<<',';json<<std::quoted(name);comma=true;}
    json<<"],\"observations\":{\"held\":"<<std::quoted(node(adc.sampler.OUT.id()))
        <<",\"dac\":"<<std::quoted(node(adc.ladder.OUT.id()))
        <<",\"positive\":"<<std::quoted(node(adc.comparison.OUTP.id()))
        <<",\"negative\":"<<std::quoted(node(adc.comparison.OUTN.id()))<<"},\"devices\":[";
    comma=false;
    for(std::size_t i=adc.first_element;i<adc.last_element;++i){
        if(access.contains(i))continue;
        const auto& e=sim.elements[i];if(comma)json<<',';comma=true;
        if(e.name.empty())throw std::runtime_error("unnamed SAR model element D"+std::to_string(i));
        json<<"\n{\"id\":"<<i<<",\"name\":\"D"<<i<<"\",\"hierarchical_name\":"
            <<std::quoted(e.name)<<',';
        if(mos.contains(i)){
            const auto b=mos.at(i);const auto p=b.parameters;
            json<<"\"kind\":\"mos\",\"model\":\"sg13_"<<(p.core?"lv":"hv")<<'_'<<(e.kind==analog::law::pmos?"p":"n")<<"mos\","
              <<"\"connections\":{\"D\":"<<std::quoted(node(e.a))<<",\"G\":"<<std::quoted(node(e.gate))
              <<",\"S\":"<<std::quoted(node(e.b))<<",\"B\":"<<std::quoted(node(b.body))<<"},"
              <<"\"width_um\":"<<p.width*1e6<<",\"length_um\":"<<p.length*1e6<<",\"fingers\":"<<p.fingers;
        }else{
            auto a=e.a,b=e.b;double value=e.kind==analog::law::capacitor?e.c:e.r;
            if(mim.contains(i)){a=mim.at(i).a;b=mim.at(i).b;}
            const bool cap=e.kind==analog::law::capacitor;
            if(!cap && e.kind!=analog::law::resistor)throw std::runtime_error("unmapped physical element");
            json<<"\"kind\":\""<<(cap?"capacitor":"resistor")<<"\",\"value\":"<<value
              <<",\"connections\":{\"PLUS\":"<<std::quoted(node(a))<<",\"MINUS\":"<<std::quoted(node(b));
            if(!cap)json<<",\"SUB\":\"VSS\"";
            json<<'}';
        }
        json<<'}';
    }
    json<<"\n]}\n";
    if(!json||!spice)throw std::runtime_error("export write failed");
    std::cout<<"Exported one slice with "<<mos.size()<<" explicit MOS bindings\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
