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
struct array_packed_size_bits
{
    constexpr static size_t value = sizeof(TYPE) * 8;
};

template<typename TYPE>
struct array_packed_size_bits<TYPE, std::void_t<decltype(std::remove_cv_t<std::remove_reference_t<TYPE>>::_size_bits())>>
{
    constexpr static size_t value = std::remove_cv_t<std::remove_reference_t<TYPE>>::_size_bits();
};

template<typename TYPE, typename = void>
struct array_can_static_cast_uint64
{
    constexpr static bool value = false;
};

template<typename TYPE>
struct array_can_static_cast_uint64<TYPE, std::void_t<decltype(static_cast<uint64_t>(std::declval<const TYPE&>()))>>
{
    constexpr static bool value = true;
};

template<size_t WIDTH, typename TYPE>
logic<WIDTH> array_pack_value(const TYPE& value)
{
    if constexpr (requires { value.pack(); }) {
        return logic<WIDTH>(value.pack());
    }
    else if constexpr (is_logic_v<TYPE>) {
        return logic<WIDTH>(value);
    }
    else if constexpr (array_can_static_cast_uint64<TYPE>::value) {
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
    else if constexpr (requires(TYPE v) { v = 0; }) {
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

    template<typename T>
    array_packed_ref& operator=(const T& value)
    {
        if constexpr (requires { value.pack(); }) {
            ref = value.pack();
        }
        else if constexpr (array_can_static_cast_uint64<T>::value && !is_logic_v<T> && !is_logic_bits_v<T>) {
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

    logic_bits<TOTAL_BITS> operator[](size_t bitnum)
    {
        return bits(bitnum, bitnum);
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
        else if constexpr (requires(T value, logic<ELEMENT_BITS> bits) { value = bits; }) {
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

template<typename TYPE, size_t COUNT, bool PACKED>
struct array;

template<typename TYPE, size_t COUNT>
struct array<TYPE, COUNT, false> : public bitops<logic<COUNT * detail::array_packed_size_bits<TYPE>::value>>
{
    constexpr static size_t ELEMENT_BITS = detail::array_packed_size_bits<TYPE>::value;
    constexpr static size_t SIZE_BITS = COUNT * ELEMENT_BITS;
    constexpr static size_t SIZE = (SIZE_BITS + 7) / 8;
    constexpr static bool PACKED = false;
    TYPE data[COUNT];

    constexpr static size_t _size_bits()
    {
        return SIZE_BITS;
    }

    array() = default;

    array(const array<TYPE,COUNT,false>& other) = default;

    template<typename T>
    array(const T& other) : bitops<logic<SIZE_BITS>>(other) {}

    array& operator=(const array<TYPE,COUNT,false>& other) = default;

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

    logic<SIZE_BITS> pack() const
    {
        logic<SIZE_BITS> packed = 0;
        for (size_t i = 0; i < COUNT; ++i) {
            auto elem = detail::array_pack_value<ELEMENT_BITS>(data[i]);
            for (size_t bit = 0; bit < ELEMENT_BITS; ++bit) {
                packed.set(i * ELEMENT_BITS + bit, elem.get(bit));
            }
        }
        return packed;
    }

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

    using bitops<logic<SIZE_BITS>>::operator=;
    using bitops<logic<SIZE_BITS>>::operator&;
    using bitops<logic<SIZE_BITS>>::operator|;
    using bitops<logic<SIZE_BITS>>::operator^;
    using bitops<logic<SIZE_BITS>>::operator~;
    using bitops<logic<SIZE_BITS>>::operator<<;
    using bitops<logic<SIZE_BITS>>::operator>>;
    using bitops<logic<SIZE_BITS>>::operator+;
    using bitops<logic<SIZE_BITS>>::operator-;
    using bitops<logic<SIZE_BITS>>::operator==;
    using bitops<logic<SIZE_BITS>>::operator!=;
    using bitops<logic<SIZE_BITS>>::operator<;
    using bitops<logic<SIZE_BITS>>::operator<=;
    using bitops<logic<SIZE_BITS>>::operator>;
    using bitops<logic<SIZE_BITS>>::operator>=;
    using bitops<logic<SIZE_BITS>>::to_ullong;
    using bitops<logic<SIZE_BITS>>::to_hex;

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

    explicit operator uint64_t() const
    {
        return pack().to_ullong();
    }

    explicit operator bool() const
    {
        return pack().to_ullong();
    }

    explicit operator uint32_t() const
    {
        return pack().to_ullong();
    }

    explicit operator uint16_t() const
    {
        return pack().to_ullong();
    }

    explicit operator uint8_t() const
    {
        return pack().to_ullong();
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

template<typename TYPE, size_t COUNT>
struct array<TYPE, COUNT, true> : public bitops<logic<COUNT * detail::array_packed_size_bits<TYPE>::value>>
{
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
    array(const array<TYPE, COUNT, true>& other) = default;

    template<typename T>
    array(const T& other)
    {
        data = other;
    }

    array& operator=(const array<TYPE, COUNT, true>& other) = default;

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
        else if constexpr (requires(TYPE value, logic<ELEMENT_BITS> bits) { value = bits; }) {
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
struct array<void,COUNT,PACKED> {};


}
