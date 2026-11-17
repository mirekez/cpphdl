#pragma once
// Electrical characterization recipes. Every point uses the same connected
// slice and complete conversions; deliberately stressed points remain visible.
template<class Slice=adc_sar_slice<>>
static void characterize_case(const std::filesystem::path& dir,const std::string& name,
                              conversion_config cfg,bool required=false) {
    cfg.characterize=true;cfg.physical_clocks=true;cfg.gate=false;
    const auto work=dir/name;std::filesystem::create_directories(work);
    std::ofstream meta(work/"configuration.csv");meta<<std::setprecision(17);
    meta<<"name,required,temperature_C,source_ohm,frequency_Hz,amplitude_V,center_V,track_s,settle_s,evaluate_s,period_s,sample_period_s,ripple_io_V,ripple_core_V,ripple_Hz,jitter_sigma_s,seed,clock_slew_s,warmup_s,step_s,bdf2\n";
    meta<<name<<','<<required<<','<<Slice::corner.temperature<<','<<cfg.source_r<<','<<cfg.sine_frequency<<','
        <<cfg.sine_amplitude<<','<<cfg.sine_center<<','<<cfg.track<<','<<cfg.period*(1-cfg.evaluate_fraction)<<','
        <<cfg.period*cfg.evaluate_fraction<<','<<cfg.period<<','<<cfg.track+8*cfg.period+10e-9<<','<<cfg.ripple<<','
        <<cfg.core_ripple<<','<<(cfg.sinusoidal_ripple ? 1/cfg.ripple_period:0)<<','<<cfg.random_jitter<<','
        <<cfg.random_seed<<','<<cfg.clock_slew<<','<<cfg.warmup<<','<<cfg.dt<<','<<(cfg.method==integration_method::bdf2)<<'\n';meta.close();
    conversion<Slice>(work,"conversion",cfg);
}
static conversion_config coherent(unsigned count=64,unsigned bin=5) {
    conversion_config c;c.levels.assign(count,1.45);
    c.sine_frequency=bin/(count*(c.track+8*c.period+10e-9));return c;
}
static void characterization_suite(const std::filesystem::path& dir,const std::string& suite) {
    if (suite=="frequency") {
        for(unsigned bin:{1u,5u,15u,29u}) {
            auto c=coherent(64,bin);characterize_case(dir,"bin_"+std::to_string(bin),c,true);
        }
    } else if (suite=="amplitude") {
        for(double amplitude:{.05,.25,.55}) {
            auto c=coherent();c.sine_amplitude=amplitude;
            characterize_case(dir,"amplitude_"+std::to_string(int(amplitude*1000)),c,true);
        }
    } else if (suite=="impedance") {
        for(double r:{25.,50.,500.,5000.}) {
            auto c=coherent();c.source_r=r;
            characterize_case(dir,"source_"+std::to_string(int(r)),c,r<=50);
        }
    } else if (suite=="kickback") {
        for(double r:{25.,50.,500.,5000.}) {
            conversion_config c;c.source_r=r;c.levels={.9,1.45,2.};
            characterize_case(dir,"source_"+std::to_string(int(r)),c,r<=50);
        }
    } else if (suite=="psrr") {
        for(unsigned bin:{1u,5u,13u})for(unsigned rail:{0u,1u}) {
            conversion_config c;c.levels.assign(32,1.45);c.sinusoidal_ripple=true;
            c.ripple_period=32*(c.track+8*c.period+10e-9)/bin;
            if(rail)c.core_ripple=.025;else c.ripple=.025;
            characterize_case(dir,(rail ? "core_":"io_")+std::to_string(bin),c,true);
        }
        for(double amplitude:{.005,.05}) {
            conversion_config c;c.levels.assign(32,1.45);c.sinusoidal_ripple=true;
            c.ripple_period=32*(c.track+8*c.period+10e-9)/5;c.ripple=amplitude;
            characterize_case(dir,"io_amplitude_"+std::to_string(int(amplitude*1000)),c,true);
        }
        for(unsigned rail:{0u,1u}) {
            conversion_config c;c.levels.assign(32,1.45);c.sinusoidal_ripple=true;c.dt=5e-12;
            c.ripple_period=32*(c.track+8*c.period+10e-9)/5;
            if(rail)c.core_ripple=.025;else c.ripple=.025;
            characterize_case(dir,rail ? "core_refined5ps":"io_refined5ps",c,true);
        }
    } else if (suite=="acquisition") {
        for(double r:{50.,500.,5000.})for(double ns:{.25,.5,1.,2.,4.,8.}) {
            conversion_config c;c.levels={.9,2.,.9,2.};c.source_r=r;c.track=ns*1e-9;
            characterize_case(dir,"r"+std::to_string(int(r))+"_track_"+std::to_string(ns),c,r==50 && ns==8);
        }
        constexpr corner_parameters slow{125,.85,.85,.025,.025};
        constexpr corner_parameters fast{-40,1.15,1.15,-.025,-.025};
        for(double ns:{.25,.5,1.,2.,4.,8.}) {
            conversion_config c;c.levels={.9,2.,.9,2.};c.track=ns*1e-9;
            characterize_case<adc_sar_slice<8,ihp130_profile2,ihp130_passives,slow>>(dir,"generic_slow_track_"+std::to_string(ns),c,ns==8);
            characterize_case<adc_sar_slice<8,ihp130_profile2,ihp130_passives,fast>>(dir,"generic_fast_track_"+std::to_string(ns),c,ns==8);
        }
    } else if (suite=="evaluation") {
        for(double settle:{.5,1.,2.,3.})for(double eval:{.1,.25,.5,1.,2.}) {
            conversion_config c;c.levels={.9,2.,.9,2.};c.period=(settle+eval)*1e-9;c.evaluate_fraction=eval/(settle+eval);
            characterize_case(dir,"settle_"+std::to_string(settle)+"_eval_"+std::to_string(eval),c,settle>=2 && eval==2);
        }
    } else if (suite=="jitter") {
        for(double ps:{0.,10.,50.,100.})for(unsigned seed:{7u,19u}) {
            if(!ps && seed==19)continue;
            auto c=coherent(32,5);c.random_jitter=ps*1e-12;c.random_seed=seed;c.control_c=100e-15;
            characterize_case(dir,"sigma_"+std::to_string(int(ps))+"_seed_"+std::to_string(seed),c,ps<=50);
        }
    } else if (suite=="power") {
        for(double period:{2e-9,4e-9,6e-9}) {
            conversion_config c;c.period=period;c.levels={.9,2.,.9,2.};
            characterize_case(dir,"period_"+std::to_string(int(period*1e12)),c,period>=4e-9);
        }
        for(double warmup:{2e-9,5e-9}) {
            conversion_config c;c.warmup=warmup;c.levels={.9,2.,.9,2.};
            characterize_case(dir,"warmup_"+std::to_string(int(warmup*1e12)),c,true);
        }
        for(double dt:{.3125e-12,.15625e-12}) {
            conversion_config c;c.levels={1.45,1.45};c.dt=dt;c.method=integration_method::bdf2;c.edge_step=dt/312.5;c.edge_window=5e-12;
            characterize_case(dir,"refined_"+std::to_string(dt*1e12),c,true);
        }
    } else if(suite=="smoke") {
        conversion_config c;c.levels={.9,2.};c.random_jitter=25e-12;c.control_c=100e-15;
        characterize_case(dir,"smoke",c,true);
    } else throw std::runtime_error("unknown characterization suite");
}
