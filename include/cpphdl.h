#pragma once

#include "cpphdl_comb_proof.h"

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned uint32_t;
typedef unsigned long uint64_t;
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long int64_t;

namespace cpphdl
{


typedef unsigned char byte;

constexpr unsigned flog2(unsigned x)
{
    return x == 1 ? 0 : 1+flog2(x >> 1);
}

constexpr unsigned clog2(unsigned x)
{
    return x == 1 ? 0 : flog2(x - 1) + 1;
}


}

#include <type_traits>
#include <array>
#include <initializer_list>
#include <utility>

template<typename T>
struct is_from_cpphdl_namespace : std::false_type {};

#include <string>

struct cpphdl_exception
{
    std::string text;
};

#define cpphdl_assert(a, text) if (!(a)) { throw cpphdl_exception{text}; }

#include "cpphdl_types.h"
#include "cpphdl_array.h"
#include "cpphdl_logic.h"
#include "cpphdl_reg.h"
#include "cpphdl_format.h"
#include "cpphdl_cat.h"
#include "cpphdl_memory.h"
#include "cpphdl_module.h"
#include "cpphdl_port.h"

#ifdef VERILATOR
// Some Verilator installations still expect user code to provide this
// timestamp hook even when tests do not use delays or SystemC. Keep the shared
// harness linkable with both conda and system Verilator headers.
// The generated model is the only caller, so force the inline definition into
// the testbench object even when that translation unit does not call it.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((used))
#endif
inline double sc_time_stamp()
{
    return 0.0;
}
#endif

namespace cpphdl
{





namespace detail
{

template<typename T, typename = void>
struct has_bits_method : std::false_type {};

template<typename T>
struct has_bits_method<T, std::void_t<decltype(std::declval<const T&>().bits(size_t{}, size_t{}))>> : std::true_type {};

}

template<typename T, typename V>
constexpr void sv_assign_field(T& dst, const V& value);

template<typename Target, typename Source>
constexpr Target convert_packed(const Source& source);

// Aggregate assignment reaches these overloads before their later definitions.
// Without declarations, dependent lookup misses packed and standard arrays.
// Declare every array form here so generated field assignments resolve reliably.
template<typename T, size_t N, typename V>
constexpr void sv_assign_field(std::array<T, N>& dst, std::initializer_list<V> values);

template<typename T, size_t N, typename V>
constexpr void sv_assign_field(std::array<T, N>& dst, const V& value);

template<typename T, size_t N, bool PACKED, typename V>
constexpr void sv_assign_field(array<N, T, PACKED>& dst, const V& value);

template<typename T, typename V>
constexpr T sv_cast(const V& value)
{
    T out{};
    sv_assign_field(out, value);
    return out;
}

template<size_t WIDTH, typename T>
constexpr int64_t sv_signed(const T& value)
{
    uint64_t raw = static_cast<uint64_t>(value);
    if constexpr (WIDTH == 0) {
        return 0;
    }
    else if constexpr (WIDTH >= 64) {
        return static_cast<int64_t>(raw);
    }
    else {
        constexpr uint64_t mask = (1ull << WIDTH) - 1ull;
        constexpr uint64_t sign = 1ull << (WIDTH - 1);
        raw &= mask;
        if ((raw & sign) != 0) {
            raw |= ~mask;
        }
        return static_cast<int64_t>(raw);
    }
}

template<size_t WIDTH, typename T>
constexpr uint64_t sv_unsigned(const T& value)
{
    uint64_t raw = static_cast<uint64_t>(value);
    if constexpr (WIDTH == 0) {
        return 0;
    }
    else if constexpr (WIDTH >= 64) {
        return raw;
    }
    else {
        return raw & ((1ull << WIDTH) - 1ull);
    }
}

template<size_t WIDTH, typename T>
constexpr logic<WIDTH> sv_bits(const T& value, size_t last, size_t first)
{
    // Keep slices in logic form so selections wider than the host uint64_t retain
    // every requested bit. logic performs the destination-width truncation itself.
    if constexpr (detail::has_bits_method<T>::value) {
        return logic<WIDTH>(value.bits(last, first));
    }
    else if constexpr (detail::has_pack_method<T>::value) {
        return logic<WIDTH>(value.pack().bits(last, first));
    }
    else {
        auto raw = static_cast<uint64_t>(value);
        if constexpr (WIDTH == 0) {
            return logic<WIDTH>(0);
        }
        else if constexpr (WIDTH >= 64) {
            return logic<WIDTH>(raw >> first);
        }
        else {
            return logic<WIDTH>((raw >> first) & ((1ull << WIDTH) - 1ull));
        }
    }
}

// Two-state CppHDL values cannot contain SystemVerilog X or Z states.
// Generated $isunknown expressions still require a callable runtime operation.
// Report false explicitly to preserve that two-state model contract.
template<typename T>
constexpr bool sv_isunknown(const T&)
{
    return false;
}

template<typename T>
constexpr uint64_t sv_bits_runtime(const T& value, size_t last, size_t first)
{
    auto width = last >= first ? last - first + 1 : 0;
    uint64_t raw = 0;
    if constexpr (detail::has_bits_method<T>::value) {
        raw = static_cast<uint64_t>(value.bits(last, first));
    }
    else {
        raw = static_cast<uint64_t>(value) >> first;
    }
    if (width == 0) {
        return 0;
    }
    if (width >= 64) {
        return raw;
    }
    return raw & ((1ull << width) - 1ull);
}

template<size_t WIDTH>
constexpr logic<WIDTH> byteswap(const logic<WIDTH>& value)
{
    logic<WIDTH> out = 0;
    constexpr size_t byte_count = (WIDTH + 7) / 8;
    for (size_t byte = 0; byte < byte_count; ++byte) {
        for (size_t bit = 0; bit < 8; ++bit) {
            size_t src = byte * 8 + bit;
            size_t dst = (byte_count - 1 - byte) * 8 + bit;
            if (src < WIDTH && dst < WIDTH) {
                out.set(dst, value.get(src));
            }
        }
    }
    return out;
}

template<size_t WIDTH>
constexpr logic<WIDTH> byteswap(const logic_bits<WIDTH>& value)
{
    return byteswap(static_cast<const logic<WIDTH>&>(value));
}

// Packed structs did not match the logic-only byteswap overloads.
// Their SystemVerilog representation is the bit sequence returned by pack().
// Delegate through pack() so generated byteswap calls retain packed field order.
template<typename T, typename Raw = std::remove_cv_t<std::remove_reference_t<T>>,
    typename std::enable_if_t<detail::has_pack_method<Raw>::value && !is_logic_v<Raw>, int> = 0>
constexpr auto byteswap(const T& value)
{
    return byteswap(value.pack());
}

template<typename T, size_t WIDTH, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
constexpr T operator&(T lhs, const logic<WIDTH>& rhs)
{
    return static_cast<T>(static_cast<uint64_t>(lhs) & static_cast<uint64_t>(rhs));
}

template<typename T, size_t WIDTH, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
constexpr T operator|(T lhs, const logic<WIDTH>& rhs)
{
    return static_cast<T>(static_cast<uint64_t>(lhs) | static_cast<uint64_t>(rhs));
}

template<typename T, size_t WIDTH, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
constexpr T operator^(T lhs, const logic<WIDTH>& rhs)
{
    return static_cast<T>(static_cast<uint64_t>(lhs) ^ static_cast<uint64_t>(rhs));
}

namespace detail
{
template<typename T, typename = void>
struct is_sv_unpacked_array : std::false_type {};

template<typename T>
struct is_sv_unpacked_array<T, std::void_t<typename T::value_type,
    decltype(T::COUNT_VALUE), decltype(T::PACKED)>> : std::bool_constant<!T::PACKED> {};
}

template<typename T, typename = void>
struct type_width_value
{
    static constexpr size_t value = sizeof(T) * 8;
};

template<typename T>
struct type_width_value<T, std::void_t<decltype(T::_size_bits())>>
{
    static constexpr size_t value = [] {
        if constexpr (detail::is_sv_unpacked_array<T>::value) {
            return T::COUNT_VALUE * type_width_value<typename T::value_type>::value;
        }
        else {
            return T::_size_bits();
        }
    }();
};

template<size_t WIDTH>
struct type_width_value<logic<WIDTH>, void>
{
    static constexpr size_t value = WIDTH;
};

template<typename T, size_t N, bool PACKED>
struct type_width_value<array<N, T, PACKED>, void>
{
    static constexpr size_t value = PACKED ? array<N, T, PACKED>::_size_bits()
                                          : N * type_width_value<T>::value;
};

template<typename T>
constexpr size_t type_width()
{
    return type_width_value<std::remove_cv_t<std::remove_reference_t<T>>>::value;
}

template<size_t WIDTH, typename T>
constexpr logic<WIDTH> pack_value(const T& value)
{
    // SV bit-vector conversions use logical element widths, not the byte stride
    // of addressable C++ array storage (also inherited by array registers).
    if constexpr (detail::is_sv_unpacked_array<T>::value) {
        constexpr size_t elementWidth = type_width<typename T::value_type>();
        logic<type_width<T>()> packed = 0;
        if constexpr (elementWidth != 0) {
            for (size_t index = 0; index < T::COUNT_VALUE; ++index) {
                packed.bits((index + 1) * elementWidth - 1, index * elementWidth) =
                    pack_value<elementWidth>(value[index]);
            }
        }
        return logic<WIDTH>(packed);
    }
    // Packed-array element proxies are addressable views, not scalar integers.
    // Falling through a uint64_t conversion truncated proxy values wider than 64 bits.
    // Convert logic directly and proxy views through their declared element width.
    else if constexpr (detail::has_pack_method<T>::value) {
        return logic<WIDTH>(value.pack());
    }
    else if constexpr (is_logic_v<T> || is_logic_bits_v<T>) {
        return logic<WIDTH>(value);
    }
    else if constexpr (detail::is_array_packed_ref<std::remove_cv_t<std::remove_reference_t<T>>>::value) {
        using ref_t = std::remove_cv_t<std::remove_reference_t<T>>;
        return logic<WIDTH>(static_cast<logic<detail::is_array_packed_ref<ref_t>::element_bits>>(value));
    }
    else if constexpr (detail::can_static_cast_uint64<T>::value) {
        return logic<WIDTH>(static_cast<uint64_t>(value));
    }
    else {
        return logic<WIDTH>(0);
    }
}

// Packed field bounds are known after elaboration. Write the source directly,
// rather than constructing a parent-width slice, reading it, then writing back.
template<size_t First, size_t Width, size_t Total>
__attribute__((always_inline)) constexpr void sv_insert_field(
    logic<Total>& destination, const logic<Width>& value)
{
    static_assert(First <= Total && Width <= Total - First);
    if constexpr (Width == 0) return;
    constexpr size_t shift = First % 8;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if constexpr (Width != 0 && Width + shift <= 64) {
        if (!detail::is_constant_evaluated_compat()) {
            constexpr size_t bytes = (Width + shift + 7) / 8;
            constexpr uint64_t mask = (~uint64_t{0} >> (64 - Width)) << shift;
            uint64_t word = 0;
            std::memcpy(&word, destination.bytes + First / 8, bytes);
            word = (word & ~mask) | ((uint64_t(value) << shift) & mask);
            std::memcpy(destination.bytes + First / 8, &word, bytes);
            return;
        }
    }
#endif
    for (size_t byte = First / 8; byte < (First + Width + 7) / 8; ++byte) {
        const size_t begin = byte * 8 < First ? First - byte * 8 : 0;
        const size_t end = First + Width < byte * 8 + 8 ? First + Width - byte * 8 : 8;
        const unsigned mask = ((1u << (end - begin)) - 1u) << begin;
        const size_t source = byte * 8 + begin - First;
        uint16_t pair = value.bytes[source / 8];
        if (source / 8 + 1 < logic<Width>::SIZE) {
            pair |= static_cast<uint16_t>(value.bytes[source / 8 + 1]) << 8;
        }
        destination.bytes[byte] = static_cast<uint8_t>((destination.bytes[byte] & ~mask) |
            (((pair >> (source % 8)) << begin) & mask));
    }
}

template<typename T, size_t WIDTH>
constexpr T unpack_value(const logic<WIDTH>& value)
{
    T out{};
    // Invert pack_value rather than array's raw-storage assignment operator.
    // In particular, an eleven-bit struct must advance eleven bits, not sizeof(T).
    if constexpr (detail::is_sv_unpacked_array<T>::value) {
        using element_type = typename T::value_type;
        constexpr size_t elementWidth = type_width<element_type>();
        const logic<type_width<T>()> packed = value;
        if constexpr (elementWidth != 0) {
            for (size_t index = 0; index < T::COUNT_VALUE; ++index) {
                out[index] = unpack_value<element_type>(logic<elementWidth>(
                    packed.bits((index + 1) * elementWidth - 1, index * elementWidth)));
            }
        }
    }
    else if constexpr (std::is_assignable_v<T&, logic<WIDTH>>) {
        out = value;
    }
    else if constexpr (std::is_constructible_v<T, logic<WIDTH>>) {
        out = T(value);
    }
    else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        out = static_cast<T>(static_cast<uint64_t>(value));
    }
    else if constexpr (detail::can_assign_from<T, int>::value) {
        out = 0;
    }
    return out;
}

namespace detail {
template<typename Target, typename Source, typename = void>
struct compatible_generated_layout : std::false_type {};

template<typename Target, typename Source>
struct compatible_generated_layout<Target, Source,
    std::void_t<decltype(Target::template __hdlcpp_layout_compatible<Source>())>>
    : std::bool_constant<generated_layout<Target>::value && generated_layout<Source>::value &&
                        Target::template __hdlcpp_layout_compatible<Source>()> {};
}

// Equal-width bit vectors are not enough: fields must match in name, offset
// and width. All unproved layouts (including unions and custom packers) retain
// the original pack/unpack semantics. Keep this fallback usable in C++17.
template<typename Target, typename Source>
constexpr Target convert_packed(const Source& source)
{
    if constexpr (detail::compatible_generated_layout<Target, Source>::value) {
        if constexpr (std::is_same_v<Target, Source> && detail::native_packed_element<Target>::value)
            return source;
        Target result{};
        result.__hdlcpp_assign_layout(source);
        return result;
    }
    else return unpack_value<Target>(pack_value<type_width<Target>()>(source));
}

// Generated expressions shift packed structs as their SystemVerilog bit vectors.
// C++ has no shift operator for user-defined packed aggregate types by default.
// Pack first and then apply the shift to the equivalent scalar representation.
// Scalar-convertible wrappers already use built-in shifts and must not match here.
template<typename T, typename std::enable_if_t<detail::has_pack_method<T>::value &&
    !is_logic_v<T> && !detail::can_static_cast_uint64<T>::value, int> = 0>
constexpr logic<type_width<T>()> operator>>(const T& lhs, unsigned rhs)
{
    return lhs.pack() >> rhs;
}

template<typename T, typename std::enable_if_t<detail::has_pack_method<T>::value &&
    !is_logic_v<T> && !detail::can_static_cast_uint64<T>::value, int> = 0>
constexpr logic<type_width<T>()> operator<<(const T& lhs, unsigned rhs)
{
    return lhs.pack() << rhs;
}

template<size_t COUNT, size_t WIDTH>
constexpr logic<COUNT * WIDTH> repeat(const logic<WIDTH>& value)
{
    logic<COUNT * WIDTH> out = 0;
    // Sign extension and masks commonly repeat one bit dozens of times.
    // Fill storage bytes directly for one-bit and byte-aligned patterns.
    // Retain the generic bit path only for genuinely unaligned repeated fields.
    if constexpr (WIDTH == 1) {
        const uint8_t fill = value.get(0) ? 0xffu : 0u;
        for (size_t byte = 0; byte < out.SIZE; ++byte) {
            out.bytes[byte] = fill;
        }
        if constexpr (((COUNT * WIDTH) % 8) != 0) {
            out.bytes[out.SIZE - 1] &=
                static_cast<uint8_t>((1u << ((COUNT * WIDTH) % 8)) - 1u);
        }
    }
    else if constexpr ((WIDTH % 8) == 0) {
        for (size_t rep = 0; rep < COUNT; ++rep) {
            for (size_t byte = 0; byte < value.SIZE; ++byte) {
                out.bytes[rep * value.SIZE + byte] = value.bytes[byte];
            }
        }
    }
    else {
        for (size_t rep = 0; rep < COUNT; ++rep) {
            for (size_t bit = 0; bit < WIDTH; ++bit) {
                out.set(rep * WIDTH + bit, value.get(bit));
            }
        }
    }
    return out;
}

template<size_t WIDTH>
constexpr logic<1> reduce_and(const logic<WIDTH>& value)
{
    for (size_t bit = 0; bit < WIDTH; ++bit) {
        if (!value.get(bit)) {
            return logic<1>(0);
        }
    }
    return logic<1>(1);
}

template<typename T, size_t TOTAL_BITS, size_t ELEMENT_BITS>
constexpr logic<1> reduce_and(const detail::array_packed_ref<T, TOTAL_BITS, ELEMENT_BITS>& value)
{
    return reduce_and(logic<ELEMENT_BITS>(value));
}

template<typename T, size_t N, bool PACKED>
constexpr logic<1> reduce_and(const array<N, T, PACKED>& value)
{
    for (size_t i = 0; i < N; ++i) {
        if (!static_cast<bool>(value[i])) {
            return logic<1>(0);
        }
    }
    return logic<1>(1);
}

template<typename T, typename V>
constexpr void sv_assign_field(T& dst, const V& value)
{
    // Generated aggregate assignments should write their final destination in place.
    // Prefer its native assignment operator, and retain the packed conversion used by
    // hdlcpp's former return-by-value lambda when the source type is not assignable.
    if constexpr (std::is_arithmetic_v<T> && !std::is_arithmetic_v<std::remove_cv_t<std::remove_reference_t<V>>>) {
        dst = static_cast<T>(value);
    }
    else if constexpr (!std::is_arithmetic_v<T> && std::is_arithmetic_v<V> &&
                       std::is_aggregate_v<T>) {
        // A packed aggregate assigned scalar zero is exactly its value-initialized form.
        // Avoid constructing and unpacking an all-zero packed vector field by field;
        // retain the generated packed assignment operator for every nonzero value.
        if (value == 0) {
            dst = T{};
        }
        else {
            dst = value;
        }
    }
    else if constexpr (std::is_assignable_v<T&, const V&>) {
        dst = value;
    }
    else {
        dst = unpack_value<T>(pack_value<type_width<T>()>(value));
    }
}

template<typename T, size_t N, typename V>
constexpr void sv_assign_field(std::array<T, N>& dst, std::initializer_list<V> values)
{
    size_t i = 0;
    for (const auto& value : values) {
        if (i >= N) {
            break;
        }
        sv_assign_field(dst[i++], value);
    }
}

template<typename T, size_t N, typename V>
constexpr void sv_assign_field(std::array<T, N>& dst, const V& value)
{
    if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<V>>, std::array<T, N>>) {
        dst = value;
    }
    else {
        for (auto& item : dst) {
            sv_assign_field(item, value);
        }
    }
}

// Generated standalone bit assignments do not need a writable slice object.
// Keep narrow values in native words and touch only one byte of wide values.
// Type dispatch happens in C++, not by guessing a template's printed name in
// the scheduler; wrappers and array elements retain their assignment semantics.
// Keep this small lowering primitive at the callsite so known proxy offsets and
// bounds disappear, rather than making the parent escape into an opaque call.
template<typename Target, typename Value>
__attribute__((always_inline)) constexpr void sv_assign_bit(Target& target, size_t index, Value&& value)
{
    if constexpr (is_logic_v<Target> && !std::is_const_v<Target>) {
        cpphdl_assert(index < Target::_size_bits(), "wrong bitnum");
        const uint64_t bit = uint64_t(logic<1>(value));
        if constexpr (Target::_size_bits() <= 64) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            if (!detail::is_constant_evaluated_compat()) {
                // Copy the actual store, including untouched padding. Going
                // through logic::operator= splits the store to mask padding,
                // defeating native load/store forwarding on the next read.
                uint64_t word = 0;
                std::memcpy(&word, target.bytes, Target::SIZE);
                word = (word & ~(uint64_t{1} << index)) | (bit << index);
                std::memcpy(target.bytes, &word, Target::SIZE);
                return;
            }
#endif
        }
        target.set(index, bit);
    }
    else if constexpr (detail::is_array_packed_ref<std::remove_cv_t<Target>>::value &&
                       !std::is_const_v<Target>) {
        using element_type = typename detail::is_array_packed_ref<Target>::element_type;
        if constexpr (is_logic_v<element_type>) {
            // A packed element already identifies its parent and bit offset.
            // Write that store directly, without constructing another slice or
            // depending on the host compiler to inline generic writeback loops.
            cpphdl_assert(index < element_type::_size_bits(), "wrong packed array element bit range");
            sv_assign_bit(*target.ref.parent, target.ref.first + index,
                          std::forward<Value>(value));
        }
        else {
            target[index] = std::forward<Value>(value);
        }
    }
    else {
        target[index] = std::forward<Value>(value);
    }
}

template<typename T, size_t N, bool PACKED, typename V>
constexpr void sv_assign_field(array<N, T, PACKED>& dst, const V& value)
{
    // Scalar assignment to a packed array means assignment to its complete bit field.
    // Element-by-element conversion repeated the scalar and changed packed semantics.
    // Repack compatible scalar sources once and unpack them into the destination shape.
    using src_t = std::remove_cv_t<std::remove_reference_t<V>>;
    // Registers publicly derive from their current-value type. Copy that
    // array subobject once; broadcasting it repacks the whole source for each
    // element and, more importantly, gives every element the wrong value.
    using array_t = array<N, T, PACKED>;
    if constexpr (std::is_convertible_v<const src_t*, const array_t*>) {
        dst = static_cast<const array_t&>(value);
    }
    else if constexpr (std::is_convertible_v<const src_t*, const array<N, T, !PACKED>*>) {
        // Projected interface fields may use different packed/unpacked storage.
        // Preserve element indices: broadcasting the complete vector loses every
        // request except bit zero. Registers also expose this current-value base.
        const auto& source = static_cast<const array<N, T, !PACKED>&>(value);
        for (size_t index = 0; index < N; ++index) {
            if constexpr (PACKED) {
                auto item = dst[index];
                sv_assign_field(item, source[index]);
            }
            else {
                sv_assign_field(dst[index], source[index]);
            }
        }
    }
    else if constexpr (PACKED && (detail::has_pack_method<src_t>::value || is_logic_v<src_t> || std::is_integral_v<src_t> || std::is_enum_v<src_t>)) {
        dst = unpack_value<array<N, T, PACKED>>(pack_value<type_width<array<N, T, PACKED>>()>(value));
    }
    else {
        for (size_t i = 0; i < N; ++i) {
            if constexpr (PACKED) {
                auto item = dst[i];
                sv_assign_field(item, value);
            }
            else {
                sv_assign_field(dst[i], value);
            }
        }
    }
}

}

#ifndef CPPHDL_STATIC  // static version is faster but not used now

#define _PORT(A...)  cpphdl::function_ref<A>
#define _ASSIGN(a...)  [&]() { return a; }  // any expression (uses std::function, captures all object's pointers in a call chain, using heap)
#define _ASSIGN_REG(a...)  [&]() { return &a; }  // (faster) register or comb() returning ref to lvalue (uses function_ref, captures only one ref, dont use heap)
#define _ASSIGN_COMB(a...)  _ASSIGN_REG(a)  // all comb functions return reference to lvalue in cpphdl, but keep called when being accessed

#define _ASSIGN_I(a...)  [&,i]() { return a; }  // any expression
#define _ASSIGN_J(a...)  [&,j]() { return a; }
#define _ASSIGN_IJ(a...)  [&,i,j]() { return a; }
#define _ASSIGN_REG_I(a...)  [&,i]() { return &a; }  // (faster) register or comb() returning ref to lvalue
#define _ASSIGN_REG_J(a...)  [&,j]() { return &a; }
#define _ASSIGN_REG_IJ(a...)  [&,i,j]() { return &a; }
#define _ASSIGN_COMB_I(a...)  _ASSIGN_REG_I(a)  // all comb functions return reference to lvalue
#define _ASSIGN_COMB_J(a...)  _ASSIGN_REG_J(a)
#define _ASSIGN_COMB_IJ(a...)  _ASSIGN_REG_IJ(a)

// captures any indexes: _ASSIGN_INDEXED((i,j,k), out_reg[i+j+k])
#define CPPHDL_UNPAREN(a...) a
#define _ASSIGN_INDEXED(caps, a...) [&, CPPHDL_UNPAREN caps]() { return a; }  // expression
#define _ASSIGN_REG_INDEXED(caps, a...)  [&, CPPHDL_UNPAREN caps]() { return &a; }  // (faster) register or comb returning &
#define _ASSIGN_COMB_INDEXED(a...)  _ASSIGN_REG_INDEXED(a)

// Caching is simulation-only; no timestamp or early return belongs in RTL.
#ifndef SYNTHESIS
#define _LAZY_COMB(name, type...) \
    type name; \
    long __prev__system_clock_##name = -1; \
    type& name##_func() { \
        if (__prev__system_clock_##name == _system_clock) { \
            return name; \
        } \
        __prev__system_clock_##name = _system_clock;
#else
#define _LAZY_COMB(name, type...) \
    type name; \
    type& name##_func() {
#endif

#else  // legacy CPPHDL_STATIC - requires all methods to be static - 2 times faster but does not support arrays of modules -> not supported now

#define _PORT(A...) inline static cpphdl::function_ref<A>
#define _ASSIGN(a...) +[]() { static auto tmp = a; tmp = a; return &tmp; }  // expression
#define _ASSIGN_REG(a...)  +[]() { return &a; }  // variable
#define _ASSIGN_COMB(a...)  _ASSIGN_REG(a)

// Keep the legacy static variant subject to the same synthesis boundary.
#ifndef SYNTHESIS
#define _LAZY_COMB(name, type...) \
    inline static type name; \
    inline static long __prev__system_clock_##name = -1; \
    static type& name##_func() { \
        if (__prev__system_clock_##name == _system_clock) { \
            return name; \
        } \
        __prev__system_clock_##name = _system_clock;
#else
#define _LAZY_COMB(name, type...) \
    inline static type name; \
    static type& name##_func() {
#endif

#endif

#if !defined(CPPHDL_DISABLE_STD_FORMAT) && defined(__has_include)
#if __has_include(<format>)
#include <format>
#endif
#elif !defined(CPPHDL_DISABLE_STD_FORMAT)
#include <format>
#endif

#if defined(CPPHDL_DISABLE_STD_PRINT)
#ifdef CPPHDL_HAS_STD_PRINT
#undef CPPHDL_HAS_STD_PRINT
#endif
#elif defined(__has_include)
#if __has_include(<print>)
#include <print>
#if defined(__cpp_lib_print) && (__cpp_lib_print >= 202207L)
#define CPPHDL_HAS_STD_PRINT 1
#endif
#endif
#else
#include <print>
#if defined(__cpp_lib_print) && (__cpp_lib_print >= 202207L)
#define CPPHDL_HAS_STD_PRINT 1
#endif
#endif
