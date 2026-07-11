#pragma once

#include "cpphdl_bitops.h"
#include <type_traits>
#include <initializer_list>
#include <utility>

namespace cpphdl
{


template<typename T, size_t S, bool PACKED = false>
struct array;

template<size_t WIDTH>
struct logic;

template<size_t WIDTH>
struct logic_bits;

template<typename T>
struct is_logic_bits : std::false_type {};

template<size_t WIDTH>
struct is_logic_bits<logic_bits<WIDTH>> : std::true_type {};

template<typename T>
inline constexpr bool is_logic_bits_v = is_logic_bits<std::remove_cv_t<std::remove_reference_t<T>>>::value;

template<typename T>
struct is_logic : std::false_type {};

template<size_t WIDTH>
struct is_logic<logic<WIDTH>> : std::true_type {};

template<typename T>
inline constexpr bool is_logic_v = is_logic<std::remove_cv_t<std::remove_reference_t<T>>>::value;

namespace detail
{

template<typename T, typename = void>
struct has_pack_method : std::false_type {};

template<typename T>
struct has_pack_method<T, std::void_t<decltype(std::declval<const T&>().pack())>> : std::true_type {};

template<typename T, typename = void>
struct can_static_cast_uint64 : std::false_type {};

template<typename T>
struct can_static_cast_uint64<T, std::void_t<decltype(static_cast<uint64_t>(std::declval<const T&>()))>> : std::true_type {};

template<typename T, typename V, typename = void>
struct can_assign_from : std::false_type {};

template<typename T, typename V>
struct can_assign_from<T, V, std::void_t<decltype(std::declval<T&>() = std::declval<V>())>> : std::true_type {};

}

template<size_t WIDTH>
struct logic : public bitops<logic<WIDTH>>
{
    constexpr static size_t SIZE = (WIDTH+7)/8;
    uint8_t bytes[SIZE];

    constexpr static size_t _size_bits()
    {
        return WIDTH;
    }

    constexpr logic() = default;
    constexpr logic(const logic& other) = default;

    template<size_t WIDTH1>
    constexpr logic(const logic<WIDTH1>& other);

    template<size_t WIDTH1>
    constexpr logic(const logic_bits<WIDTH1>& other);

    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
    constexpr logic(T other) : bytes{}
    {
        uint64_t value = static_cast<uint64_t>(other);
        for (size_t i = 0; i < SIZE; ++i) {
            bytes[i] = i < sizeof(uint64_t) ? static_cast<uint8_t>((value >> (8 * i)) & 0xffu) : 0;
        }
        if constexpr ((WIDTH % 8) != 0) {
            bytes[SIZE - 1] &= static_cast<uint8_t>((1u << (WIDTH % 8)) - 1u);
        }
    }

    template<typename T, typename std::enable_if_t<!std::is_integral_v<T> && !std::is_enum_v<T> && !is_logic_v<T> && !is_logic_bits_v<T>, int> = 0>
    logic(const T& other) : bytes{}
    {
        if constexpr (detail::has_pack_method<T>::value) {
            *this = other.pack();
        }
        else {
            bitops<logic<WIDTH>>::operator=(other);
        }
    }

    template<typename T>
    constexpr logic(std::initializer_list<T> values) : bytes{}
    {
        size_t dst = WIDTH;
        for (const auto& value : values) {
            uint64_t bits = static_cast<uint64_t>(value);
            size_t srcWidth = 1;
            if constexpr (!std::is_integral_v<T> && !std::is_enum_v<T>) {
                srcWidth = value.size();
            }
            for (size_t i = 0; i < srcWidth && dst > 0; ++i) {
                --dst;
                set(dst, static_cast<uint8_t>((bits >> (srcWidth - 1 - i)) & 1u));
            }
        }
    }

    constexpr logic& operator=(const logic& other) = default;

    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
    constexpr logic& operator=(T other)
    {
        uint64_t value = static_cast<uint64_t>(other);
        for (size_t i = 0; i < SIZE; ++i) {
            bytes[i] = i < sizeof(uint64_t) ? static_cast<uint8_t>((value >> (8 * i)) & 0xffu) : 0;
        }
        if constexpr ((WIDTH % 8) != 0) {
            bytes[SIZE - 1] &= static_cast<uint8_t>((1u << (WIDTH % 8)) - 1u);
        }
        return *this;
    }

    template<typename T, typename std::enable_if_t<!std::is_integral_v<T> && !std::is_enum_v<T> && !is_logic_v<T> && !is_logic_bits_v<T>, int> = 0>
    logic& operator=(const T& other)
    {
        if constexpr (detail::has_pack_method<T>::value) {
            *this = other.pack();
        }
        else {
            bitops<logic<WIDTH>>::operator=(other);
        }
        return *this;
    }

    template<typename T>
    constexpr logic& operator=(std::initializer_list<T> values)
    {
        *this = logic(values);
        return *this;
    }

    template<size_t WIDTH1>
    constexpr logic& operator=(const logic<WIDTH1>& other);

    template<size_t WIDTH1>
    constexpr logic& operator=(const logic_bits<WIDTH1>& other);

    logic_bits<WIDTH> bits(size_t last, size_t first);
    constexpr logic<WIDTH> bits(size_t last, size_t first) const;
    logic_bits<WIDTH> operator[](size_t bitnum);
    constexpr logic<1> operator[](size_t bitnum) const;

    constexpr uint8_t get(size_t bitnum) const
    {
        return (bytes[bitnum/8]>>(bitnum%8))&1;
    }

    constexpr void set(size_t bitnum, uint8_t in)
    {
        in &= 1;
        bytes[bitnum/8] = (bytes[bitnum/8]&~(1<<(bitnum%8)))|(in<<(bitnum%8));
    }

    using bitops<logic<WIDTH>>::operator&;
    using bitops<logic<WIDTH>>::operator|;
    using bitops<logic<WIDTH>>::operator^;
    using bitops<logic<WIDTH>>::operator<<;
    using bitops<logic<WIDTH>>::operator>>;
    using bitops<logic<WIDTH>>::operator+;
    using bitops<logic<WIDTH>>::operator-;
    using bitops<logic<WIDTH>>::operator==;
    using bitops<logic<WIDTH>>::operator!=;
    using bitops<logic<WIDTH>>::operator<;
    using bitops<logic<WIDTH>>::operator<=;
    using bitops<logic<WIDTH>>::operator>;
    using bitops<logic<WIDTH>>::operator>=;
    using bitops<logic<WIDTH>>::to_ullong;
    using bitops<logic<WIDTH>>::to_hex;

    logic& operator<<=(uint64_t shift)
    {
        return *this = *this << shift;
    }

    logic& operator>>=(uint64_t shift)
    {
        return *this = *this >> shift;
    }

    logic& operator++()
    {
        *this = logic(static_cast<uint64_t>(*this) + 1);
        return *this;
    }

    logic operator++(int)
    {
        logic tmp = *this;
        ++(*this);
        return tmp;
    }

    logic& operator--()
    {
        *this = logic(static_cast<uint64_t>(*this) - 1);
        return *this;
    }

    logic operator--(int)
    {
        logic tmp = *this;
        --(*this);
        return tmp;
    }

    template<size_t WIDTH1>
    logic& operator&=(const logic<WIDTH1>& in)
    {
        return *this = *this & in;
    }

    template<size_t WIDTH1>
    logic& operator|=(const logic<WIDTH1>& in)
    {
        return *this = *this | in;
    }

    template<size_t WIDTH1>
    logic& operator^=(const logic<WIDTH1>& in)
    {
        return *this = *this ^ in;
    }

    // The inherited bitops complement was not constexpr for logic constants.
    // SystemVerilog requires width-limited inversion rather than host integer promotion.
    // Invert each declared bit into a same-width logic result.
    constexpr logic operator~() const
    {
        logic result{};
        for (size_t i = 0; i < WIDTH; ++i) {
            result.set(i, get(i) ? 0 : 1);
        }
        return result;
    }

    constexpr uint64_t to_uint64_constexpr() const
    {
        uint64_t value = 0;
        constexpr size_t limit = SIZE < sizeof(uint64_t) ? SIZE : sizeof(uint64_t);
        for (size_t i = 0; i < limit; ++i) {
            value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
        }
        return value;
    }

    constexpr operator uint64_t() const
    {
        return to_uint64_constexpr();
    }

    template<size_t W = WIDTH, typename std::enable_if_t<W == 8, int> = 0>
    constexpr operator unsigned char() const
    {
        return static_cast<unsigned char>(to_uint64_constexpr());
    }

    template<size_t W = WIDTH, typename std::enable_if_t<W == 16, int> = 0>
    constexpr operator unsigned short() const
    {
        return static_cast<unsigned short>(to_uint64_constexpr());
    }

    template<size_t W = WIDTH, typename std::enable_if_t<W == 32, int> = 0>
    constexpr operator unsigned int() const
    {
        return static_cast<unsigned int>(to_uint64_constexpr());
    }

    template<size_t W = WIDTH, typename std::enable_if_t<W == 8, int> = 0>
    constexpr operator signed char() const
    {
        return static_cast<signed char>(to_uint64_constexpr());
    }

    template<size_t W = WIDTH, typename std::enable_if_t<W == 16, int> = 0>
    constexpr operator signed short() const
    {
        return static_cast<signed short>(to_uint64_constexpr());
    }

    template<size_t W = WIDTH, typename std::enable_if_t<W == 32, int> = 0>
    constexpr operator signed int() const
    {
        return static_cast<signed int>(to_uint64_constexpr());
    }

    template<size_t W = WIDTH, typename std::enable_if_t<W == 64, int> = 0>
    constexpr operator signed long() const
    {
        return static_cast<signed long>(to_uint64_constexpr());
    }

    explicit constexpr operator bool() const
    {
        return to_uint64_constexpr() != 0;
    }

    explicit constexpr operator uint32_t() const
    {
        return static_cast<uint32_t>(to_uint64_constexpr());
    }

    explicit constexpr operator uint16_t() const
    {
        return static_cast<uint16_t>(to_uint64_constexpr());
    }

    explicit constexpr operator uint8_t() const
    {
        return static_cast<uint8_t>(to_uint64_constexpr());
    }

    std::string to_string() const
    {
        std::string hex;
        for (size_t i = 0; i < size(); ++i) {
            hex = std::string(this->get(i)?"1":"0") + hex;
        }
        return hex;
    }

    constexpr size_t size() const
    {
        return WIDTH;
    }
};

template<size_t WIDTH>
template<size_t WIDTH1>
constexpr logic<WIDTH>::logic(const logic<WIDTH1>& other)
{
    *this = other;
}

template<size_t WIDTH>
template<size_t WIDTH1>
constexpr logic<WIDTH>::logic(const logic_bits<WIDTH1>& other)
{
    *this = other;
}

template<size_t WIDTH>
template<size_t WIDTH1>
constexpr logic<WIDTH>& logic<WIDTH>::operator=(const logic<WIDTH1>& other)
{
    constexpr size_t SRC_SIZE = logic<WIDTH1>::SIZE;
    for (size_t i = 0; i < SIZE; ++i) {
        bytes[i] = i < SRC_SIZE ? other.bytes[i] : 0;
    }
    if constexpr ((WIDTH % 8) != 0) {
        bytes[SIZE - 1] &= static_cast<uint8_t>((1u << (WIDTH % 8)) - 1u);
    }
    return *this;
}

template<size_t WIDTH>
template<size_t WIDTH1>
constexpr logic<WIDTH>& logic<WIDTH>::operator=(const logic_bits<WIDTH1>& other)
{
    constexpr size_t SRC_SIZE = logic<WIDTH1>::SIZE;
    const auto& src = static_cast<const logic<WIDTH1>&>(other);
    for (size_t i = 0; i < SIZE; ++i) {
        bytes[i] = i < SRC_SIZE ? src.bytes[i] : 0;
    }
    if constexpr ((WIDTH % 8) != 0) {
        bytes[SIZE - 1] &= static_cast<uint8_t>((1u << (WIDTH % 8)) - 1u);
    }
    return *this;
}

template<size_t WIDTH>
struct logic_bits : public logic<WIDTH>
{
    logic<WIDTH>* parent;
    size_t first;
    size_t last;

    logic_bits() = delete;
    logic_bits(const logic_bits& other) = delete;

    logic_bits(logic<WIDTH>* parent, size_t first, size_t last) : parent(parent), first(first), last(last)
    {
        if ((first%8)==0 && first != last) {
            memcpy(logic<WIDTH>::bytes, &parent->bytes[first/8], (last+1-first+7)/8);
            memset(&logic<WIDTH>::bytes[(last+1-first+7)/8], 0, sizeof(logic<WIDTH>::bytes)-(last+1-first+7)/8);
            for (size_t i=last+1-first; i%8 != 0; ++i) {
                logic<WIDTH>::set(i, 0);
            }
        }
        else {
            memset(logic<WIDTH>::bytes, 0, sizeof(logic<WIDTH>::bytes));
            size_t j = 0;
            for (size_t i=first; i <= last; ++i) {
                logic<WIDTH>::set(j++, parent->get(i));
            }
        }
    }

    void updateParent() const
    {
        if (parent) {  // if parent==0 then it can be pointer to logic, not logic_bits - we can use only logic
            if ((first%8)==0) {
                memcpy(&parent->bytes[first/8], logic<WIDTH>::bytes, (last+1-first)/8);
                size_t j = (last-first)/8*8;
                for (size_t i=last/8*8; i <= last; ++i) {  // rest bits
                    parent->set(i, this->get(j++));
                }
            }
            else {
                size_t j = 0;
                for (size_t i=first; i <= last; ++i) {  // todo? optimize with ulong
                    parent->set(i, this->get(j++));
                }
            }
//            parent->updateParent();
        }
    }

    logic_bits& operator=(const logic_bits<WIDTH>& other)
    {
        *this = (logic<WIDTH>)other;
        updateParent();
        return *this;
    }

    template<size_t WIDTH1>
    logic_bits& operator=(const logic_bits<WIDTH1>& other)
    {
        *this = (logic<WIDTH1>)other;
        updateParent();
        return *this;
    }

    template<size_t WIDTH1>
    logic_bits& operator=(const logic<WIDTH1>& other)
    {
        *(logic<WIDTH>*)this = other;
        updateParent();
        return *this;
    }

    logic_bits& operator=(uint64_t other)
    {
        *(logic<WIDTH>*)this = other;
        updateParent();
        return *this;
    }

    template<typename T, typename std::enable_if_t<!std::is_integral_v<T> && !std::is_enum_v<T> && !is_logic_v<T> && !is_logic_bits_v<T>, int> = 0>
    logic_bits& operator=(const T& other)
    {
        *(logic<WIDTH>*)this = other;
        updateParent();
        return *this;
    }

    using logic<WIDTH>::operator&;
    using logic<WIDTH>::operator|;
    using logic<WIDTH>::operator^;
    using logic<WIDTH>::operator~;
    using logic<WIDTH>::operator<<;
    using logic<WIDTH>::operator>>;
    using logic<WIDTH>::operator+;
    using logic<WIDTH>::operator-;
    using logic<WIDTH>::operator==;
    using logic<WIDTH>::operator!=;
    using logic<WIDTH>::operator<;
    using logic<WIDTH>::operator<=;
    using logic<WIDTH>::operator>;
    using logic<WIDTH>::operator>=;
    using logic<WIDTH>::to_ullong;
    using logic<WIDTH>::to_hex;

    template<size_t WIDTH1>
    logic_bits& operator&=(const logic<WIDTH1>& in)
    {
        *(logic<WIDTH>*)this &= in;
        updateParent();
        return *this;
    }

    template<size_t WIDTH1>
    logic_bits& operator|=(const logic<WIDTH1>& in)
    {
        *(logic<WIDTH>*)this |= in;
        updateParent();
        return *this;
    }

    template<size_t WIDTH1>
    logic_bits& operator^=(const logic<WIDTH1>& in)
    {
        *(logic<WIDTH>*)this ^= in;
        updateParent();
        return *this;
    }

    logic_bits& operator&=(uint64_t in)
    {
        *(logic<WIDTH>*)this &= logic<WIDTH>(in);
        updateParent();
        return *this;
    }

    logic_bits& operator|=(uint64_t in)
    {
        *(logic<WIDTH>*)this |= logic<WIDTH>(in);
        updateParent();
        return *this;
    }

    logic_bits& operator^=(uint64_t in)
    {
        *(logic<WIDTH>*)this ^= logic<WIDTH>(in);
        updateParent();
        return *this;
    }
};

template<size_t WIDTH>
logic_bits<WIDTH> logic<WIDTH>::bits(size_t last, size_t first)
{
    cpphdl_assert(first < WIDTH && last < WIDTH && first <= last, "wrong first or last bitnumber");
    return logic_bits(this, first, last);
}

template<size_t WIDTH>
constexpr logic<WIDTH> logic<WIDTH>::bits(size_t last, size_t first) const
{
    cpphdl_assert(first < WIDTH && last < WIDTH && first <= last, "wrong first or last bitnumber");
    logic<WIDTH> ret = 0;
    size_t dst = 0;
    for (size_t src = first; src <= last; ++src) {
        ret.set(dst++, get(src));
    }
    return ret;
}

template<size_t WIDTH>
logic_bits<WIDTH> logic<WIDTH>::operator[](size_t bitnum)
{
    cpphdl_assert(bitnum < WIDTH, "wrong bitnum");
    return logic_bits<WIDTH>(this, bitnum, bitnum);
}

template<size_t WIDTH>
constexpr logic<1> logic<WIDTH>::operator[](size_t bitnum) const
{
    cpphdl_assert(bitnum < WIDTH, "wrong bitnum");
    return logic<1>(get(bitnum));
}


}
