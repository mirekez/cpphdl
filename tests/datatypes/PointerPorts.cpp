#include <cpphdl.h>
#include <cstddef>
#include <cstdio>
#include <memory>

#ifndef SYNTHESIS
using namespace cpphdl;

long _system_clock = 0;

// Keep binding overloads available for zero-argument callables, including
// lvalue-only and mutable functors; invalid calls must fail by substitution.
struct LvaluePointerSource { logic<8>* operator()() &; };
struct MutableValueSource { logic<8> operator()(); };
struct NeedsArgument { logic<8> operator()(int); };
struct RvalueOnlySource { logic<8> operator()() &&; };
using TestPort = function_ref<logic<8>>;
static_assert(std::is_constructible_v<TestPort, LvaluePointerSource>);
static_assert(std::is_constructible_v<TestPort, MutableValueSource>);
static_assert(std::is_constructible_v<TestPort, logic<8>(*)()>);
static_assert(std::is_constructible_v<TestPort, logic<8>(*)() noexcept>);
static_assert(std::is_constructible_v<TestPort, TestPort&>);
static_assert(!std::is_constructible_v<TestPort, NeedsArgument>);
static_assert(!std::is_constructible_v<TestPort, RvalueOnlySource>);
static_assert(!std::is_constructible_v<TestPort, decltype(&MutableValueSource::operator())>);

template<typename T, typename Raw>
struct PointerPortProbe
{
    T source = 0;
    _PORT(T) constructed = _ASSIGN_REG(source);
    _PORT(T) assigned;
    _PORT(T) mutable_bound;
    _PORT(Raw) raw;

    void _assign()
    {
        assigned = _ASSIGN_COMB(value_comb_func());
        mutable_bound = [this]() mutable { return &source; };
        raw = _ASSIGN_REG(source);
    }

    T& value_comb_func() { return source; }
};

template<typename T, typename Raw>
bool check(Raw value)
{
    PointerPortProbe<T, Raw> probe;
    probe._assign();
    for (Raw sample : {Raw(0), value, Raw(1)}) {
        probe.source = T(sample);
        if (Raw(probe.constructed()) != sample || Raw(probe.assigned()) != sample
            || Raw(probe.mutable_bound()) != sample || probe.raw() != sample) {
            std::printf("pointer port value mismatch\n");
            return false;
        }
        // Compatible pointers must still refer directly to the source storage.
        if (std::addressof(probe.raw()) != &probe.source) return false;
        ++_system_clock;
    }
    return true;
}

template<typename T, typename Raw>
bool check_packed(Raw value)
{
    struct __attribute__((packed)) Packed { uint8_t pad; T source; };
    static_assert(offsetof(Packed, source) == 1, "test requires unaligned scalar storage");
    alignas(8) Packed packed{};
    function_ref<T> constructed = _ASSIGN_REG(packed.source);
    function_ref<T> assigned;
    assigned = _ASSIGN_REG(packed.source);
    for (Raw sample : {Raw(0), value, Raw(1)}) {
        packed.source = T(sample);
        if (Raw(constructed()) != sample || Raw(assigned()) != sample) return false;
        ++_system_clock;
    }
    return true;
}

int main()
{
    bool ok = true;
    ok &= check<u8, uint8_t>(0xe5);
    ok &= check<u16, uint16_t>(0x9876);
    ok &= check<u32, uint32_t>(0xfedcba98);
    ok &= check<u64, uint64_t>(0xfedcba9876543210ULL);
    ok &= check<i8, int8_t>(-101);
    ok &= check<i16, int16_t>(-12345);
    ok &= check<i32, int32_t>(-123456789);
    ok &= check<i64, int64_t>(-123456789012345LL);
    ok &= check<u<8>, u<8>>(u<8>(0xe5));
    ok &= check_packed<u16, uint16_t>(0x9876);
    ok &= check_packed<u32, uint32_t>(0xfedcba98);
    ok &= check_packed<u64, uint64_t>(0xfedcba9876543210ULL);
    ok &= check_packed<i64, int64_t>(-123456789012345LL);
    return !ok;
}
#endif
