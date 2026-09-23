#include <cpphdl.h>
using namespace cpphdl;

struct CastBase { virtual ~CastBase() = default; };
struct CastDerived : CastBase { int value; };
class CastReject : public Module {
public:
    uint64_t result;
#if CAST_REJECT == 0
    void test(CastBase& base) { result = dynamic_cast<CastDerived&>(base).value; }
#elif CAST_REJECT == 1
    void test(uint64_t* p) { result = reinterpret_cast<uintptr_t>(p); }
#elif CAST_REJECT == 2
    void test(uintptr_t p) { result = *reinterpret_cast<uint64_t*>(p); }
#elif CAST_REJECT == 3
    void test(double value) { result = static_cast<uint64_t>(value); }
#elif CAST_REJECT == 4
    void test(unsigned& value) { result = reinterpret_cast<int&>(value); }
#elif CAST_REJECT == 5
    void test(uint64_t* p) { result = (uintptr_t)p; }
#elif CAST_REJECT == 6
    void test(int CastDerived::* p) { result = static_cast<bool>(p); }
#else
    void test(uint32_t value) { result = static_cast<bool>(static_cast<float>(value)); }
#endif
};
