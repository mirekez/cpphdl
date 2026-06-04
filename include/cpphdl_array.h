#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

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
struct array_packed_size_bits<TYPE, std::void_t<decltype(std::remove_cvref_t<TYPE>::_size_bits())>>
{
    constexpr static size_t value = std::remove_cvref_t<TYPE>::_size_bits();
};

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
        if constexpr (requires { static_cast<uint64_t>(value); } && !is_logic_v<T> && !is_logic_bits_v<T>) {
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

    operator logic<ELEMENT_BITS>() const
    {
        return logic<ELEMENT_BITS>(ref);
    }

    template<typename T = TYPE, typename std::enable_if_t<!is_logic_v<T>, int> = 0>
    operator T() const
    {
        if constexpr (is_logic_v<TYPE>) {
            return TYPE(logic<ELEMENT_BITS>(ref));
        }
        else if constexpr (std::is_integral_v<TYPE> || std::is_enum_v<TYPE> || std::is_constructible_v<TYPE, uint64_t>) {
            return TYPE(static_cast<uint64_t>(logic<ELEMENT_BITS>(ref)));
        }
        else {
            TYPE value{};
            logic<ELEMENT_BITS> tmp(ref);
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
};

} // namespace detail

template<typename TYPE, size_t COUNT, bool PACKED>
struct array;

template<typename TYPE, size_t COUNT>
struct array<TYPE, COUNT, false> : public bitops<logic<COUNT*sizeof(TYPE)*8>>
{
    constexpr static size_t SIZE = COUNT*sizeof(TYPE);
    constexpr static size_t SIZE_BITS = SIZE * 8;
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

    TYPE& operator[](std::size_t i) { return data[i]; }
    const TYPE& operator[](std::size_t i) const { return data[i]; }

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
        return to_ullong();
    }

    explicit operator bool() const
    {
        return to_ullong();
    }

    explicit operator uint32_t() const
    {
        return to_ullong();
    }

    explicit operator uint16_t() const
    {
        return to_ullong();
    }

    explicit operator uint8_t() const
    {
        return to_ullong();
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
