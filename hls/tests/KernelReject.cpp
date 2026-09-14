#include <cstdint>
#include <cstdlib>

struct State { uint64_t value; uint32_t (*callback)(uint32_t); };
extern "C" uint64_t unavailable(uint32_t);
__attribute__((noinline)) uint64_t recursive(uint32_t n) { return n ? recursive(n - 1) + recursive(n / 2) : 1; }
extern "C" const uint64_t cpphdl_hls_state_bytes = sizeof(State);
#if REJECT_KIND == 6
extern "C" const uint64_t cpphdl_hls_state_align = 64;
#else
extern "C" const uint64_t cpphdl_hls_state_align = alignof(State);
#endif
extern "C" uint64_t rejected_kernel(State* self, uint32_t op, uint32_t index, uint32_t value) {
#if REJECT_KIND == 0
    return unavailable(value);
#elif REJECT_KIND == 1
    return self->callback(value);
#elif REJECT_KIND == 2
    return recursive(value);
#elif REJECT_KIND == 3
    double x = value; return uint64_t(x / (index + 0.5));
#elif REJECT_KIND == 4
    auto* p = static_cast<uint32_t*>(__builtin_alloca(value));
    p[index] = op; return p[op];
#else
    return __atomic_load_n(&self->value, __ATOMIC_SEQ_CST);
#endif
}
