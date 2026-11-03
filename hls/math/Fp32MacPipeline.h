#pragma once
#include "Float.h"

namespace cpphdl::hls::math {
// A source-level MAC module: the ordinary C++ implementation is the reference
// for synthesis. The replacement retains separate FP32 multiply/add rounding.
struct [[clang::annotate("CPPHDL_REPLACEMENT_MODULE=Fp32MacPipeline")]]
       [[clang::annotate("CPPHDL_REPLACEMENT_MIN_LATENCY=2")]]
       [[clang::annotate("CPPHDL_REPLACEMENT_SCRIPT=generate_mac_replacement.py;")]] Fp32MacMethods {
    static float calculate(float c,float a,float b) { return c+a*b; }
    uint32_t command(uint32_t c,uint32_t a,uint32_t b) const {
        return encode(calculate(decode(c),decode(a),decode(b)));
    }
};
}
