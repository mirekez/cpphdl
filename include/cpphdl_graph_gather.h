#pragma once
#include <cstdint>
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#endif

namespace cpphdl::graph_runtime {
// PDEP(PEXT(value, source), destination), with a portable bit-exact fallback.
inline uint64_t gatherBitsPortable(uint64_t value,uint64_t source,uint64_t destination) noexcept {
    uint64_t result=0;
    while(source&&destination) {
        const uint64_t from=source&(uint64_t(0)-source);
        const uint64_t to=destination&(uint64_t(0)-destination);
        if(value&from)result|=to;
        source&=source-1;destination&=destination-1;
    }
    return result;
}
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
[[gnu::target("bmi2"),gnu::noinline]] inline uint64_t gatherBitsBMI2(
        uint64_t value,uint64_t source,uint64_t destination) noexcept {
    return _pdep_u64(_pext_u64(value,source),destination);
}
#endif
inline uint64_t gatherBits(uint64_t value,uint64_t source,uint64_t destination) noexcept {
#if defined(__BMI2__) && defined(__x86_64__)
    // An explicitly BMI2-targeted translation unit already requires BMI2.
    // Keep its two instructions inline instead of adding a call and dispatch
    // branch at every generated sparse route. Generic builds stay portable.
    return _pdep_u64(_pext_u64(value,source),destination);
#elif (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
    static const bool supported=__builtin_cpu_supports("bmi2");
    if(supported)return gatherBitsBMI2(value,source,destination);
    return gatherBitsPortable(value,source,destination);
#else
    return gatherBitsPortable(value,source,destination);
#endif
}
}
