#include "../Clocked.h"

uint64_t external_step(uint32_t);
#if HLS_REJECT == 11
uint32_t volatile_identity(volatile uint32_t value) { return value; }
#endif
#if HLS_REJECT == 10
struct MutableConstant { mutable uint32_t value; };
constexpr MutableConstant mutableConstant{7};
#endif
struct UnsupportedMethods {
#if HLS_REJECT == 3
    volatile uint32_t device;
#endif
    uint64_t command(uint32_t op, uint32_t index, uint32_t value) {
#if HLS_REJECT == 0
        return external_step(value);
#elif HLS_REJECT == 1
        return value ? command(op, index, value - 1) : 0;
#elif HLS_REJECT == 2
        static uint32_t stored = 0;
        return ++stored;
#elif HLS_REJECT == 3
        return device;
#elif HLS_REJECT == 4
        float x = value;
        return uint64_t(x * 1.25f);
#elif HLS_REJECT == 5
        uint64_t (*fn)(uint32_t) = external_step;
        return fn(value);
#elif HLS_REJECT == 10
        return ++mutableConstant.value;
#elif HLS_REJECT == 11
        return volatile_identity(value);
#else
        return value;
#endif
    }
};
class UnsupportedTop : public cpphdl::Module {
public:
#if HLS_REJECT == 9
    cpphdl::hls::Clocked<UnsupportedMethods, 17> worker;
#else
    cpphdl::hls::Clocked<UnsupportedMethods> worker;
#endif
    void _work(bool reset) { worker._work(reset); }
    void _strobe() { worker._strobe(); }
};
