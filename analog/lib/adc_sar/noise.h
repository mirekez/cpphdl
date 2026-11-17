#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace analog::adc_sar {

inline constexpr double boltzmann_J_K=1.380649e-23; // Exact SI value.

// Sampled, input-referred EQUIVALENT noise, not intrinsic PSP device noise.
// PSDs are one-sided V^2/Hz. Extra PSD must exclude sampling noise when kT/C
// is enabled. Defaults predict ideal settled sampling noise only; zero extra
// PSD is an omitted contribution, not a measured noiseless amplifier.
struct noise_parameters {
    bool include_ktc=true;
    double capacitance_F=0;            // Zero uses the slice's temperature-scaled hold C + lumped held C.
    double ktc_variance_scale=1;       // Explicit calibration factor; unity is ideal kT/C.
    double white_psd_V2_Hz=0;          // Additional input-referred white PSD, calibrated at this bias/T.
    double flicker_V2=0;               // K in S(f)=K/f, calibrated at this bias/T.
    double low_Hz=1000, high_Hz=8e6;    // Explicit integration band for additional PSD.
    double reference_temperature_K=0; // Required for nonzero PSD; reject uncalibrated T extrapolation.
};

// Portable PRNG/Box-Muller: deterministic per seed, independent of solver
// steps, Newton iterations, rejected steps and std::normal_distribution.
class noise_random {
    std::uint64_t state_;
public:
    explicit noise_random(std::uint64_t seed):state_(seed) {}
    double uniform() {
        std::uint64_t z=(state_+=0x9e3779b97f4a7c15ULL);
        z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;
        z=(z^(z>>27))*0x94d049bb133111ebULL;
        z^=z>>31;
        return (double(z>>12)+.5)/4503599627370496.; // Strictly inside (0,1).
    }
    double normal() {
        const double radius=std::sqrt(-2*std::log(uniform()));
        return radius*std::cos(2*std::acos(-1.)*uniform());
    }
};

namespace noise_detail {
inline void inverse_fft(std::vector<std::complex<double>>& x) {
    const auto n=x.size();
    for (std::size_t i=1,j=0;i<n;++i) {
        std::size_t bit=n>>1;
        for (;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if (i<j) std::swap(x[i],x[j]);
    }
    for (std::size_t length=2;length<=n;length*=2) {
        const auto w=std::polar(1.,2*std::acos(-1.)/length);
        for (std::size_t i=0;i<n;i+=length) {
            std::complex<double> phase=1;
            for (std::size_t j=0;j<length/2;++j) {
                const auto a=x[i+j],b=x[i+j+length/2]*phase;
                x[i+j]=a+b; x[i+j+length/2]=a-b; phase*=w;
            }
        }
    }
    for (auto& v:x) v/=double(n);
}
inline std::vector<double> spectrum_record(std::size_t n,double dt,double low,double high,
                                          double white,double flicker,std::uint64_t seed) {
    std::vector<std::complex<double>> bins(n);
    noise_random rng(seed);
    const double df=1/(n*dt);
    for (std::size_t k=1;k<=n/2;++k) {
        const double a=std::max(low,(k-.5)*df),b=std::min(high,(k+.5)*df);
        if (b<=a) continue;
        const double variance=white*(b-a)+flicker*std::log(b/a);
        if (k==n/2) bins[k]=double(n)*std::sqrt(variance)*rng.normal();
        else {
            // Pair k,N-k contributes variance S*df to the real time record.
            const double real=rng.normal(),imaginary=rng.normal();
            bins[k]=double(n)*std::sqrt(variance/4)*std::complex<double>(real,imaginary);
            bins[n-k]=std::conj(bins[k]);
        }
    }
    inverse_fft(bins);
    std::vector<double> result; result.reserve(n);
    for (auto v:bins) result.push_back(v.real());
    return result;
}
} // namespace noise_detail

struct noise_record {
    std::vector<double> sampling,white,flicker,total;
    double temperature_K=0,capacitance_F=0,period_s=0;
    double sampling_variance_V2=0,white_variance_V2=0,flicker_variance_V2=0;
    std::uint64_t seed=0;
};

// Generate once, then consume by sample index. Never wrap this finite record:
// repeating its FFT period would fabricate correlation. The low-frequency
// cutoff must be resolved by its duration. White/flicker components retain
// their band-limited time correlation, rather than becoming IID offsets.
inline noise_record make_noise_record(noise_parameters p,double temperature_K,double default_C,
                                      std::size_t n,double period_s,std::uint64_t seed) {
    auto positive=[](double x) { return std::isfinite(x) && x>0; };
    auto nonnegative=[](double x) { return std::isfinite(x) && x>=0; };
    if (!positive(temperature_K) || !positive(default_C) || !positive(period_s)
        || !nonnegative(p.capacitance_F) || !nonnegative(p.ktc_variance_scale)
        || !nonnegative(p.white_psd_V2_Hz) || !nonnegative(p.flicker_V2)
        || !positive(p.low_Hz) || !positive(p.high_Hz) || p.low_Hz>=p.high_Hz
        || n<16 || n>(1u<<22) || (n&(n-1)))
        throw std::invalid_argument("invalid sampled-noise parameters or record length");
    const bool colored=p.white_psd_V2_Hz>0 || p.flicker_V2>0;
    if (!nonnegative(p.reference_temperature_K) || (colored &&
        (!positive(p.reference_temperature_K) || std::abs(p.reference_temperature_K-temperature_K)>1e-6)))
        throw std::invalid_argument("additional noise PSD needs calibration at the requested temperature");
    if (colored && (p.high_Hz>.5/period_s || p.low_Hz<1/(n*period_s)))
        throw std::invalid_argument("noise band exceeds Nyquist or record frequency resolution");
    noise_record r;
    r.temperature_K=temperature_K; r.capacitance_F=p.capacitance_F ? p.capacitance_F : default_C;
    r.period_s=period_s; r.seed=seed;
    r.sampling_variance_V2=p.include_ktc ? p.ktc_variance_scale*boltzmann_J_K*temperature_K/r.capacitance_F : 0;
    r.white_variance_V2=p.white_psd_V2_Hz*(p.high_Hz-p.low_Hz);
    r.flicker_variance_V2=p.flicker_V2*std::log(p.high_Hz/p.low_Hz);
    for (double v:{r.sampling_variance_V2,r.white_variance_V2,r.flicker_variance_V2})
        if (!nonnegative(v)) throw std::invalid_argument("noise variance overflow");
    r.sampling.resize(n); r.white.resize(n); r.flicker.resize(n); r.total.resize(n);
    noise_random rng(seed^0x12cbfee581da29cbULL);
    for (auto& v:r.sampling) v=std::sqrt(r.sampling_variance_V2)*rng.normal();
    if (p.white_psd_V2_Hz) r.white=noise_detail::spectrum_record(n,period_s,p.low_Hz,p.high_Hz,
        p.white_psd_V2_Hz,0,seed^0x77de892184cb916bULL);
    if (p.flicker_V2) r.flicker=noise_detail::spectrum_record(n,period_s,p.low_Hz,p.high_Hz,
        0,p.flicker_V2,seed^0xaf923b17e2263e9aULL);
    for (std::size_t i=0;i<n;++i) r.total[i]=r.sampling[i]+r.white[i]+r.flicker[i];
    return r;
}
} // namespace analog::adc_sar
