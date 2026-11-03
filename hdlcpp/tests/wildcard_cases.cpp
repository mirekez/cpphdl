#include <cassert>
#include <cstdint>
#include <cstdio>
#ifdef USE_VERILATOR
#include "Vwildcard_cases.h"
using Model = Vwildcard_cases;
#elif defined(USE_NATIVE)
#include "model.h"
using Model = cpphdl_native::Model;
#else
#include "generated/wildcard_cases.h"
#ifdef USE_COMBS
#include "wildcard_cases_optimized_combs.h"
#endif
using Model = wildcard_cases;
long _system_clock = 0;
#endif

int main() {
    Model model;
#if !defined(USE_VERILATOR) && !defined(USE_NATIVE)
    cpphdl::logic<16> data;
    cpphdl::logic<4> fallback;
    cpphdl::logic<128> wide;
    model.data_in = _ASSIGN_REG(data);
    model.fallback_in = _ASSIGN_REG(fallback);
    model.wide_in = _ASSIGN_REG(wide);
    model._assign();
#ifdef USE_COMBS
    bind_optimized_ports(model);
#endif
#endif
    for (unsigned value = 0; value < 65536; ++value) {
        unsigned fb = (value >> 5) & 15;
        uint32_t words[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu,
                            (value << 16) | 0xffffu};
#ifdef USE_VERILATOR
        model.data = value; model.fallback = fb;
        for (unsigned i = 0; i < 4; ++i) model.wide[i] = words[i];
        model.eval();
        unsigned first=model.first_zero, x=model.xdecode, z=model.zdecode, w=model.wide_decode;
        unsigned strict=model.strict_z, context=model.context_decode;
        unsigned symbolic0=model.symbolic0, symbolic1=model.symbolic1;
#elif defined(USE_NATIVE)
        model.data[0] = value; model.fallback[0] = fb;
        for (unsigned i = 0; i < 4; ++i) model.wide[i] = words[i];
        model.eval(false);
        unsigned first=model.first_zero[0], x=model.xdecode[0], z=model.zdecode[0], w=model.wide_decode[0];
        unsigned strict=model.strict_z[0], context=model.context_decode[0];
        unsigned symbolic0=model.symbolic0[0], symbolic1=model.symbolic1[0];
#else
        data = value; fallback = fb;
        for (unsigned i = 0; i < 4; ++i) wide.bits(i * 32 + 31, i * 32) = cpphdl::logic<32>(words[i]);
        ++_system_clock;
#ifdef USE_COMBS
        calc_all(model, false);
#endif
        unsigned first=uint64_t(model.first_zero_out()), x=uint64_t(model.xdecode_out());
        unsigned z=uint64_t(model.zdecode_out()), w=uint64_t(model.wide_decode_out());
        unsigned strict=uint64_t(model.strict_z_out()), context=uint64_t(model.context_decode_out());
        unsigned symbolic0=uint64_t(model.symbolic0_out()), symbolic1=uint64_t(model.symbolic1_out());
#endif
        unsigned expected = fb;
        for (unsigned bit = 0; bit < 16; ++bit) if (!(value & (1u << bit))) { expected = bit; break; }
        assert(first == expected);
        unsigned low = value & 15;
        assert(x == (low >= 8 ? 1 : low >= 4 ? 2 : low >= 2 ? 3 : low == 0 ? 4 : 5));
        assert(z == (low == 15 ? 1 : (value & 128) ? 2 : (value & 64) ? 3 : 7));
        assert(w == ((value >> 12) == 8 ? 1 : (value & 4095) == 4095 ? 2 : 3));
        assert(strict == 1);
        assert(context == (low == 15 ? 2 : 0));
        unsigned tagged = (value & 5) == 5 ? 3 : 1;
        assert(symbolic0 == ((value & 16) ? 2 : tagged));
        assert(symbolic1 == ((value & 16) ? tagged : 2));
        std::printf("%u %u %u %u %u %u %u %u\n", first, x, z, w, strict, context, symbolic0, symbolic1);
    }
}
