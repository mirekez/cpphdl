#include <cpphdl.h>
using namespace cpphdl;

struct CastBase { virtual ~CastBase() = default; };
struct CastDerived : CastBase { int value; };
// Conversion-only checks: these casts are deliberately ignored, not claimed
// to implement runtime pointers, RTTI or floating-point arithmetic in RTL.
class CastFallback : public Module {
public:
    uint64_t result;
#if CAST_FALLBACK == 0
    void test(CastBase& base) { result = dynamic_cast<CastDerived&>(base).value; }
#elif CAST_FALLBACK == 1
    void test(uint64_t* p) { result = reinterpret_cast<uintptr_t>(p); }
#elif CAST_FALLBACK == 2
    void test(uintptr_t p) { result = *reinterpret_cast<uint64_t*>(p); }
#elif CAST_FALLBACK == 3
    void test(double value) { result = static_cast<uint64_t>(value); }
#elif CAST_FALLBACK == 4
    void test(unsigned& value) { result = reinterpret_cast<int&>(value); }
#elif CAST_FALLBACK == 5
    void test(uint64_t* p) { result = (uintptr_t)p; }
#elif CAST_FALLBACK == 6
    void test(int CastDerived::* p) { result = static_cast<bool>(p); }
#else
    void test(uint32_t value) { result = static_cast<bool>(static_cast<float>(value)); }
#endif
};
