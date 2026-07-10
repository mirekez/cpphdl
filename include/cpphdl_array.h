#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

#include "cpphdl_logic.h"

namespace cpphdl
{

namespace detail
{

template<typename TYPE, typename = void>
struct is_packed_array : std::false_type {};

template<typename TYPE>
struct is_packed_array<TYPE, std::void_t<typename TYPE::value_type,
    decltype(TYPE::COUNT_VALUE), decltype(TYPE::PACKED)>> : std::bool_constant<TYPE::PACKED> {};

struct array_packed_offset_t {};

template<typename TYPE, typename = void>
struct array_packed_size_bits
{
    constexpr static size_t value = sizeof(TYPE) * 8;
};

template<typename TYPE>
struct array_packed_size_bits<TYPE, std::void_t<decltype(std::remove_cv_t<std::remove_reference_t<TYPE>>::_size_bits())>>
{
    constexpr static size_t value = std::remove_cv_t<std::remove_reference_t<TYPE>>::_size_bits();
};

template<size_t WIDTH, typename TYPE>
logic<WIDTH> array_pack_value(const TYPE& value)
{
    if constexpr (has_pack_method<TYPE>::value) {
        return logic<WIDTH>(value.pack());
    }
    else if constexpr (is_logic_v<TYPE>) {
        return logic<WIDTH>(value);
    }
    else if constexpr (can_static_cast_uint64<TYPE>::value) {
        return logic<WIDTH>(static_cast<uint64_t>(value));
    }
    else {
        return logic<WIDTH>(0);
    }
}

template<typename TYPE, size_t WIDTH>
TYPE array_unpack_value(const logic<WIDTH>& value)
{
    TYPE out{};
    if constexpr (is_logic_v<TYPE>) {
        out = TYPE(value);
    }
    else if constexpr (std::is_assignable_v<TYPE&, logic<WIDTH>>) {
        out = value;
    }
    else if constexpr (std::is_integral_v<TYPE> || std::is_enum_v<TYPE> || std::is_constructible_v<TYPE, uint64_t>) {
        out = TYPE(static_cast<uint64_t>(value));
    }
    else if constexpr (can_assign_from<TYPE, int>::value) {
        out = 0;
    }
    return out;
}

template<typename TYPE, size_t TOTAL_BITS, size_t ELEMENT_BITS>
struct array_packed_ref
{
    logic_bits<TOTAL_BITS> ref;

    array_packed_ref(logic<TOTAL_BITS>* parent, size_t index)
        : ref(parent, index * ELEMENT_BITS, index * ELEMENT_BITS + ELEMENT_BITS - 1)
    {
    }

    array_packed_ref(logic<TOTAL_BITS>* parent, size_t first, array_packed_offset_t)
        : ref(parent, first, first + ELEMENT_BITS - 1)
    {
    }

    template<typename T>
    array_packed_ref& operator=(const T& value)
    {
        if constexpr (has_pack_method<T>::value) {
            ref = value.pack();
        }
        else if constexpr (can_static_cast_uint64<T>::value && !is_logic_v<T> && !is_logic_bits_v<T>) {
            ref = static_cast<uint64_t>(value);
        }
        else {
            ref = value;
        }
        return *this;
    }

    array_packed_ref& operator=(uint64_t value)
    {
        ref = value;
        return *this;
    }

    logic_bits<TOTAL_BITS>& bits()
    {
        return ref;
    }

    logic_bits<TOTAL_BITS> bits(size_t last, size_t first)
    {
        cpphdl_assert(first < ELEMENT_BITS && last < ELEMENT_BITS && first <= last, "wrong packed array element bit range");
        return logic_bits<TOTAL_BITS>(ref.parent, ref.first + first, ref.first + last);
    }

    logic<ELEMENT_BITS> bits(size_t last, size_t first) const
    {
        cpphdl_assert(first < ELEMENT_BITS && last < ELEMENT_BITS && first <= last, "wrong packed array element bit range");
        return logic<ELEMENT_BITS>(ref).bits(last, first);
    }

    auto operator[](size_t index)
    {
        if constexpr (is_packed_array<TYPE>::value) {
            // Nested packed indexing must keep referencing the outer bit store.
            using element_type = typename TYPE::value_type;
            constexpr size_t element_bits = array_packed_size_bits<element_type>::value;
            cpphdl_assert(index < TYPE::COUNT_VALUE, "wrong nested packed array index");
            return array_packed_ref<element_type, TOTAL_BITS, element_bits>(
                ref.parent, ref.first + index * element_bits, array_packed_offset_t{});
        }
        else {
            return bits(index, index);
        }
    }

    logic<1> operator[](size_t bitnum) const
    {
        return logic<ELEMENT_BITS>(ref)[bitnum];
    }

    operator logic<ELEMENT_BITS>() const
    {
        return logic<ELEMENT_BITS>(ref);
    }

    template<typename T = TYPE, typename std::enable_if_t<!is_logic_v<T>, int> = 0>
    operator T() const
    {
        logic<ELEMENT_BITS> tmp(ref);
        if constexpr (std::is_assignable_v<T&, logic<ELEMENT_BITS>>) {
            T value{};
            value = tmp;
            return value;
        }
        else if constexpr (std::is_integral_v<T> || std::is_enum_v<T> || std::is_constructible_v<T, uint64_t>) {
            return T(static_cast<uint64_t>(tmp));
        }
        else if constexpr (can_assign_from<T, logic<ELEMENT_BITS>>::value) {
            T value{};
            value = tmp;
            return value;
        }
        else {
            T value{};
            std::memcpy(&value, tmp.bytes, std::min(sizeof(value), sizeof(tmp.bytes)));
            return value;
        }
    }

    explicit operator uint64_t() const
    {
        return static_cast<uint64_t>(logic<ELEMENT_BITS>(ref));
    }

    explicit operator bool() const
    {
        return static_cast<bool>(logic<ELEMENT_BITS>(ref));
    }

    template<typename T>
    auto operator|(const T& rhs) const
    {
        return logic<ELEMENT_BITS>(*this) | rhs;
    }

    template<typename T>
    auto operator&(const T& rhs) const
    {
        return logic<ELEMENT_BITS>(*this) & rhs;
    }

    template<typename T>
    auto operator^(const T& rhs) const
    {
        return logic<ELEMENT_BITS>(*this) ^ rhs;
    }

    auto operator~() const
    {
        return ~logic<ELEMENT_BITS>(*this);
    }

    template<typename T>
    auto operator==(const T& rhs) const
    {
        return logic<ELEMENT_BITS>(*this) == rhs;
    }

    template<typename T>
    auto operator!=(const T& rhs) const
    {
        return logic<ELEMENT_BITS>(*this) != rhs;
    }

    template<typename T>
    auto operator&&(const T& rhs) const
    {
        return static_cast<bool>(*this) && static_cast<bool>(rhs);
    }

    template<typename T>
    auto operator||(const T& rhs) const
    {
        return static_cast<bool>(*this) || static_cast<bool>(rhs);
    }
};

} // namespace detail

template<size_t COUNT, typename TYPE, bool PACKED>
struct array;

template<size_t COUNT1, size_t COUNT2, typename TYPE, bool PACKED = false>
using array2D = array<COUNT1, array<COUNT2, TYPE, PACKED>, PACKED>;

template<size_t COUNT1, size_t COUNT2, size_t COUNT3, typename TYPE, bool PACKED = false>
using array3D = array<COUNT1, array2D<COUNT2, COUNT3, TYPE, PACKED>, PACKED>;

template<size_t COUNT1, size_t COUNT2, size_t COUNT3, size_t COUNT4, typename TYPE, bool PACKED = false>
using array4D = array<COUNT1, array3D<COUNT2, COUNT3, COUNT4, TYPE, PACKED>, PACKED>;

template<size_t COUNT, typename TYPE>
struct array<COUNT, TYPE, false> : public bitops<array<COUNT, TYPE, false>>
{
    using BaseOps = bitops<array<COUNT, TYPE, false>>;
    using value_type = TYPE;
    constexpr static size_t COUNT_VALUE = COUNT;
    constexpr static size_t ELEMENT_BITS = sizeof(TYPE) * 8;
    constexpr static size_t SIZE_BITS = COUNT * ELEMENT_BITS;
    constexpr static size_t SIZE = (SIZE_BITS + 7) / 8;
    constexpr static bool PACKED = false;
    TYPE data[COUNT];

    constexpr static size_t _size_bits()
    {
        return SIZE_BITS;
    }

    array() = default;

    array(const array<COUNT, TYPE, false>& other) = default;

    template<typename T>
    array(const T& other)
    {
        *this = other;
    }

    array& operator=(const array<COUNT, TYPE, false>& other) = default;

    template<size_t WIDTH>
    array& operator=(const logic<WIDTH>& value)
    {
        logic<SIZE_BITS> packed = value;
        for (size_t i = 0; i < COUNT; ++i) {
            logic<ELEMENT_BITS> elem = 0;
            for (size_t bit = 0; bit < ELEMENT_BITS; ++bit) {
                elem.set(bit, packed.get(i * ELEMENT_BITS + bit));
            }
            data[i] = detail::array_unpack_value<TYPE>(elem);
        }
        return *this;
    }

    array& operator=(uint64_t value)
    {
        return *this = logic<SIZE_BITS>(value);
    }

    TYPE& operator[](std::size_t i) { return data[i]; }
    const TYPE& operator[](std::size_t i) const { return data[i]; }

    array<COUNT, TYPE, true> pack() const;

    logic_bits<SIZE_BITS> bits(size_t last, size_t first)
    {
        return ((logic<SIZE_BITS>&)*this).bits(last, first);
    }

    operator logic<SIZE_BITS>&()
    {
        return *(logic<SIZE_BITS>*)this;
    }

    operator const logic<SIZE_BITS>&() const
    {
        return *(const logic<SIZE_BITS>*)this;
    }

    using BaseOps::operator=;
    using BaseOps::operator&;
    using BaseOps::operator|;
    using BaseOps::operator^;
    using BaseOps::operator~;
    using BaseOps::operator<<;
    using BaseOps::operator>>;
    using BaseOps::operator+;
    using BaseOps::operator-;
    using BaseOps::operator==;
    using BaseOps::operator!=;
    using BaseOps::operator<;
    using BaseOps::operator<=;
    using BaseOps::operator>;
    using BaseOps::operator>=;
    using BaseOps::to_ullong;
    using BaseOps::to_hex;

    array& operator<<=(size_t shift)
    {
        return (array&)(*this = *this << shift);
    }

    array& operator>>=(size_t shift)
    {
        return (array&)(*this = *this >> shift);
    }

    array& operator|=(const array& other)
    {
        return (array&)(*this = *this | other);
    }

    array& operator&=(const array& other)
    {
        return (array&)(*this = *this & other);
    }

    array& operator^=(const array& other)
    {
        return (array&)(*this = *this ^ other);
    }

    std::string to_string()
    {
        std::string str;
        for (int i=COUNT-1; i >= 0; --i) {
            char buf[10] = {};
            std::snprintf(buf, 10, "%.02lx", (uint64_t)data[i]);
            str += buf;
        }
        return str;
    }
};

template<size_t COUNT, typename TYPE>
struct array<COUNT, TYPE, true> : public bitops<logic<COUNT * detail::array_packed_size_bits<TYPE>::value>>
{
    using value_type = TYPE;
    constexpr static size_t COUNT_VALUE = COUNT;
    constexpr static size_t ELEMENT_BITS = detail::array_packed_size_bits<TYPE>::value;
    constexpr static size_t SIZE_BITS = COUNT * ELEMENT_BITS;
    constexpr static size_t SIZE = (SIZE_BITS + 7) / 8;
    constexpr static bool PACKED = true;

    logic<SIZE_BITS> data;

    constexpr static size_t _size_bits()
    {
        return SIZE_BITS;
    }

    array() = default;
    array(const array<COUNT, TYPE, true>& other) = default;

    template<typename T>
    array(const T& other)
    {
        data = other;
    }

    array& operator=(const array<COUNT, TYPE, true>& other) = default;

    template<typename T>
    array& operator=(const T& other)
    {
        data = other;
        return *this;
    }

    detail::array_packed_ref<TYPE, SIZE_BITS, ELEMENT_BITS> operator[](std::size_t i)
    {
        cpphdl_assert(i < COUNT, "wrong array index");
        return detail::array_packed_ref<TYPE, SIZE_BITS, ELEMENT_BITS>(&data, i);
    }

    TYPE operator[](std::size_t i) const
    {
        cpphdl_assert(i < COUNT, "wrong array index");
        logic<ELEMENT_BITS> tmp = 0;
        for (size_t bit = 0; bit < ELEMENT_BITS; ++bit) {
            tmp.set(bit, data.get(i * ELEMENT_BITS + bit));
        }
        if constexpr (is_logic_v<TYPE>) {
            return TYPE(tmp);
        }
        else if constexpr (std::is_integral_v<TYPE> || std::is_enum_v<TYPE> || std::is_constructible_v<TYPE, uint64_t>) {
            return TYPE(static_cast<uint64_t>(tmp));
        }
        else if constexpr (detail::can_assign_from<TYPE, logic<ELEMENT_BITS>>::value) {
            TYPE value{};
            value = tmp;
            return value;
        }
        else {
            TYPE value{};
            std::memcpy(&value, tmp.bytes, std::min(sizeof(value), sizeof(tmp.bytes)));
            return value;
        }
    }

    logic_bits<SIZE_BITS> bits(size_t last, size_t first)
    {
        return data.bits(last, first);
    }

    logic<SIZE_BITS> pack() const
    {
        return data;
    }

    operator logic<SIZE_BITS>&()
    {
        return data;
    }

    operator const logic<SIZE_BITS>&() const
    {
        return data;
    }

    explicit operator uint64_t() const
    {
        return data.to_ullong();
    }

    explicit operator bool() const
    {
        return data.to_ullong();
    }

    explicit operator uint32_t() const
    {
        return data.to_ullong();
    }

    explicit operator uint16_t() const
    {
        return data.to_ullong();
    }

    explicit operator uint8_t() const
    {
        return data.to_ullong();
    }

    std::string to_string()
    {
        return data.to_hex();
    }
};

template<size_t COUNT, bool PACKED>
struct array<COUNT, void, PACKED> {};

template<size_t COUNT, typename TYPE>
array<COUNT, TYPE, true> array<COUNT, TYPE, false>::pack() const
{
    array<COUNT, TYPE, true> packed;
    for (size_t i = 0; i < COUNT; ++i) {
        packed[i] = data[i];
    }
    return packed;
}


}
