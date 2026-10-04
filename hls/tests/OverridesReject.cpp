#include "../Clocked.h"
#include "../Overrides.h"
namespace override_api {
inline uint32_t compute(uint32_t value) { return value + 1; }
inline uint32_t variadic(uint32_t value, ...) { return value; }
}
#if OVERRIDE_CASE == 0
HLS_OVERRIDE("override_api::compute")
uint32_t wrong(uint64_t value) { return value; }
#elif OVERRIDE_CASE == 1
HLS_OVERRIDE("override_api::compute")
uint32_t first(uint32_t value) { return value; }
HLS_OVERRIDE("override_api::compute")
uint32_t second(uint32_t value) { return value; }
#elif OVERRIDE_CASE == 2
HLS_OVERRIDE("override_api::compute")
uint32_t cycle(uint32_t value) { return override_api::compute(value); }
#elif OVERRIDE_CASE == 3
HLS_OVERRIDE("override_api::compute")
uint32_t missing(uint32_t value);
#elif OVERRIDE_CASE == 4
HLS_OVERRIDE_BITS("@float.cast.u32.f32")
uint32_t unused_policy(uint32_t value) { return value; }
#elif OVERRIDE_CASE == 6
template<class T>
HLS_OVERRIDE("override_api::compute")
T not_concrete(T value) { return value; }
#elif OVERRIDE_CASE == 8
HLS_OVERRIDE("override_api::variadic")
uint32_t variadic_rtl(uint32_t value) { return value; }
#endif
struct OverrideRejectMethods {
    struct Keep { uint32_t value; ~Keep() {} };
    uint64_t command(uint32_t, uint32_t, uint32_t value) {
#if OVERRIDE_CASE == 4
        return uint64_t(float(value) + 1.0f);
#elif OVERRIDE_CASE == 5
    again:
        if (value--) goto again;
        return 0;
#elif OVERRIDE_CASE == 7
        const Keep& temporary = Keep{value};
        return temporary.value;
#elif OVERRIDE_CASE == 8
        return override_api::variadic(value, value);
#else
        return override_api::compute(value);
#endif
    }
};
class OverrideRejectTop : public cpphdl::Module {
public:
    cpphdl::hls::ClockedDelayer<OverrideRejectMethods> worker;
};
