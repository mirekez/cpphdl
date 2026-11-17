#include "../adc_sar_slice.h"
#ifdef ADC_USE_EIGEN
#include <sparse.hpp>
#endif
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <tuple>
using namespace analog;
using namespace analog::adc_sar;

static void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
static double mean(const std::vector<double>& v) { return std::accumulate(v.begin(),v.end(),0.)/v.size(); }
static double variance(const std::vector<double>& v) {
    double m=mean(v),s=0;for (auto x:v)s+=(x-m)*(x-m);return s/(v.size()-1);
}
static double covariance(const std::vector<double>& v,std::size_t lag) {
    double m=mean(v),s=0;for (std::size_t i=lag;i<v.size();++i)s+=(v[i]-m)*(v[i-lag]-m);
    return s/(v.size()-lag);
}
static void rejects(const std::function<void()>& f) {
    bool bad=false;try {f();}catch(const std::invalid_argument&){bad=true;}
    require(bad,"invalid noise configuration was accepted");
}

static void model(const std::filesystem::path& dir) {
    auto r=adc_sar_slice<>::sample_noise(131072,50e-9,7);
    auto same=adc_sar_slice<>::sample_noise(131072,50e-9,7);
    auto other=adc_sar_slice<>::sample_noise(131072,50e-9,19);
    require(r.total==same.total && r.total!=other.total,"noise seed reproducibility failed");
    auto p=noise_parameters{};p.include_ktc=false;
    auto zero=make_noise_record(p,300.15,1e-12,256,50e-9,7);
    require(std::all_of(zero.total.begin(),zero.total.end(),[](double v){return v==0;}),"noise-off record is nonzero");
    rejects([&]{make_noise_record(p,0,1e-12,256,50e-9,7);});
    rejects([&]{make_noise_record(p,300.15,0,256,50e-9,7);});
    rejects([&]{make_noise_record(p,300.15,1e-12,255,50e-9,7);});
    rejects([&]{auto q=p;q.white_psd_V2_Hz=NAN;make_noise_record(q,300.15,1e-12,256,50e-9,7);});
    rejects([&]{auto q=p;q.flicker_V2=-1;make_noise_record(q,300.15,1e-12,256,50e-9,7);});
    rejects([&]{auto q=p;q.flicker_V2=1e-10;make_noise_record(q,300.15,1e-12,131072,50e-9,7);});
    rejects([&]{auto q=p;q.flicker_V2=1e-10;q.reference_temperature_K=300.15;make_noise_record(q,300.15,1e-12,256,50e-9,7);});
    rejects([&]{auto q=p;q.white_psd_V2_Hz=1e-16;q.high_Hz=11e6;q.reference_temperature_K=300.15;make_noise_record(q,300.15,1e-12,131072,50e-9,7);});
    std::ofstream stats(dir/"noise_statistics.csv");stats<<std::setprecision(17);
    stats<<"kind,temperature_K,capacitance_F,expected_rms_V,measured_rms_V,relative_rms_error,mean_V,lag1_correlation,pass\n";
    for (double temperature:{233.15,300.15,398.15}) for (double cap:{.5e-12,1e-12,2e-12}) {
        auto record=make_noise_record({},temperature,cap,131072,50e-9,7);
        double expected=std::sqrt(1.380649e-23*temperature/cap),measured=std::sqrt(variance(record.total));
        double error=measured/expected-1,correlation=covariance(record.total,1)/variance(record.total);
        require(std::abs(error)<.015 && std::abs(mean(record.total))/expected<.015 && std::abs(correlation)<.015,
                "kT/C amplitude, mean or independence calibration failed");
        stats<<"ktc,"<<temperature<<','<<cap<<','<<expected<<','<<measured<<','<<error<<','
             <<mean(record.total)<<','<<correlation<<",1\n";
    }
    // Parameter propagation into the slice factory, including PDK MIM TC and layout C.
    constexpr auto hot=[] { auto c=nominal_corner;c.temperature=125;return c; }();
    constexpr auto layout=[] { auto l=schematic_layout;l.held_c=.2e-12;return l; }();
    using Hot=adc_sar_slice<8,ihp130_profile2,ihp130_passives,hot,layout>;
    auto hot_record=Hot::sample_noise(256,50e-9,1);
    require(std::abs(hot_record.capacitance_F-(1e-12*(1+3.6e-6*98+2e-9*98*98)+.2e-12))<1e-27,
            "slice C/temperature/layout noise binding lost");
    constexpr auto extracted=[] { auto l=schematic_layout;l.external_routes=true;return l; }();
    using Extracted=adc_sar_slice<8,ihp130_profile2,ihp130_passives,nominal_corner,extracted>;
    rejects([] { Extracted::sample_noise(256,50e-9,1); });
    auto extracted_profile=noise_parameters{};extracted_profile.capacitance_F=1.3e-12;
    require(Extracted::sample_noise(256,50e-9,1,extracted_profile).capacitance_F==1.3e-12,"extracted effective C ignored");
    p.include_ktc=true;p.white_psd_V2_Hz=4e-16;p.flicker_V2=4e-10;p.reference_temperature_K=300.15;
    rejects([&] { Hot::sample_noise(131072,50e-9,1,p); });
    auto colored=make_noise_record(p,300.15,1e-12,131072,50e-9,7);
    for (const auto& [name,values,expected_variance]:std::vector<std::tuple<std::string,const std::vector<double>*,double>>{
            {"white",&colored.white,4e-16*(8e6-1000)},
            {"flicker",&colored.flicker,4e-10*std::log(8000.)},
            {"combined",&colored.total,1.380649e-23*300.15/1e-12+4e-16*(8e6-1000)+4e-10*std::log(8000.)}}) {
        double measured=std::sqrt(variance(*values)),expected=std::sqrt(expected_variance);
        require(std::abs(measured/expected-1)<.12,"band-integrated spectral noise variance failed");
        stats<<name<<",300.15,1e-12,"<<expected<<','<<measured<<','<<measured/expected-1<<','
             <<mean(*values)<<','<<covariance(*values,1)/variance(*values)<<",1\n";
    }
    std::ofstream corr(dir/"noise_correlation.csv");corr<<std::setprecision(17);
    corr<<"lag,expected_covariance_V2,measured_covariance_V2,error_over_total_variance,pass\n";
    for (unsigned lag:{1u,10u,100u}) {
        // Independent log-frequency quadrature of integral S(f) cos(2*pi*f*t) df.
        const double tau=lag*50e-9,ratio=std::log(8000.)/16384;
        double expected=0;
        for (unsigned i=0;i<16384;++i) {
            double lo=1000*std::exp(i*ratio),hi=1000*std::exp((i+1)*ratio),f=std::sqrt(lo*hi);
            expected+=(4e-16*(hi-lo)+4e-10*ratio)*std::cos(2*std::acos(-1.)*f*tau);
        }
        double measured=covariance(colored.total,lag),error=(measured-expected)/variance(colored.total);
        require(std::abs(error)<.08,"noise spectrum/correlation lost or flicker became IID");
        corr<<lag<<','<<expected<<','<<measured<<','<<error<<",1\n";
    }
    std::ofstream samples(dir/"noise_samples.csv");samples<<std::setprecision(17);
    samples<<"seed,sample,noise_V,expected_sigma_V\n";
    for (unsigned seed:{7u,19u,31u,43u}) {
        auto record=adc_sar_slice<>::sample_noise(256,50e-9,seed);
        for (unsigned i=0;i<256;++i) samples<<seed<<','<<i<<','<<record.total[i]<<','<<std::sqrt(record.sampling_variance_V2)<<'\n';
    }
}

static void calibrated_profile(const std::filesystem::path& dir) {
    noise_parameters p;std::ifstream in(dir/"calibration_input.txt");
    require(bool(in>>p.include_ktc>>p.white_psd_V2_Hz>>p.flicker_V2>>p.low_Hz>>p.high_Hz>>p.reference_temperature_K),
            "malformed calibration fixture");
    const auto r=adc_sar_slice<>::sample_noise(131072,50e-9,7,p);
    const double expected=std::sqrt(r.sampling_variance_V2+r.white_variance_V2+r.flicker_variance_V2);
    const double measured=std::sqrt(variance(r.total));
    require(std::abs(measured/expected-1)<.12,"imported calibrated PSD failed native RMS validation");
    std::ofstream out(dir/"calibrated_noise_statistics.csv");out<<std::setprecision(17);
    out<<"expected_rms_V,measured_rms_V,relative_rms_error,pass\n"<<expected<<','<<measured<<','<<measured/expected-1<<",1\n";
}

static double rc(double temperature,double cap,unsigned subdivisions) {
    constexpr unsigned count=16384;
    const double resistance_ohm=1000,tau=resistance_ohm*cap,grid=.05*tau;
    const double source_sigma=std::sqrt(2*1.380649e-23*temperature*resistance_ohm/grid);
    noise_random random(3327);
    std::vector<std::pair<double,double>> wave;
    for (unsigned i=0;i<=count;++i) wave.emplace_back(i*grid,source_sigma*random.normal());
    simulation sim;sim.analytic_jacobian=true;
#ifdef ADC_USE_EIGEN
    use_sparse_solver(sim,sparse_strategy::certified_lu);
#endif
    electrical g,input,output;sim.reference(g);
    input=sim_pwl_voltage_source(g,wave);
    output=resistor(input,resistance(resistance_ohm));output=capacitor(g,capacitance_value(cap));
    sim.observe("output",output,g);
    double integral=0,elapsed=0,last_t=0,last_v=0;
    for (unsigned end=512;end<=count;end+=512) {
        if (end==512)sim.run(duration(end*grid),duration(grid/subdivisions));
        else sim.advance_to(duration(end*grid),duration(grid/subdivisions));
        for (const auto& row:sim.trace) {
            const double v=*row.values[0];
            if (last_t>=20*tau && row.time>last_t) {
                double dt=row.time-last_t;integral+=dt*(v*v+last_v*last_v)/2;elapsed+=dt;
            }
            last_t=row.time;last_v=v;
        }
        sim.discard_trace();
    }
    return std::sqrt(integral/elapsed);
}

static void thermal_rc(const std::filesystem::path& dir) {
    std::ofstream out(dir/"noise_rc.csv");out<<std::setprecision(17);
    out<<"temperature_K,capacitance_F,subdivisions,expected_rms_V,measured_rms_V,relative_rms_error,pass\n";
    double coarse=0;
    for (auto [temp,cap,steps]:std::vector<std::tuple<double,double,unsigned>>{
            {300.15,1e-12,4},{300.15,1e-12,8},{233.15,.5e-12,4},{398.15,2e-12,4}}) {
        double measured=rc(temp,cap,steps),expected=std::sqrt(1.380649e-23*temp/cap);
        require(std::abs(measured/expected-1)<.12,"electrical Johnson-noise RC disagrees with kT/C");
        if (!coarse)coarse=measured;
        if (steps==8)require(std::abs(measured/coarse-1)<.005,"noise RC timestep calibration failed");
        out<<temp<<','<<cap<<','<<steps<<','<<expected<<','<<measured<<','<<measured/expected-1<<",1\n";
    }
}

int main(int argc,char** argv) {
    try {
        require(argc==3,"usage: adc_sar_noise_tests SUITE RESULT_DIRECTORY");
        std::filesystem::path dir=argv[2];std::filesystem::create_directories(dir);
        const std::string name=argv[1];
        if (name=="model")model(dir);
        else if (name=="rc")thermal_rc(dir);
        else if (name=="profile")calibrated_profile(dir);
        else throw std::invalid_argument("unknown noise suite");
        std::cout<<"PASS noise_"<<name<<'\n';
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
